#include "reworkbench/phi_transfers.hpp"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace reworkbench {
namespace {
constexpr qsizetype max_blocks = 384;
constexpr qsizetype max_edges = 3072;
constexpr quint32 max_assignments = 40000;
constexpr qsizetype max_blockers = 12;

bool valid(qsizetype i, qsizetype count) {
    return i >= 0 && i < count;
}
void block(HirPhiEdgeTransfer& edge, const QString& message) {
    if (!edge.blockers.contains(message) && edge.blockers.size() < max_blockers)
        edge.blockers << message;
}

// A dependency is detected when a source is also a destination on the
// SAME edge. If lowering used sequential assignments, earlier writes
// could destroy later sources. A directed cycle requires temporaries.
void find_parallel_hazards(HirPhiEdgeTransfer& edge) {
    QHash<quint32, quint32> source_of_target;
    for (const auto& assignment : edge.simultaneous_assignments)
        if (!assignment.identical_value)
            source_of_target.insert(assignment.target_phi_id,
                                    assignment.source_value_id);
    for (const auto& assignment : edge.simultaneous_assignments) {
        if (assignment.identical_value) continue;
        if (source_of_target.contains(assignment.source_value_id))
            edge.has_parallel_dependency = true;
        QSet<quint32> seen;
        quint32 current = assignment.target_phi_id;
        while (source_of_target.contains(current)) {
            if (seen.contains(current)) {
                edge.has_assignment_cycle = true;
                break;
            }
            seen.insert(current);
            current = source_of_target.value(current);
        }
        if (edge.has_assignment_cycle) break;
    }
}
} // namespace

HirPhiTransferObservation observe_hir_phi_transfers(
    const HirObservation& hir, std::stop_token stop) {
    HirPhiTransferObservation report;
    report.ir_static_audit_passed = hir.ir_audit.structural_checks_passed;
    report.opaque_barriers_present = hir.has_opaque_barriers
        || hir.ir_audit.opaque_effects_present;
    report.graph_partial = hir.partial || hir.limited
        || !report.ir_static_audit_passed;
    report.warnings << QStringLiteral(
        "All Phi inputs are simultaneous EDGE transfers, not sequential "
        "source-level assignments. Memory/architecture tokens are state "
        "dependencies, not confirmed local variables.");
    report.warnings << QStringLiteral(
        "This report does not lower Phi instructions, reorder effects, "
        "alter CFG or authorize rewriting C/C++ source.");

    const qsizetype count = hir.blocks.size();
    if (count == 0 || count > max_blocks
        || !valid(hir.entry_block, count)
        || hir.values.size() > 65536) {
        report.limited = true;
        report.warnings << QStringLiteral(
            "HIR exceeds the bounded edge-analysis capacity, or has no entry.");
        return report;
    }

    QVector<QVector<const HirValue*>> phis(count);
    bool malformed_phi_identity = false;
    for (qsizetype i = 0; i < hir.values.size(); ++i) {
        const auto& value = hir.values[i];
        if (value.operation != QStringLiteral("phi")) continue;
        if (value.id != static_cast<quint32>(i)
            || !valid(value.owner_block, count)
            || !hir.blocks[value.owner_block].reachable) {
            report.graph_partial = true;
            malformed_phi_identity = true;
            continue;
        }
        phis[value.owner_block] << &value;
    }
    if (malformed_phi_identity)
        report.warnings << QStringLiteral(
            "Malformed, unowned or unreachable Phi nodes: the edge "
            "transfer mapping cannot be certified.");

    bool well_formed = !malformed_phi_identity;
    quint32 expected = 0;
    quint32 created = 0;
    for (qsizetype successor = 0; successor < count; ++successor) {
        check_cancelled(stop);
        const auto& destination = hir.blocks[successor];
        if (!destination.reachable) continue;

        QSet<qsizetype> incoming;
        for (qsizetype predecessor : destination.predecessors) {
            if (incoming.contains(predecessor)) {
                well_formed = false;
                continue;
            }
            incoming.insert(predecessor);
        }
        if (successor == hir.entry_block) incoming.insert(-1);
        if (incoming.isEmpty()) {
            well_formed = false;
            report.graph_partial = true;
        }
        QVector<qsizetype> predecessors;
        for (qsizetype predecessor : incoming) predecessors << predecessor;
        std::sort(predecessors.begin(), predecessors.end());

        for (qsizetype predecessor : predecessors) {
            if (predecessor == -1) ++report.synthetic_entry_edges_expected;
            else ++report.reachable_cfg_edges_expected;
            ++expected;
            if (report.edges.size() >= max_edges
                || report.total_assignments + phis[successor].size()
                    > max_assignments) {
                report.limited = true;
                report.graph_partial = true;
                break;
            }
            HirPhiEdgeTransfer edge;
            bool edge_structure_valid = true;
            edge.predecessor_block = predecessor;
            edge.successor_block = successor;
            edge.synthetic_entry = predecessor == -1;
            if (!report.ir_static_audit_passed)
                block(edge, QStringLiteral("ir_static_audit_not_passed"));
            if (hir.partial || hir.limited)
                block(edge, QStringLiteral("hir_partial_or_limited"));
            if (report.opaque_barriers_present)
                block(edge, QStringLiteral("opaque_state_effects_unresolved"));
            if (predecessor == -1) {
                if (successor != hir.entry_block) {
                    block(edge, QStringLiteral("synthetic_edge_not_to_entry"));
                    edge_structure_valid = false;
                }
            } else if (!valid(predecessor, count)
                       || !hir.blocks[predecessor].reachable
                       || !hir.blocks[predecessor].successors.contains(successor)) {
                block(edge, QStringLiteral("predecessor_cfg_edge_unverified"));
                edge_structure_valid = false;
            }
            if (phis[successor].isEmpty()) {
                block(edge, QStringLiteral("destination_has_no_state_phi"));
                edge_structure_valid = false;
            }

            QSet<quint32> targets;
            for (const HirValue* phi : phis[successor]) {
                if (targets.contains(phi->id)) {
                    block(edge, QStringLiteral("duplicate_phi_target"));
                    edge_structure_valid = false;
                    continue;
                }
                targets.insert(phi->id);
                const HirPhiInput* input = nullptr;
                int matches = 0;
                for (const auto& incoming_value : phi->phi_inputs) {
                    if (incoming_value.predecessor_block == predecessor) {
                        input = &incoming_value;
                        ++matches;
                    }
                }
                if (matches != 1 || !input) {
                    block(edge, QStringLiteral("phi_edge_input_not_unique"));
                    edge_structure_valid = false;
                    continue;
                }
                if (input->source_value_id >=
                        static_cast<quint32>(hir.values.size())) {
                    block(edge, QStringLiteral("phi_source_id_out_of_bounds"));
                    edge_structure_valid = false;
                    continue;
                }
                const auto& source = hir.values[input->source_value_id];
                if (source.id != input->source_value_id) {
                    block(edge, QStringLiteral("hir_ir_identity_mismatch"));
                    edge_structure_valid = false;
                    continue;
                }
                if (source.representation != phi->representation
                    || source.width_bits != phi->width_bits) {
                    block(edge, QStringLiteral("phi_representation_mismatch"));
                    edge_structure_valid = false;
                    continue;
                }

                HirPhiEdgeAssignment assignment;
                assignment.target_phi_id = phi->id;
                assignment.source_value_id = input->source_value_id;
                assignment.representation = phi->representation;
                assignment.width_bits = phi->width_bits;
                assignment.identical_value =
                    assignment.target_phi_id == assignment.source_value_id;
                assignment.is_machine_state_token =
                    phi->representation != QStringLiteral("bitvector");
                assignment.sourced_from_opaque_projection =
                    source.operation == QStringLiteral("opaque_state_field")
                    || source.operation == QStringLiteral("opaque_instruction")
                    || source.operation == QStringLiteral("opaque_call")
                    || source.operation == QStringLiteral("undefined");
                edge.requires_machine_state_lowering |=
                    assignment.is_machine_state_token;
                edge.touches_opaque_state |=
                    assignment.sourced_from_opaque_projection;
                edge.simultaneous_assignments << assignment;
                ++report.total_assignments;
            }
            if (edge.simultaneous_assignments.size() != phis[successor].size()) {
                block(edge, QStringLiteral("incomplete_phi_assignment_set"));
                edge_structure_valid = false;
            }
            find_parallel_hazards(edge);
            if (edge.has_parallel_dependency)
                ++report.edges_with_parallel_dependency;
            if (edge.has_assignment_cycle)
                ++report.edges_with_cycles;
            if (edge.requires_machine_state_lowering)
                block(edge, QStringLiteral("state_token_lowering_unproven"));
            if (edge.touches_opaque_state)
                block(edge, QStringLiteral("opaque_phi_source_requires_barrier"));
            if (edge.has_parallel_dependency)
                block(edge, QStringLiteral("parallel_copy_lowering_unproven"));

            // Well-formedness means local CFG/Phi associations are accounted
            // for, NOT that any parallel copies can be safely emitted.
            edge.assignments_complete = edge_structure_valid;
            if (!edge.assignments_complete)
                well_formed = false;
            if (!edge.blockers.isEmpty())
                ++report.blocked_edges;
            report.edges << std::move(edge);
            ++created;
        }
        if (report.limited) break;
    }

    report.all_edges_covered = !report.limited && expected == created;
    report.all_assignments_well_formed = well_formed
        && report.all_edges_covered && report.ir_static_audit_passed
        && !report.graph_partial;
    if (report.limited)
        report.warnings << QStringLiteral(
            "Edge/assignment budget exceeded; incomplete result is "
            "NOT evidence that unlisted transfers do not exist.");
    if (!report.all_edges_covered)
        report.warnings << QStringLiteral(
            "Not all reachable predecessor/entry edges were represented.");
    if (report.edges_with_cycles)
        report.warnings << QStringLiteral(
            "Parallel-copy dependency cycle(s) present. Sequential "
            "assignments would overwrite sources; temporary values are "
            "required before any lowering.");
    report.rewrite_authorized = false;
    return report;
}

} // namespace reworkbench
