#include "ir_semantic_audit.hpp"

#include <QSet>

#include <algorithm>
#include <utility>

namespace reworkbench::ir_semantic_audit {
namespace {
using namespace decompiler_detail;

constexpr qsizetype max_blocks = 512;
constexpr qsizetype max_nodes = 65536;
constexpr qsizetype max_recorded_issues = 160;
constexpr int max_dominance_passes = 1024;

bool valid_block(qsizetype b, qsizetype count) {
    return b >= 0 && b < count;
}

void issue(IrSemanticAudit& report, const QString& code,
           bool error, quint32 node, qsizetype block, const QString& detail) {
    if (error) ++report.errors;
    else ++report.warnings;
    if (report.issues.size() >= max_recorded_issues) {
        report.issues_truncated = true;
        return;
    }
    report.issues << IrSemanticIssue{
        code, error ? QStringLiteral("error") : QStringLiteral("warning"),
        node, block, detail
    };
}

bool valid_value(const Program& program, Value id) {
    return id != no_value && id < static_cast<Value>(program.nodes.size());
}

bool matches(const Program& program, Value id, Type kind) {
    return valid_value(program, id) && program.nodes[id].type == kind;
}

} // namespace

IrSemanticAudit inspect(const Program& program, std::stop_token stop) {
    IrSemanticAudit report;
    report.ir_incomplete = program.incomplete
        || program.total != program.modeled;
    report.warnings_detail << QStringLiteral(
        "Static SSA/memory checks prove only consistency of the modeled "
        "IR graph, NOT equivalence with the original machine instructions.");
    const qsizetype n = program.blocks.size();
    if (n == 0 || n > max_blocks || program.nodes.size() > max_nodes
        || !valid_block(program.entry, n)) {
        issue(report, QStringLiteral("audit_budget_or_missing_cfg"), true,
              0, -1, QStringLiteral("CFG/IR missing or larger than audit budget."));
        return report;
    }
    report.nodes_examined = static_cast<quint32>(program.nodes.size());
    QSet<qsizetype> reachable;
    for (qsizetype i = 0; i < n; ++i)
        if (program.blocks[i].reachable) reachable.insert(i);

    bool cfg_ok = reachable.contains(program.entry);
    for (qsizetype i = 0; i < n; ++i) {
        check_cancelled(stop);
        const auto& b = program.blocks[i];
        if (!b.reachable) continue;
        if (b.input.size() != program.state_slots.size()
            || b.output.size() != program.state_slots.size()) {
            cfg_ok = false;
            issue(report, QStringLiteral("state_vector_arity"), true, 0, i,
                  QStringLiteral("Block input/output count differs from state slots."));
        }
        for (qsizetype successor : b.successors) {
            if (!valid_block(successor, n)
                || !program.blocks[successor].reachable
                || !program.blocks[successor].predecessors.contains(i)) {
                cfg_ok = false;
                issue(report, QStringLiteral("cfg_successor_mismatch"), true,
                      0, i, QStringLiteral("Successor or reverse predecessor invalid."));
            }
        }
        for (qsizetype predecessor : b.predecessors)
            if (!valid_block(predecessor, n)
                || (program.blocks[predecessor].reachable
                    && !program.blocks[predecessor].successors.contains(i))) {
                cfg_ok = false;
                issue(report, QStringLiteral("cfg_predecessor_mismatch"), true,
                      0, i, QStringLiteral("Predecessor edge invalid."));
            }
    }
    report.cfg_consistent = cfg_ok;

    QVector<QSet<qsizetype>> dom(n);
    for (qsizetype block : reachable) {
        dom[block] = block == program.entry
            ? QSet<qsizetype>{program.entry} : reachable;
    }
    bool converged = false;
    for (int pass = 0; pass < max_dominance_passes; ++pass) {
        check_cancelled(stop);
        bool changed = false;
        for (qsizetype block = 0; block < n; ++block) {
            if (!reachable.contains(block) || block == program.entry) continue;
            const auto& predecessors = program.blocks[block].predecessors;
            QSet<qsizetype> incoming;
            bool started = false;
            for (qsizetype pred : predecessors) {
                if (!reachable.contains(pred)) continue;
                if (!started) {
                    incoming = dom[pred];
                    started = true;
                } else incoming.intersect(dom[pred]);
            }
            if (!started) {
                cfg_ok = false;
                issue(report, QStringLiteral("reachable_orphan_block"), true,
                      0, block, QStringLiteral("Reachable non-entry block lacks reachable predecessors."));
                incoming.clear();
            }
            incoming.insert(block);
            if (incoming != dom[block]) {
                dom[block] = std::move(incoming);
                changed = true;
            }
        }
        if (!changed) {
            converged = true;
            break;
        }
    }
    report.dominance_converged = converged;
    if (!converged)
        issue(report, QStringLiteral("dominance_not_converged"), true,
              0, -1, QStringLiteral("SSA dominance cannot be established."));
    report.cfg_consistent &= cfg_ok;

    bool types_ok = true;
    bool ssa_ok = converged;
    bool mem_ok = true;

    const auto fail_type = [&](quint32 id, const Node& node,
                               const QString& code, const QString& explanation) {
        types_ok = false;
        issue(report, code, true, id, node.block, explanation);
    };
    const auto fail_ssa = [&](quint32 id, const Node& node,
                              const QString& code, const QString& explanation) {
        ssa_ok = false;
        issue(report, code, true, id, node.block, explanation);
    };
    const auto fail_memory = [&](quint32 id, const Node& node,
                                 const QString& code, const QString& explanation) {
        mem_ok = false;
        issue(report, code, true, id, node.block, explanation);
    };

    for (qsizetype i = 0; i < program.nodes.size(); ++i) {
        if ((i & 0xff) == 0) check_cancelled(stop);
        const Node& node = program.nodes[i];
        const auto id = static_cast<quint32>(i);
        if (node.block != -1 && !valid_block(node.block, n)) {
            fail_ssa(id, node, QStringLiteral("invalid_owner_block"),
                     QStringLiteral("IR definition has invalid owning block."));
            continue;
        }
        if (node.block >= 0 && !program.blocks[node.block].reachable) {
            fail_ssa(id, node, QStringLiteral("definition_in_unreachable_block"),
                     QStringLiteral("SSA definition belongs to unreachable block."));
        }
        for (Value operand : node.operands) {
            if (!valid_value(program, operand)) {
                fail_ssa(id, node, QStringLiteral("missing_operand_definition"),
                         QStringLiteral("Operand refers to absent IR value."));
                continue;
            }
            const auto& def = program.nodes[operand];
            if (def.block < 0) continue; // Entry Input definitions.
            if (!converged || !valid_block(node.block, n)
                || !valid_block(def.block, n)) {
                ssa_ok = false;
            } else if (def.block == node.block) {
                if (operand >= id)
                    fail_ssa(id, node, QStringLiteral("same_block_forward_use"),
                             QStringLiteral("Non-PHI operand uses a value not yet defined."));
            } else if (!dom[node.block].contains(def.block)) {
                fail_ssa(id, node, QStringLiteral("non_dominating_definition"),
                         QStringLiteral("Operand definition does not dominate its use."));
            }
        }

        if (node.op == Op::Phi) {
            if (!valid_block(node.block, n)) {
                fail_ssa(id, node, QStringLiteral("orphan_phi"),
                         QStringLiteral("PHI lacks a valid basic block."));
                continue;
            }
            const auto& raw_block = program.blocks[node.block];
            QSet<qsizetype> expected;
            for (qsizetype predecessor : raw_block.predecessors)
                if (reachable.contains(predecessor)) expected.insert(predecessor);
            if (node.block == program.entry) expected.insert(-1);
            QSet<qsizetype> received;
            for (const Incoming& input : node.incoming) {
                ++report.phi_inputs_examined;
                if (!expected.contains(input.predecessor)
                    || received.contains(input.predecessor)
                    || !valid_value(program, input.value)) {
                    fail_ssa(id, node, QStringLiteral("phi_incoming_invalid"),
                             QStringLiteral("PHI has a missing, duplicate or non-CFG predecessor."));
                    continue;
                }
                received.insert(input.predecessor);
                if (program.nodes[input.value].type != node.type)
                    fail_type(id, node, QStringLiteral("phi_type_mismatch"),
                              QStringLiteral("PHI source/result types differ."));
                const qsizetype pred = input.predecessor;
                const auto& source = program.nodes[input.value];
                if (pred == -1) {
                    if (source.block != -1)
                        fail_ssa(id, node, QStringLiteral("phi_entry_source"),
                                 QStringLiteral("Synthetic entry source is not an entry Input."));
                } else if (source.block >= 0
                    && (!converged || !dom[pred].contains(source.block))) {
                    fail_ssa(id, node, QStringLiteral("phi_edge_dominance"),
                             QStringLiteral("PHI input definition does not dominate incoming edge."));
                }
            }
            if (expected != received)
                fail_ssa(id, node, QStringLiteral("phi_missing_predecessor"),
                         QStringLiteral("PHI incoming edge coverage disagrees with reachable CFG."));
        }

        if (node.op == Op::Load || node.op == Op::Store) {
            ++report.memory_operations_examined;
            const bool load = node.op == Op::Load;
            const int arity = load ? 2 : 3;
            if (node.operands.size() != arity
                || !matches(program, node.operands.value(0, no_value),
                            {TypeKind::Memory, 0})
                || !valid_value(program, node.operands.value(1, no_value))
                || program.nodes[node.operands.value(1, no_value)].type.kind != TypeKind::Bits
                || program.nodes[node.operands.value(1, no_value)].type.bits !=
                    program.register_width) {
                fail_memory(id, node, QStringLiteral("memory_operand_contract"),
                            QStringLiteral("Memory token, pointer width or operation arity invalid."));
            }
            if (load) {
                if (node.type.kind != TypeKind::Bits
                    || node.type.bits <= 0 || node.type.bits > 64)
                    fail_memory(id, node, QStringLiteral("load_result_width"),
                                QStringLiteral("Modeled load result not a supported bitvector."));
            } else {
                if (node.type != Type{TypeKind::Memory, 0}
                    || !valid_value(program, node.operands.value(2, no_value))
                    || program.nodes[node.operands.value(2, no_value)].type.kind != TypeKind::Bits)
                    fail_memory(id, node, QStringLiteral("store_value_contract"),
                                QStringLiteral("Store result/value does not fit memory-state contract."));
            }
        }
        if (node.op == Op::Opaque || node.op == Op::CallOpaque) {
            report.opaque_effects_present = true;
            if (node.type.kind != TypeKind::OpaqueState
                || node.operands.size() != program.state_slots.size()) {
                fail_memory(id, node, QStringLiteral("opaque_effect_contract"),
                            QStringLiteral("Opaque effect does not consume the full state vector."));
            } else {
                for (qsizetype slot = 0; slot < program.state_slots.size(); ++slot)
                    if (!matches(program, node.operands[slot],
                                 program.state_slots[slot].type)) {
                        fail_memory(id, node, QStringLiteral("opaque_state_slot_mismatch"),
                                    QStringLiteral("Opaque input state slot has wrong type."));
                        break;
                    }
            }
        }
        if (node.op == Op::StateField) {
            if (node.operands.size() != 1
                || !valid_value(program, node.operands.value(0, no_value))
                || (program.nodes[node.operands.value(0, no_value)].op != Op::Opaque
                    && program.nodes[node.operands.value(0, no_value)].op != Op::CallOpaque)
                || !valid_block(
                    program.slot_index.value(node.text, -1),
                    program.state_slots.size())
                || node.type != program.state_slots[
                    program.slot_index.value(node.text, -1)].type) {
                fail_memory(id, node, QStringLiteral("state_projection_contract"),
                            QStringLiteral("StateField does not project a valid opaque state slot."));
            }
        }
    }

    report.operand_types_consistent = types_ok;
    report.ssa_uses_dominated = ssa_ok && report.cfg_consistent;
    report.memory_tokens_well_formed = mem_ok;
    report.structural_checks_passed = report.errors == 0
        && report.cfg_consistent && report.dominance_converged
        && report.operand_types_consistent && report.ssa_uses_dominated
        && report.memory_tokens_well_formed && !report.ir_incomplete;
    // Structural well-formedness is not equivalent to machine semantics;
    // external proof and differential validation remain mandatory.
    report.semantic_equivalence_proven = false;
    report.rewrite_authorized = false;
    if (report.ir_incomplete)
        report.warnings_detail << QStringLiteral(
            "Function IR is partial or contains opaque semantics. No missing "
            "instruction effect may be assumed absent.");
    if (report.opaque_effects_present)
        report.warnings_detail << QStringLiteral(
            "Opaque call/instruction barriers prohibit unsound memory "
            "and control-flow rewrites.");
    if (!report.structural_checks_passed)
        report.warnings_detail << QStringLiteral(
            "Static IR invariants not all established; preserve raw CFG fallback.");
    return report;
}

} // namespace reworkbench::ir_semantic_audit
