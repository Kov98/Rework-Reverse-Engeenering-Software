#include "reworkbench/control_safety.hpp"

#include <QSet>
#include <algorithm>

namespace reworkbench {
namespace {
constexpr qsizetype max_blocks = 384;
constexpr qsizetype max_edges = 3072;
constexpr qsizetype max_reasons = 12;

bool valid(qsizetype block, qsizetype count) {
    return block >= 0 && block < count;
}
void reason(QStringList& reasons, const QString& value) {
    if (!reasons.contains(value) && reasons.size() < max_reasons)
        reasons << value;
}
QVector<qsizetype> unique(const QVector<qsizetype>& list) {
    QVector<qsizetype> result;
    QSet<qsizetype> used;
    for (qsizetype item : list) {
        if (used.contains(item)) continue;
        used.insert(item);
        result << item;
    }
    std::sort(result.begin(), result.end());
    return result;
}
bool opaque_call(const HirObservation& hir, const HirBlock& block) {
    for (quint32 id : block.ordered_definitions) {
        if (id < static_cast<quint32>(hir.values.size())
            && hir.values[id].operation == QStringLiteral("opaque_call"))
            return true;
    }
    return false;
}
const HirEdgeEffectContract* effect_for(
    const HirEdgeEffectsObservation& effects,
    qsizetype pred, qsizetype succ) {
    for (const auto& effect : effects.edges)
        if (!effect.synthetic_entry
            && effect.predecessor_block == pred
            && effect.successor_block == succ)
            return &effect;
    return nullptr;
}
} // namespace

HirControlSafetyObservation observe_hir_control_safety(
    const HirObservation& hir, const HirEdgeEffectsObservation& effects,
    std::stop_token stop) {
    HirControlSafetyObservation report;
    report.partial = hir.partial || hir.limited || effects.partial
        || effects.limited || !hir.ir_audit.structural_checks_passed;
    report.warnings << QStringLiteral(
        "Only the OBSERVED machine CFG is classified. Unknown indirect,"
        " exceptional, tailcall and nonreturning call behavior is not"
        " converted to verified source-level control.");
    report.warnings << QStringLiteral(
        "Critical edges indicate an edge-local placement problem for"
        " future Phi copies. No edge is split and no rewriting occurs.");

    const qsizetype count = hir.blocks.size();
    if (count == 0 || count > max_blocks) {
        report.limited = true;
        return report;
    }
    quint32 total_expected_edges = 0;
    bool all_matched = true;
    for (qsizetype b = 0; b < count; ++b) {
        check_cancelled(stop);
        const auto& block = hir.blocks[b];
        if (!block.reachable) continue;
        ++report.reachable_blocks;
        HirTerminatorEvidence terminator;
        terminator.block_index = b;
        terminator.observed_kind = block.terminator;
        terminator.observed_successors = unique(block.successors);
        terminator.opaque_call_seen_in_block = opaque_call(hir, block);
        terminator.call_noreturn_unverified =
            terminator.opaque_call_seen_in_block;
        if (terminator.call_noreturn_unverified)
            ++report.potentially_nonreturning_calls;
        const auto degree = terminator.observed_successors.size();
        const bool unresolved = block.target_resolution_incomplete;
        const bool condition = block.terminator == QStringLiteral("conditional");
        const bool go = block.terminator == QStringLiteral("goto");
        const bool ret = block.terminator == QStringLiteral("machine_state_return");
        const bool exit_unknown = block.terminator == QStringLiteral("opaque_exit");
        bool shape = degree == block.successors.size();
        for (qsizetype dest : terminator.observed_successors)
            if (!valid(dest, count) || !hir.blocks[dest].reachable
                || !hir.blocks[dest].predecessors.contains(b))
                shape = false;
        if (condition) {
            terminator.normalized_kind = QStringLiteral("two_way_branch_candidate");
            if (degree != 2 || !block.condition_value
                || !block.resolved_taken_block
                || !block.resolved_not_taken_block
                || *block.resolved_taken_block == *block.resolved_not_taken_block
                || !terminator.observed_successors.contains(*block.resolved_taken_block)
                || !terminator.observed_successors.contains(*block.resolved_not_taken_block))
                shape = false;
        } else if (go) {
            terminator.normalized_kind = QStringLiteral("one_way_transfer_candidate");
            if (degree != 1) shape = false;
            // The lifter may represent fallthrough via taken_address.
            if (block.resolved_taken_block
                && !terminator.observed_successors.contains(*block.resolved_taken_block))
                shape = false;
        } else if (ret) {
            terminator.normalized_kind = QStringLiteral("machine_return_candidate");
            if (degree != 0) shape = false;
        } else if (exit_unknown) {
            terminator.normalized_kind = QStringLiteral("opaque_exit_unknown");
            // Never reinterpret a trap, indirect exit or unknown instruction
            // as a verified return just because no normal successor exists.
            if (degree != 0) shape = false;
        } else {
            terminator.normalized_kind = QStringLiteral("unknown_terminator");
            shape = false;
        }
        terminator.observed_successor_set_consistent = shape && !unresolved;
        // Edge presence by itself is not proof that the terminator has
        // valid operands/targets. Do not report all edges structurally
        // accounted for when the parent terminator is inconsistent.
        if (!terminator.observed_successor_set_consistent)
            all_matched = false;
        terminator.indirect_or_external_target_possible = unresolved
            || exit_unknown || (!shape && (condition || go));
        if (!shape) reason(terminator.blockers,
                           QStringLiteral("observed_cfg_terminator_mismatch"));
        if (unresolved) reason(terminator.blockers,
                               QStringLiteral("unresolved_control_target"));
        if (terminator.call_noreturn_unverified)
            reason(terminator.blockers,
                   QStringLiteral("call_may_not_return_unverified"));
        if (terminator.indirect_or_external_target_possible)
            reason(terminator.blockers,
                   QStringLiteral("indirect_or_external_control_unknown"));
        reason(terminator.blockers,
               QStringLiteral("exceptional_control_unmodeled"));
        if (!shape || unresolved || exit_unknown
            || terminator.call_noreturn_unverified)
            ++report.unresolved_terminators;
        report.terminators << std::move(terminator);
        for (qsizetype succ : report.terminators.last().observed_successors) {
            check_cancelled(stop);
            if (++total_expected_edges > max_edges) {
                report.limited = true;
                all_matched = false;
                break;
            }
            HirControlEdgeEvidence edge;
            edge.predecessor_block = b;
            edge.successor_block = succ;
            edge.cfg_reciprocal_edge = valid(succ, count)
                && hir.blocks[succ].reachable
                && hir.blocks[succ].predecessors.contains(b);
            const auto& from = hir.blocks[b];
            if (condition && from.resolved_taken_block
                && succ == *from.resolved_taken_block)
                edge.branch_arm = QStringLiteral("taken");
            else if (condition && from.resolved_not_taken_block
                && succ == *from.resolved_not_taken_block)
                edge.branch_arm = QStringLiteral("not_taken");
            else if (go) edge.branch_arm = QStringLiteral("unconditional");

            const qsizetype incoming_degree = valid(succ, count)
                ? unique(hir.blocks[succ].predecessors).size() : 0;
            edge.critical_edge_in_observed_cfg =
                degree > 1 && incoming_degree > 1;
            if (edge.critical_edge_in_observed_cfg)
                ++report.critical_edges;
            const auto* effect = effect_for(effects, b, succ);
            edge.effect_contract_found = effect != nullptr;
            edge.effect_contract_unresolved = !effect
                || effect->exceptional_control_unknown
                || !effect->blockers.isEmpty()
                || !effect->phi_schedule_accounted_for;
            edge.needs_edge_split_if_lowered =
                edge.critical_edge_in_observed_cfg
                && (!effect || !effect->captures.isEmpty()
                    || !effect->applications.isEmpty());
            if (edge.needs_edge_split_if_lowered)
                ++report.edges_needing_splitting;
            if (!effect) ++report.missing_effect_contracts;
            if (!edge.cfg_reciprocal_edge) {
                reason(edge.blockers, QStringLiteral("broken_reciprocal_cfg_edge"));
                all_matched = false;
            }
            if (!report.terminators.last().observed_successor_set_consistent)
                reason(edge.blockers,
                       QStringLiteral("terminator_semantics_unproven"));
            if (edge.needs_edge_split_if_lowered)
                reason(edge.blockers,
                       QStringLiteral("critical_edge_split_not_implemented"));
            if (edge.effect_contract_unresolved)
                reason(edge.blockers,
                       QStringLiteral("effect_contract_unresolved"));
            reason(edge.blockers,
                   QStringLiteral("exceptional_successors_unverified"));
            report.edges << std::move(edge);
        }
        if (report.limited) break;
    }
    report.all_observed_edges_accounted_for =
        !report.limited && all_matched
        && report.edges.size() == total_expected_edges
        && report.missing_effect_contracts == 0
        && effects.all_edges_accounted_for;
    // Observations are useful for safety gating, NEVER proof of total CFG
    // coverage or no-return/exceptional semantics.
    report.partial |= !report.all_observed_edges_accounted_for
        || report.unresolved_terminators != 0
        || report.missing_effect_contracts != 0;
    if (report.limited)
        report.warnings << QStringLiteral(
            "Bounded CFG reporting truncated; absent edges are UNKNOWN.");
    return report;
}

} // namespace reworkbench
