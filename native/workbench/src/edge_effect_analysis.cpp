#include "reworkbench/edge_effects.hpp"

#include <QSet>

namespace reworkbench {
namespace {
constexpr qsizetype max_edges = 3072;
constexpr qsizetype max_effect_samples = 32;
constexpr qsizetype max_actions_per_edge = 2048;
constexpr qsizetype max_blockers = 16;

bool valid(qsizetype index, qsizetype count) {
    return index >= 0 && index < count;
}

void block(HirEdgeEffectContract& edge, const QString& why) {
    if (!edge.blockers.contains(why) && edge.blockers.size() < max_blockers)
        edge.blockers << why;
}

bool is_effect(const HirValue& value) {
    return value.effect_kind == QStringLiteral("memory_read_may_fault")
        || value.effect_kind == QStringLiteral("memory_write_may_fault")
        || value.effect_kind == QStringLiteral("unknown_machine_state_barrier")
        || value.effect_kind == QStringLiteral("unknown_call_state_barrier")
        || value.effect_kind == QStringLiteral("opaque_state_projection")
        || value.effect_kind == QStringLiteral("unknown_value");
}

HirEdgeBoundaryAction action(const HirParallelCopyStep& step,
                             const QString& location, const QString& kind) {
    HirEdgeBoundaryAction out;
    out.location = location;
    out.kind = kind;
    out.source_value_id = step.source_value_id;
    out.target_phi_id = step.target_phi_id;
    out.virtual_temp_id = step.virtual_temp_id;
    out.representation = step.representation;
    out.width_bits = step.width_bits;
    return out;
}
} // namespace

HirEdgeEffectsObservation observe_hir_edge_effects(
    const HirObservation& hir, const HirParallelCopyPlan& copies,
    std::stop_token stop) {
    HirEdgeEffectsObservation report;
    report.expected_edges = copies.expected_edge_count;
    report.partial = hir.partial || hir.limited || copies.partial
        || copies.limited || !copies.all_edges_accounted_for
        || !hir.ir_audit.structural_checks_passed;
    report.warnings << QStringLiteral(
        "Predecessor-exit captures and successor-entry transfers are "
        "symbolic location contracts only. They do not execute machine IR.");
    report.warnings << QStringLiteral(
        "Modeled effects are listed in their original predecessor block "
        "order. Exceptions, indirect targets, call side effects and "
        "edge splitting are not proved safe.");
    report.warnings << QStringLiteral(
        "Machine-state Phi dependencies are never translated into "
        "unverified source-language assignments.");

    if (hir.blocks.isEmpty() || hir.blocks.size() > 384
        || hir.values.size() > 65536
        || copies.edges.size() > max_edges) {
        report.limited = true;
        report.partial = true;
        report.warnings << QStringLiteral(
            "Edge-contract input exceeds bounded analysis limits.");
        return report;
    }

    for (const auto& planned : copies.edges) {
        check_cancelled(stop);
        if (planned.steps.size() > max_actions_per_edge) {
            report.limited = true;
            report.partial = true;
            report.warnings << QStringLiteral(
                "An edge has more symbolic actions than the safe cap.");
            break;
        }

        HirEdgeEffectContract edge;
        edge.predecessor_block = planned.predecessor_block;
        edge.successor_block = planned.successor_block;
        edge.synthetic_entry = planned.synthetic_entry;
        edge.capture_boundary = planned.synthetic_entry
            ? QStringLiteral("synthetic_function_entry_before_phi_merge")
            : QStringLiteral("predecessor_exit_after_modeled_effects");
        edge.phi_schedule_accounted_for =
            planned.assignments_accounted_for
            && planned.schedule_structurally_consistent;

        const bool valid_successor =
            valid(edge.successor_block, hir.blocks.size())
            && hir.blocks[edge.successor_block].reachable;
        if (!valid_successor)
            block(edge, QStringLiteral("invalid_or_unreachable_successor"));

        if (edge.synthetic_entry) {
            edge.cfg_edge_observed =
                edge.predecessor_block == -1
                && edge.successor_block == hir.entry_block;
            edge.predecessor_terminator =
                QStringLiteral("synthetic_entry");
        } else if (valid(edge.predecessor_block, hir.blocks.size())) {
            const HirBlock& pred = hir.blocks[edge.predecessor_block];
            edge.predecessor_terminator = pred.terminator;
            edge.cfg_edge_observed = valid_successor && pred.reachable
                && pred.successors.contains(edge.successor_block)
                && hir.blocks[edge.successor_block].predecessors.contains(
                    edge.predecessor_block);
            if (pred.target_resolution_incomplete
                || pred.terminator == QStringLiteral("opaque_exit")
                || pred.terminator == QStringLiteral("none"))
                block(edge, QStringLiteral("unresolved_predecessor_control"));
            if (pred.terminator == QStringLiteral("machine_state_return"))
                block(edge, QStringLiteral("return_cannot_transfer_to_successor"));

            for (quint32 ir_id : pred.ordered_definitions) {
                if (ir_id >= static_cast<quint32>(hir.values.size())) {
                    block(edge, QStringLiteral("invalid_predecessor_ir_id"));
                    continue;
                }
                const HirValue& v = hir.values[ir_id];
                if (v.owner_block != edge.predecessor_block) {
                    block(edge, QStringLiteral("effect_definition_wrong_owner"));
                    continue;
                }
                if (!is_effect(v)) continue;
                ++edge.predecessor_effect_count;
                if (edge.predecessor_effect_ir_ids.size()
                    < max_effect_samples)
                    edge.predecessor_effect_ir_ids << ir_id;
                else edge.effect_samples_truncated = true;

                if (v.effect_kind == QStringLiteral(
                        "unknown_call_state_barrier")
                    || v.effect_kind == QStringLiteral(
                        "unknown_machine_state_barrier")
                    || v.effect_kind == QStringLiteral(
                        "opaque_state_projection")
                    || v.effect_kind == QStringLiteral("unknown_value"))
                    edge.predecessor_has_opaque_effects = true;
                if (v.effect_kind == QStringLiteral(
                        "memory_read_may_fault"))
                    edge.predecessor_has_potentially_faulting_load = true;
            }
        } else {
            block(edge, QStringLiteral("invalid_predecessor_block"));
        }
        if (!edge.cfg_edge_observed)
            block(edge, QStringLiteral("cfg_edge_not_verified"));
        if (!edge.phi_schedule_accounted_for)
            block(edge, QStringLiteral("phi_copy_schedule_incomplete"));
        if (planned.has_opaque_barrier
            || edge.predecessor_has_opaque_effects)
            block(edge, QStringLiteral("opaque_effects_not_modeled"));
        if (edge.predecessor_has_potentially_faulting_load)
            block(edge, QStringLiteral("faulting_load_semantics_unknown"));
        if (planned.contains_dependency_cycle)
            block(edge, QStringLiteral("parallel_cycle_requires_verified_lowering"));
        if (planned.symbolic_state_transfers)
            block(edge, QStringLiteral("state_token_edge_semantics_unverified"));
        if (!hir.ir_audit.structural_checks_passed)
            block(edge, QStringLiteral("ssa_ir_static_audit_failed"));
        if (!planned.blockers.isEmpty())
            block(edge, QStringLiteral("parallel_copy_plan_has_blockers"));

        // Phase A: scalar sources are snapshots on the selected outgoing
        // edge, not operations moved ahead of the predecessor's effects.
        // Phase B: merge values at successor entry, before its non-Phi
        // definitions. These are LOCATIONS, not executable statements.
        bool commits_started = false;
        bool phase_ok = true;
        QSet<quint32> captured_temporaries;
        for (const auto& item : planned.steps) {
            if (item.phase == QStringLiteral("snapshot")) {
                if (commits_started) {
                    phase_ok = false;
                    block(edge, QStringLiteral("snapshot_after_application"));
                }
                if (!item.virtual_temp_id
                    || captured_temporaries.contains(*item.virtual_temp_id)) {
                    phase_ok = false;
                    block(edge, QStringLiteral("missing_or_duplicate_snapshot_temp"));
                } else captured_temporaries.insert(*item.virtual_temp_id);
                edge.captures << action(item, edge.capture_boundary,
                                        QStringLiteral("capture_scalar_reference"));
                ++report.total_capture_actions;
            } else if (item.phase == QStringLiteral("commit")) {
                commits_started = true;
                if (item.operation == QStringLiteral("write_from_snapshot")
                    && (!item.virtual_temp_id
                        || !captured_temporaries.contains(*item.virtual_temp_id))) {
                    phase_ok = false;
                    block(edge, QStringLiteral("application_without_prior_capture"));
                }
                edge.applications << action(
                    item, edge.apply_boundary,
                    item.operation == QStringLiteral("identity_noop")
                        ? QStringLiteral("identity_phi_value")
                        : QStringLiteral("apply_virtual_snapshot"));
                ++report.total_apply_actions;
            } else if (item.phase == QStringLiteral("symbolic_state")) {
                commits_started = true;
                edge.applications << action(
                    item, QStringLiteral("successor_entry_symbolic_state"),
                    QStringLiteral("preserve_machine_state_dependency"));
                ++report.total_apply_actions;
            } else {
                phase_ok = false;
                block(edge, QStringLiteral("unknown_phi_copy_phase"));
            }
        }

        if (!phase_ok) edge.phi_schedule_accounted_for = false;
        if (edge.predecessor_has_opaque_effects)
            ++report.edges_with_opaque_effects;

        // Unknown edge/exception semantics are intentionally not
        // guessed even if the CFG edge and symbolic schedule look sane.
        // This is a known proof gap, not evidence that exceptional
        // transfers or potentially faulting actions cannot happen.
        edge.exceptional_control_unknown = true;
        block(edge, QStringLiteral("exceptional_control_semantics_unverified"));
        edge.effect_order_preservation_proven = false;
        edge.rewrite_authorized = false;
        edge.lowering_executed = false;
        if (!edge.blockers.isEmpty()) ++report.unresolved_edges;
        report.edges << std::move(edge);
    }

    report.all_edges_accounted_for =
        !report.limited
        && report.edges.size() == copies.edges.size()
        && copies.all_edges_accounted_for;
    report.partial |= !report.all_edges_accounted_for
        || report.unresolved_edges != 0;
    if (report.limited)
        report.warnings << QStringLiteral(
            "The contract was truncated; missing edges remain unknown.");
    return report;
}

} // namespace reworkbench
