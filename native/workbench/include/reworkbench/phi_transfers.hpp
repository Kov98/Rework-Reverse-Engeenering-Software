#pragma once

#include "reworkbench/hir.hpp"

namespace reworkbench {

// An SSA Phi group is simultaneous on an edge. It is NOT a sequence of
// assignments, and memory/architecture tokens are NOT source variables.
struct HirPhiEdgeAssignment {
    quint32 target_phi_id = 0;
    quint32 source_value_id = 0;
    QString representation;
    int width_bits = 0;
    bool identical_value = false;
    bool is_machine_state_token = false;
    bool sourced_from_opaque_projection = false;
};

struct HirPhiEdgeTransfer {
    qsizetype predecessor_block = -1; // -1: synthetic function entry.
    qsizetype successor_block = -1;
    bool synthetic_entry = false;
    QVector<HirPhiEdgeAssignment> simultaneous_assignments;
    bool assignments_complete = false;
    bool has_parallel_dependency = false;
    bool has_assignment_cycle = false;
    bool requires_machine_state_lowering = false;
    bool touches_opaque_state = false;
    bool rewrite_authorized = false;
    QStringList blockers;
};

struct HirPhiTransferObservation {
    QString version = QStringLiteral("hir_phi_edges_v1");
    QVector<HirPhiEdgeTransfer> edges;
    quint32 reachable_cfg_edges_expected = 0;
    quint32 synthetic_entry_edges_expected = 0;
    quint32 total_assignments = 0;
    quint32 edges_with_parallel_dependency = 0;
    quint32 edges_with_cycles = 0;
    quint32 blocked_edges = 0;
    bool ir_static_audit_passed = false;
    bool all_edges_covered = false;
    bool all_assignments_well_formed = false;
    bool opaque_barriers_present = false;
    bool graph_partial = true;
    bool limited = false;
    bool rewrite_authorized = false;
    QStringList warnings;
};

// Read-only construction; all HIR IDs/CFG edges remain unchanged.
HirPhiTransferObservation observe_hir_phi_transfers(
    const HirObservation& hir, std::stop_token stop = {});

} // namespace reworkbench
