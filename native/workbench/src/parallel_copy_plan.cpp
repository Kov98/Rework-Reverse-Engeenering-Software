#include "reworkbench/parallel_copy_plan.hpp"

#include <QHash>
#include <QSet>

#include <limits>
#include <utility>

namespace reworkbench {
namespace {

constexpr qsizetype max_edges = 3072;
constexpr quint32 max_steps = 100000;
constexpr qsizetype max_blockers = 12;

void add_blocker(HirParallelCopyEdgePlan& edge, const QString& reason) {
    if (edge.blockers.size() < max_blockers
        && !edge.blockers.contains(reason))
        edge.blockers << reason;
}

HirParallelCopyStep step(const QString& phase, const QString& operation,
                         const HirPhiEdgeAssignment& assignment,
                         std::optional<quint32> temporary = std::nullopt) {
    HirParallelCopyStep item;
    item.phase = phase;
    item.operation = operation;
    item.source_value_id = assignment.source_value_id;
    item.target_phi_id = assignment.target_phi_id;
    item.virtual_temp_id = temporary;
    item.representation = assignment.representation;
    item.width_bits = assignment.width_bits;
    return item;
}

} // namespace

HirParallelCopyPlan plan_parallel_phi_copies(
    const HirPhiTransferObservation& transfers, std::stop_token stop) {
    HirParallelCopyPlan report;
    report.expected_edge_count = static_cast<quint32>(
        transfers.edges.size());
    report.partial = transfers.graph_partial || transfers.limited
        || !transfers.all_edges_covered
        || !transfers.all_assignments_well_formed;
    report.warnings << QStringLiteral(
        "Virtual snapshots refer to already-modeled SSA values at the "
        "predecessor edge; no instruction evaluation or side effect is "
        "performed by this plan.");
    report.warnings << QStringLiteral(
        "Every scalar source is captured before any planned scalar write. "
        "Memory/architecture tokens remain symbolic, not C variables.");
    report.warnings << QStringLiteral(
        "A structurally valid plan is NOT authorization to mutate SSA/CFG "
        "or generate C/C++; proof of semantics and regression are pending.");

    if (transfers.edges.size() > max_edges) {
        report.limited = true;
        report.warnings << QStringLiteral(
            "More edges than the planner budget; all edges remain "
            "in the original Phi observation.");
        return report;
    }

    quint32 next_temp = 1;
    quint32 steps_used = 0;
    bool all_consistent = true;

    for (const auto& transfer : transfers.edges) {
        check_cancelled(stop);
        // A failed/limited plan cannot be partially emitted as if complete.
        const auto assignment_count = static_cast<quint32>(
            transfer.simultaneous_assignments.size());
        const quint64 worst_case_steps =
            quint64(assignment_count) * 2;
        if (quint64(steps_used) + worst_case_steps > max_steps
            || quint64(next_temp) + assignment_count
                > std::numeric_limits<quint32>::max()) {
            report.limited = true;
            report.partial = true;
            report.warnings << QStringLiteral(
                "Virtual step/temporary budget reached; remaining "
                "edge plans are absent, not proven unnecessary.");
            break;
        }

        HirParallelCopyEdgePlan out;
        out.predecessor_block = transfer.predecessor_block;
        out.successor_block = transfer.successor_block;
        out.synthetic_entry = transfer.synthetic_entry;
        out.contains_dependency_cycle = transfer.has_assignment_cycle;
        out.has_parallel_dependencies = transfer.has_parallel_dependency;
        out.has_opaque_barrier = transfer.touches_opaque_state;

        // A source may feed multiple targets. Take one virtual snapshot
        // per distinct scalar source, avoiding unnecessary temporaries.
        // Captures precede ALL commit steps, including on cyclic edges.
        QHash<quint32, quint32> snapshot_by_source;
        QVector<HirParallelCopyStep> snapshots;
        QVector<HirParallelCopyStep> commits;
        snapshots.reserve(transfer.simultaneous_assignments.size());
        commits.reserve(transfer.simultaneous_assignments.size());
        QSet<quint32> targets;

        for (const auto& a : transfer.simultaneous_assignments) {
            if (targets.contains(a.target_phi_id))
                add_blocker(out, QStringLiteral("duplicate_target_phi"));
            targets.insert(a.target_phi_id);

            if (a.is_machine_state_token
                || a.representation != QStringLiteral("bitvector")) {
                commits << step(QStringLiteral("symbolic_state"),
                                QStringLiteral("preserve_state_dependency"), a);
                ++out.symbolic_state_transfers;
                out.has_opaque_barrier |= a.sourced_from_opaque_projection;
                continue;
            }
            if (a.sourced_from_opaque_projection) {
                out.has_opaque_barrier = true;
                add_blocker(out, QStringLiteral("opaque_scalar_source"));
            }
            if (a.identical_value) {
                commits << step(QStringLiteral("commit"),
                                QStringLiteral("identity_noop"), a);
                ++out.identity_transfers;
                continue;
            }

            auto found = snapshot_by_source.constFind(a.source_value_id);
            quint32 temp = 0;
            if (found != snapshot_by_source.cend()) {
                temp = *found;
            } else {
                temp = next_temp++;
                snapshot_by_source.insert(a.source_value_id, temp);
                snapshots << step(QStringLiteral("snapshot"),
                                  QStringLiteral("capture_scalar"), a, temp);
                ++out.captured_sources;
            }
            commits << step(QStringLiteral("commit"),
                            QStringLiteral("write_from_snapshot"), a, temp);
            ++out.scalar_writes_planned;
        }

        // Validate the recipe with local symbolic conditions only.
        // This is NOT a test of an actual executable or IR rewrite.
        QSet<quint32> captured_temps;
        for (const auto& capture : snapshots) {
            if (!capture.virtual_temp_id
                || captured_temps.contains(*capture.virtual_temp_id))
                add_blocker(out, QStringLiteral("snapshot_temp_not_unique"));
            else captured_temps.insert(*capture.virtual_temp_id);
        }
        for (const auto& commit : commits) {
            if (commit.operation == QStringLiteral("write_from_snapshot")
                && (!commit.virtual_temp_id
                    || !captured_temps.contains(*commit.virtual_temp_id)))
                add_blocker(out, QStringLiteral("uncaptured_scalar_source"));
        }

        out.assignments_accounted_for = transfer.assignments_complete
            && targets.size() == transfer.simultaneous_assignments.size()
            && commits.size() == transfer.simultaneous_assignments.size();
        if (!out.assignments_accounted_for)
            add_blocker(out, QStringLiteral("phi_assignments_incomplete"));
        if (!transfers.ir_static_audit_passed)
            add_blocker(out, QStringLiteral("machine_ir_audit_not_passed"));
        if (!transfer.blockers.isEmpty())
            add_blocker(out, QStringLiteral("phi_transfer_has_blockers"));
        if (out.symbolic_state_transfers)
            add_blocker(out, QStringLiteral("state_tokens_not_lowered"));
        if (out.has_opaque_barrier)
            add_blocker(out, QStringLiteral("opaque_effect_requires_barrier"));
        if (out.contains_dependency_cycle) {
            // Captures handle cycles as a symbolic schedule, but without
            // an actual lowering implementation this remains unverified.
            add_blocker(out, QStringLiteral("cycle_lowering_not_executed"));
        }

        // Capture phase is deliberately atomic relative to future commits.
        out.steps.reserve(snapshots.size() + commits.size());
        for (auto& capture : snapshots) out.steps << std::move(capture);
        for (auto& commit : commits) out.steps << std::move(commit);

        bool seen_commit_phase = false;
        bool ordered = true;
        for (const auto& action : out.steps) {
            if (action.phase == QStringLiteral("snapshot")) {
                if (seen_commit_phase) ordered = false;
            } else {
                seen_commit_phase = true;
            }
        }
        if (!ordered)
            add_blocker(out, QStringLiteral("snapshot_after_commit"));

        out.schedule_structurally_consistent =
            out.assignments_accounted_for && ordered
            && !out.blockers.contains(QStringLiteral("snapshot_temp_not_unique"))
            && !out.blockers.contains(QStringLiteral("uncaptured_scalar_source"));
        all_consistent &= out.schedule_structurally_consistent;
        if (!out.blockers.isEmpty()) ++report.blocked_edges;

        steps_used += static_cast<quint32>(out.steps.size());
        report.total_virtual_temporaries += out.captured_sources;
        report.total_scalar_writes_planned += out.scalar_writes_planned;
        report.total_symbolic_state_transfers += out.symbolic_state_transfers;
        if (out.contains_dependency_cycle) ++report.total_edges_with_cycles;
        report.edges << std::move(out);
    }

    report.all_edges_accounted_for = !report.limited
        && report.edges.size() == transfers.edges.size()
        && transfers.all_edges_covered;
    report.all_schedules_structurally_consistent =
        all_consistent && report.all_edges_accounted_for;
    report.partial |= report.limited || !report.all_edges_accounted_for
        || !report.all_schedules_structurally_consistent;
    report.rewrite_authorized = false;
    report.lowering_executed = false;
    if (report.limited)
        report.warnings << QStringLiteral(
            "Budget truncated this plan. No missing edge or step may "
            "be interpreted as safe to eliminate.");
    return report;
}

} // namespace reworkbench
