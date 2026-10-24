#include "memory_ssa_analysis.hpp"

#include <QHash>
#include <QQueue>
#include <utility>

#include <algorithm>
#include <limits>

namespace reworkbench::memory_ssa_analysis {
namespace {
using namespace decompiler_detail;

constexpr qsizetype max_versions = 16384;
constexpr qsizetype max_read_uses = 8192;

bool is_memory_version(const Node& node) {
    if (node.type.kind != TypeKind::Memory) return false;
    switch (node.op) {
    case Op::Input:
    case Op::Phi:
    case Op::Store:
    case Op::StateField: return true;
    default: return false;
    }
}

QString kind(const Node& node) {
    switch (node.op) {
    case Op::Input: return QStringLiteral("entry");
    case Op::Phi: return QStringLiteral("phi");
    case Op::Store: return QStringLiteral("store");
    case Op::StateField: return QStringLiteral("unknown_effect");
    default: return QStringLiteral("unrecognized");
    }
}

void fail_incomplete(MemorySSAObservation& report, const QString& message) {
    report.limited = true;
    report.complete = false;
    report.warnings << message;
}

} // namespace

MemorySSAObservation observe(const Program& program, std::stop_token stop) {
    MemorySSAObservation result;
    result.warnings << QStringLiteral(
        "Memory versions represent whole-memory IR states. The graph is "
        "structural only, not optimized MemorySSA with alias partitioning, "
        "proven loads, or source-variable recovery.");

    if (program.blocks.isEmpty() || program.entry < 0
        || program.entry >= program.blocks.size()) {
        fail_incomplete(result, QStringLiteral("No valid CFG entry for memory graph."));
        return result;
    }

    // First assign stable report-local IDs to every memory-version node.
    // Two passes are needed because PHI operands can refer to definitions
    // from backedges that appear later in the original IR node array.
    QHash<Value, quint32> ir_to_version;
    for (qsizetype i = 0; i < program.nodes.size(); ++i) {
        if ((i & 0xff) == 0) check_cancelled(stop);
        const Node& node = program.nodes[i];
        if (!is_memory_version(node)) continue;
        if (result.versions.size() >= max_versions) {
            fail_incomplete(result, QStringLiteral(
                "Memory version ceiling reached; graph is incomplete."));
            result.versions.clear();
            return result;
        }
        MemoryVersion version;
        version.id = static_cast<quint32>(result.versions.size());
        version.original_ir_node = static_cast<quint32>(i);
        version.kind = kind(node);
        version.instruction = node.address;
        version.basic_block = node.block;
        version.unknown_effect = node.op == Op::StateField;
        result.opaque_effects_present |= version.unknown_effect;
        ir_to_version.insert(static_cast<Value>(i), version.id);
        result.versions << std::move(version);
    }
    if (result.versions.isEmpty()) {
        fail_incomplete(result, QStringLiteral("No memory versions found in IR."));
        return result;
    }

    bool invalid_reference = false;
    const auto resolve_version = [&](Value ir_id) -> std::optional<quint32> {
        const auto found = ir_to_version.constFind(ir_id);
        if (found == ir_to_version.cend()) return std::nullopt;
        return *found;
    };

    // Link definitions and incoming PHI versions without mutating the IR.
    for (auto& version : result.versions) {
        check_cancelled(stop);
        const auto& node = program.nodes[version.original_ir_node];
        if (node.op == Op::Store) {
            if (node.operands.size() != 3) {
                invalid_reference = true;
                continue;
            }
            version.previous_version = resolve_version(node.operands[0]);
            invalid_reference |= !version.previous_version.has_value();
        } else if (node.op == Op::Phi) {
            if (node.incoming.isEmpty()) {
                invalid_reference = true;
                continue;
            }
            if (node.incoming.size() > 4096) {
                fail_incomplete(result, QStringLiteral(
                    "PHI incoming edge ceiling reached."));
                return result;
            }
            for (const auto& incoming : node.incoming) {
                const auto source = resolve_version(incoming.value);
                if (!source) {
                    invalid_reference = true;
                    continue;
                }
                version.incoming_versions << *source;
                version.incoming_predecessors << incoming.predecessor;
            }
        } else if (node.op == Op::StateField) {
            // StateField contains the opaque effect, which in turn contains
            // the pre-effect state vector. Keeping the predecessor is useful
            // for explaining provenance, NOT for assuming its value survives.
            if (node.operands.size() != 1
                || node.operands[0] >= static_cast<Value>(program.nodes.size())) {
                invalid_reference = true;
                continue;
            }
            const Node& effect = program.nodes[node.operands[0]];
            if (effect.op != Op::Opaque && effect.op != Op::CallOpaque) {
                invalid_reference = true;
                continue;
            }
            const auto slot = program.slot_index.constFind(QStringLiteral("mem"));
            if (slot != program.slot_index.cend() && *slot >= 0
                && *slot < effect.operands.size()) {
                version.previous_version = resolve_version(effect.operands[*slot]);
                if (!version.previous_version) invalid_reference = true;
            } else {
                invalid_reference = true;
            }
        }
    }

    // A Load consumes its *explicit* memory token. This accurately retains
    // control-flow joins and unknown-effect boundaries without pretending
    // that memory locations/alias classes have been proved disjoint.
    for (qsizetype i = 0; i < program.nodes.size(); ++i) {
        if ((i & 0xff) == 0) check_cancelled(stop);
        const auto& node = program.nodes[i];
        if (node.op != Op::Load) continue;
        if (result.reads.size() >= max_read_uses) {
            fail_incomplete(result, QStringLiteral(
                "Memory read-use ceiling reached."));
            break;
        }
        if (node.operands.size() != 2) {
            invalid_reference = true;
            continue;
        }
        const auto input = resolve_version(node.operands[0]);
        if (!input) {
            invalid_reference = true;
            continue;
        }
        MemoryReadUse read;
        read.load_ir_node = static_cast<quint32>(i);
        read.instruction = node.address;
        read.basic_block = node.block;
        read.observed_memory_version = *input;
        result.reads << read;
    }

    // Monotone taint propagation across predecessor/PHI edges, including
    // cycles. Reverse edges + work queue give O(versions + edges), avoiding
    // repeated full-graph scans on large functions.
    QVector<QVector<quint32>> dependents(result.versions.size());
    for (const auto& version : result.versions) {
        if (version.previous_version)
            dependents[*version.previous_version] << version.id;
        for (quint32 parent : version.incoming_versions)
            dependents[parent] << version.id;
    }

    QVector<quint8> tainted(result.versions.size(), quint8(0));
    QQueue<quint32> pending;
    for (const auto& version : result.versions) {
        if (!version.unknown_effect) continue;
        tainted[version.id] = 1;
        pending.enqueue(version.id);
    }
    while (!pending.isEmpty()) {
        check_cancelled(stop);
        const quint32 parent = pending.dequeue();
        for (quint32 dependent : dependents[parent]) {
            if (tainted[dependent]) continue;
            tainted[dependent] = 1;
            pending.enqueue(dependent);
        }
    }

    for (auto& read : result.reads) {
        read.may_contain_unknown_effects =
            read.observed_memory_version >= static_cast<quint32>(tainted.size())
            || tainted[read.observed_memory_version] != 0;
    }

    if (invalid_reference)
        result.warnings << QStringLiteral(
            "Some memory references or PHI inputs were not represented; "
            "report is incomplete and cannot justify simplification.");
    result.complete = !result.limited && !invalid_reference
        && !program.incomplete;
    if (program.incomplete)
        result.warnings << QStringLiteral(
            "The lifter did not fully model this function; structural "
            "memory graph cannot prove the semantics of unsupported instructions.");
    if (result.opaque_effects_present)
        result.warnings << QStringLiteral(
            "Opaque CALL/instruction effects remain barriers to load "
            "substitution, dead-store elimination and alias assumptions.");

    return result;
}

} // namespace reworkbench::memory_ssa_analysis
