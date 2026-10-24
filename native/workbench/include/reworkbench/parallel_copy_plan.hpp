#pragma once

#include "reworkbench/phi_transfers.hpp"

namespace reworkbench {

// This is a TRANSACTIONAL SCHEDULE over symbolic SSA values.
// All scalar sources are captured before any scalar destination changes.
// Machine-state tokens are kept symbolic; no memory/call effects are executed.
struct HirParallelCopyStep {
    QString phase;       // snapshot / commit / symbolic_state
    QString operation;   // capture_scalar / write_from_snapshot / identity / preserve_state
    quint32 source_value_id = 0;
    quint32 target_phi_id = 0;
    std::optional<quint32> virtual_temp_id;
    QString representation;
    int width_bits = 0;
    bool performs_machine_effect = false;
};

struct HirParallelCopyEdgePlan {
    qsizetype predecessor_block = -1;
    qsizetype successor_block = -1;
    bool synthetic_entry = false;
    QVector<HirParallelCopyStep> steps;
    quint32 captured_sources = 0;
    quint32 scalar_writes_planned = 0;
    quint32 symbolic_state_transfers = 0;
    quint32 identity_transfers = 0;
    bool contains_dependency_cycle = false;
    bool has_parallel_dependencies = false;
    bool has_opaque_barrier = false;
    bool assignments_accounted_for = false;
    bool schedule_structurally_consistent = false;
    bool lowering_executed = false;
    bool rewrite_authorized = false;
    QStringList blockers;
};

struct HirParallelCopyPlan {
    QString version = QStringLiteral("phi_parallel_copy_plan_v1");
    QVector<HirParallelCopyEdgePlan> edges;
    quint32 expected_edge_count = 0;
    quint32 total_virtual_temporaries = 0;
    quint32 total_scalar_writes_planned = 0;
    quint32 total_symbolic_state_transfers = 0;
    quint32 total_edges_with_cycles = 0;
    quint32 blocked_edges = 0;
    bool all_edges_accounted_for = false;
    bool all_schedules_structurally_consistent = false;
    bool source_snapshot_before_any_write = true;
    bool machine_ir_unchanged = true;
    bool lowering_executed = false;
    bool rewrite_authorized = false;
    bool limited = false;
    bool partial = true;
    QStringList warnings;
};

// No mutation of the HIR or machine IR. Exposes a future lowering recipe,
// not statements that are safe to emit as machine/source operations.
HirParallelCopyPlan plan_parallel_phi_copies(
    const HirPhiTransferObservation& transfers, std::stop_token stop = {});

} // namespace reworkbench
