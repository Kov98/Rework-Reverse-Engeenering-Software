#include "reworkbench/memory_dependence.hpp"

#include <algorithm>

namespace reworkbench {
namespace {

bool overlap(const StackSlotCandidate& left, const StackSlotCandidate& right) {
    const qint64 left_end = left.entry_rsp_offset_bytes + qint64(left.size_bytes);
    const qint64 right_end = right.entry_rsp_offset_bytes + qint64(right.size_bytes);
    return left.entry_rsp_offset_bytes < right_end
        && right.entry_rsp_offset_bytes < left_end;
}

bool same_range(const StackSlotCandidate& a, const StackSlotCandidate& b) {
    return a.entry_rsp_offset_bytes == b.entry_rsp_offset_bytes
        && a.size_bytes == b.size_bytes;
}

} // namespace

MemoryDependenceObservation summarize_memory_dependences(
    const StackFrameObservation& frame, const StackValueFlowObservation& flow) {
    MemoryDependenceObservation result;
    result.unknown_memory_effects = frame.partial
        || frame.unknown_machine_effects_present
        || frame.unresolved_memory_accesses != 0
        || flow.has_unknown_aliases;

    constexpr qsizetype max_alias_pairs = 4096;
    constexpr qsizetype max_uses = 512;
    for (qsizetype i = 0; i < frame.slots.size(); ++i) {
        const auto& left = frame.slots[i];
        for (qsizetype j = i + 1; j < frame.slots.size(); ++j) {
            const auto& right = frame.slots[j];
            if (!overlap(left, right)) {
                ++result.proven_disjoint_range_pairs;
                continue;
            }
            if (result.overlap_pairs.size() >= max_alias_pairs) {
                result.limited = true;
                break;
            }
            StackAliasPair pair;
            pair.first_offset_bytes = left.entry_rsp_offset_bytes;
            pair.first_size_bytes = left.size_bytes;
            pair.second_offset_bytes = right.entry_rsp_offset_bytes;
            pair.second_size_bytes = right.size_bytes;
            pair.relation = same_range(left, right)
                ? QStringLiteral("exact_range")
                : QStringLiteral("partial_overlap");
            result.overlap_pairs << pair;
        }
        if (result.limited) break;
    }

    for (const auto& read : flow.read_relations) {
        if (result.uses.size() >= max_uses) {
            result.limited = true;
            break;
        }
        MemoryUseCandidate use;
        use.instruction = read.read_instruction;
        use.read_ir_node = read.read_ir_node;
        use.entry_rsp_offset_bytes = read.entry_rsp_offset_bytes;
        use.size_bytes = read.size_bytes;
        use.possible_defining_store_nodes = read.possible_source_write_nodes;
        std::sort(use.possible_defining_store_nodes.begin(),
                  use.possible_defining_store_nodes.end());
        use.possible_defining_store_nodes.erase(
            std::unique(use.possible_defining_store_nodes.begin(),
                        use.possible_defining_store_nodes.end()),
            use.possible_defining_store_nodes.end());
        use.may_use_entry_memory = read.may_read_entry_memory;
        use.may_depend_on_unknown_write = read.unknown_memory_effect_may_intervene;
        use.has_partial_alias = read.ambiguous_overlapping_write;
        use.incomplete_producers = read.reporting_limited;
        use.needs_memory_phi =
            use.possible_defining_store_nodes.size() > 1
            || (use.may_use_entry_memory && !use.possible_defining_store_nodes.isEmpty())
            || (use.may_depend_on_unknown_write
                && (!use.possible_defining_store_nodes.isEmpty()
                    || use.may_use_entry_memory));
        result.uses << std::move(use);
    }

    result.complete = !result.limited && !frame.limited
        && !frame.partial && flow.dataflow_converged && !flow.limited
        && !result.unknown_memory_effects;

    result.warnings << QStringLiteral(
        "This is a stack-scoped memory-dependence preview, not global MemorySSA. "
        "Multiple possible definitions are advisory; no load substitution "
        "or type/variable recovery is authorized.");
    if (result.unknown_memory_effects)
        result.warnings << QStringLiteral(
            "Opaque calls, unknown-address memory accesses or partial IR may "
            "intervene; disjointness applies only to known stack ranges.");
    if (result.limited)
        result.warnings << QStringLiteral(
            "Memory alias/use output capped; results are incomplete.");
    return result;
}

} // namespace reworkbench
