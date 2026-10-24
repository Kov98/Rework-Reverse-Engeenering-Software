#include "memory_region_analysis.hpp"

#include <QHash>
#include <QSet>

#include <bit>
#include <limits>
#include <optional>

namespace reworkbench::memory_region_analysis {
namespace {
using namespace decompiler_detail;

constexpr qsizetype max_accesses = 384;
constexpr qsizetype max_reported_pairs = 1024;
constexpr qsizetype max_expression_depth = 128;
constexpr qint64 max_stack_displacement = 1024 * 1024;

// A pointer expression is either an exact 64-bit absolute constant, or one
// copy of the function-entry RSP plus a fixed 64-bit modular displacement.
// Register input (other than RSP), loads, opaque operations and casts that
// cannot be proved remain unknown. The base count never exceeds one.
struct Affine {
    int entry_rsp_factor = 0;
    quint64 displacement = 0;
    bool operator==(const Affine&) const = default;
};

class PointerResolver {
public:
    PointerResolver(const Program& program, std::stop_token stop)
        : program_(program), stop_(stop) {}

    std::optional<Affine> resolve(Value value) {
        return visit(value, 0);
    }

private:
    std::optional<Affine> visit(Value id, qsizetype depth) {
        if (id == no_value || id >= static_cast<Value>(program_.nodes.size())
            || depth >= max_expression_depth) return std::nullopt;
        const auto found = cache_.constFind(id);
        if (found != cache_.cend()) return *found;
        if (visiting_.contains(id)) return std::nullopt; // Cyclic PHI.
        if ((cache_.size() & 0x7f) == 0) check_cancelled(stop_);
        visiting_.insert(id);
        std::optional<Affine> answer;
        const Node& n = program_.nodes[id];
        if (n.type.kind == TypeKind::Bits && n.type.bits == 64) {
            switch (n.op) {
            case Op::Input:
                if (n.text == QStringLiteral("rsp")) answer = Affine{1, 0};
                break;
            case Op::Constant:
                answer = Affine{0, n.immediate};
                break;
            case Op::Add:
            case Op::Subtract:
                if (n.operands.size() == 2) {
                    auto a = visit(n.operands[0], depth + 1);
                    auto b = visit(n.operands[1], depth + 1);
                    if (a && b) {
                        const bool subtraction = n.op == Op::Subtract;
                        const int factor = a->entry_rsp_factor
                            + (subtraction ? -b->entry_rsp_factor
                                           : b->entry_rsp_factor);
                        if (factor == 0 || factor == 1) {
                            const quint64 displacement = subtraction
                                ? a->displacement - b->displacement
                                : a->displacement + b->displacement;
                            if (factor == 0
                                || (std::bit_cast<qint64>(displacement)
                                    >= -max_stack_displacement
                                    && std::bit_cast<qint64>(displacement)
                                    <= max_stack_displacement))
                                answer = Affine{factor, displacement};
                        }
                    }
                }
                break;
            case Op::Phi:
                if (!n.incoming.isEmpty() && n.incoming.size() <= 64) {
                    bool consistent = true;
                    for (const auto& incoming : n.incoming) {
                        const auto candidate = visit(incoming.value, depth + 1);
                        if (!candidate || (answer && *answer != *candidate)) {
                            consistent = false;
                            break;
                        }
                        answer = candidate;
                    }
                    if (!consistent) answer.reset();
                }
                break;
            default: break;
            }
        }
        visiting_.remove(id);
        cache_.insert(id, answer);
        return answer;
    }

    const Program& program_;
    std::stop_token stop_;
    QHash<Value, std::optional<Affine>> cache_;
    QSet<Value> visiting_;
};

bool valid_width(int bits) {
    return bits == 8 || bits == 16 || bits == 32 || bits == 64;
}

bool interval_in_image(const BinaryMetadata& metadata, quint64 address,
                       quint32 bytes, QString& name) {
    for (const auto& region : metadata.regions) {
        if (region.start.space != QStringLiteral("ram")
            || address < region.start.value) continue;
        const quint64 relative = address - region.start.value;
        if (relative <= region.virtual_size
            && quint64(bytes) <= region.virtual_size - relative) {
            name = region.name;
            return true;
        }
    }
    return false;
}

enum class Relation { MustOverlap, ProvenDisjoint, MayAlias };

Relation compare(const MemoryAddressFact& a, const MemoryAddressFact& b) {
    if (a.width_bits <= 0 || b.width_bits <= 0) return Relation::MayAlias;
    // Explicit address expressions with the SAME symbolic base can be
    // compared exactly. Cross-base (e.g. RSP vs absolute constant) remains
    // MAY, because no disjoint allocation proof exists at this stage.
    if (a.entry_rsp_offset_bytes && b.entry_rsp_offset_bytes) {
        const qint64 x = *a.entry_rsp_offset_bytes;
        const qint64 y = *b.entry_rsp_offset_bytes;
        const qint64 x_end = x + a.width_bits / 8;
        const qint64 y_end = y + b.width_bits / 8;
        return (x < y_end && y < x_end)
            ? Relation::MustOverlap : Relation::ProvenDisjoint;
    }
    if (a.absolute_address && b.absolute_address) {
        const auto x = *a.absolute_address;
        const auto y = *b.absolute_address;
        const auto max = std::numeric_limits<quint64>::max();
        const quint64 x_size = quint64(a.width_bits / 8);
        const quint64 y_size = quint64(b.width_bits / 8);
        // A wrapping address range cannot justify disjointness.
        if (x > max - x_size || y > max - y_size)
            return Relation::MayAlias;
        return (x < y + y_size && y < x + x_size)
            ? Relation::MustOverlap : Relation::ProvenDisjoint;
    }
    return Relation::MayAlias;
}

} // namespace

MemoryRegionObservation observe(const Program& program,
                                const BinaryMetadata& metadata,
                                std::stop_token stop) {
    MemoryRegionObservation report;
    report.warnings << QStringLiteral(
        "Regions are address-expression facts, not heap ownership, "
        "lifetime or source-language variable types. No load/store "
        "optimization is permitted from this diagnostic.");

    if (program.architecture != Architecture::X64
        || program.register_width != 64) {
        report.warnings << QStringLiteral("Region analysis currently requires x86-64 IR.");
        return report;
    }
    PointerResolver resolver(program, stop);

    for (qsizetype i = 0; i < program.nodes.size(); ++i) {
        if ((i & 0x7f) == 0) check_cancelled(stop);
        const Node& node = program.nodes[i];
        if (node.block < 0 || node.block >= program.blocks.size()
            || !program.blocks[node.block].reachable) continue;
        if (node.op == Op::Opaque || node.op == Op::CallOpaque)
            report.opaque_effects_present = true;
        const bool is_load = node.op == Op::Load;
        const bool is_store = node.op == Op::Store;
        if (!is_load && !is_store) continue;
        if (report.accesses.size() >= max_accesses) {
            report.limited = true;
            break;
        }
        MemoryAddressFact fact;
        fact.access_ir_node = static_cast<quint32>(i);
        fact.instruction = node.address;
        fact.block = node.block;
        fact.write = is_store;
        fact.region = QStringLiteral("unknown");
        fact.width_bits = is_load ? node.type.bits : 0;
        if (is_store && node.operands.size() == 3) {
            Value stored = node.operands[2];
            if (stored < static_cast<Value>(program.nodes.size()))
                fact.width_bits = program.nodes[stored].type.bits;
        }
        if (node.operands.size() >= 2)
            fact.pointer_ir_node = node.operands[1];
        else fact.pointer_ir_node = no_value;

        if (valid_width(fact.width_bits) && node.operands.size() == (is_load ? 2 : 3)) {
            const auto address = resolver.resolve(fact.pointer_ir_node);
            if (address) {
                if (address->entry_rsp_factor == 1) {
                    fact.region = QStringLiteral("stack_relative");
                    fact.entry_rsp_offset_bytes =
                        std::bit_cast<qint64>(address->displacement);
                } else {
                    fact.absolute_address = address->displacement;
                    fact.region = QStringLiteral("absolute_candidate");
                    if (interval_in_image(metadata, address->displacement,
                                          static_cast<quint32>(fact.width_bits / 8),
                                          fact.mapped_image_region))
                        fact.region = QStringLiteral("mapped_image_candidate");
                }
            }
        }
        if (fact.region == QStringLiteral("unknown"))
            ++report.unknown_pointer_accesses;
        report.accesses << std::move(fact);
    }

    for (qsizetype i = 0; i < report.accesses.size(); ++i) {
        if ((i & 0x3f) == 0) check_cancelled(stop);
        for (qsizetype j = i + 1; j < report.accesses.size(); ++j) {
            const Relation relation = compare(report.accesses[i], report.accesses[j]);
            if (relation == Relation::MustOverlap) ++report.must_overlap_pairs;
            else if (relation == Relation::ProvenDisjoint)
                ++report.proven_disjoint_pairs;
            else ++report.may_alias_pairs;

            // Detailed disjoint pairs would overwhelm output; preserve the
            // count, but provide evidence for overlapping/uncertain pairs.
            if (relation == Relation::ProvenDisjoint) continue;
            if (report.noteworthy_pairs.size() >= max_reported_pairs) {
                report.limited = true;
                continue;
            }
            MemoryAliasPair pair;
            pair.first_access_ir_node = report.accesses[i].access_ir_node;
            pair.second_access_ir_node = report.accesses[j].access_ir_node;
            pair.relation = relation == Relation::MustOverlap
                ? QStringLiteral("must_overlap_address_ranges")
                : QStringLiteral("may_alias");
            pair.reason = relation == Relation::MustOverlap
                ? QStringLiteral("same_symbolic_address_base_and_intersecting_ranges")
                : QStringLiteral("unknown_pointer_or_unproved_cross_base_disjointness");
            report.noteworthy_pairs << std::move(pair);
        }
    }

    // "complete" means the bounded observation did not truncate, *not*
    // full or sound alias analysis of all machine-level memory effects.
    report.complete = !report.limited && !program.incomplete
        && report.unknown_pointer_accesses == 0
        && !report.opaque_effects_present;
    report.warnings << QStringLiteral(
        "Heap pointers are not inferred from unknown addresses. Unknown "
        "pointer expressions can refer to stack, heap, globals or external "
        "mappings. Cross-base disjointness is never assumed.");
    if (report.opaque_effects_present)
        report.warnings << QStringLiteral(
            "Opaque instructions and calls remain whole-memory barriers.");
    if (report.limited)
        report.warnings << QStringLiteral(
            "Access/pair reporting reached a configured ceiling.");
    return report;
}

} // namespace reworkbench::memory_region_analysis
