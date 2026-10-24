#pragma once

#include "reworkbench/parallel_copy_plan.hpp"

namespace reworkbench {

// A boundary contract describes WHERE symbolic Phi work belongs on a CFG
// edge. It never evaluates instructions, moves effects or authorizes a
// source-code rewrite.
struct HirEdgeBoundaryAction {
    QString location;  // predecessor_exit / successor_entry / symbolic_state
    QString kind;      // snapshot / scalar_transfer / identity / state_dependency
    quint32 source_value_id = 0;
    quint32 target_phi_id = 0;
    std::optional<quint32> virtual_temp_id;
    QString representation;
    int width_bits = 0;
    bool machine_effect_executed = false;
};

struct HirEdgeEffectContract {
    qsizetype predecessor_block = -1;
    qsizetype successor_block = -1;
    bool synthetic_entry = false;
    QString predecessor_terminator;
    QString capture_boundary;
    QString apply_boundary = QStringLiteral("successor_entry_phi_merge");
    QVector<HirEdgeBoundaryAction> captures;
    QVector<HirEdgeBoundaryAction> applications;
    QVector<quint32> predecessor_effect_ir_ids; // Observed order only.
    quint32 predecessor_effect_count = 0;
    bool effect_samples_truncated = false;
    bool cfg_edge_observed = false;
    bool phi_schedule_accounted_for = false;
    bool predecessor_has_opaque_effects = false;
    bool predecessor_has_potentially_faulting_load = false;
    bool exceptional_control_unknown = true;
    bool effect_order_preservation_proven = false;
    bool lowering_executed = false;
    bool rewrite_authorized = false;
    QStringList blockers;
};

struct HirEdgeEffectsObservation {
    QString version = QStringLiteral("hir_edge_effect_boundaries_v1");
    QVector<HirEdgeEffectContract> edges;
    quint32 expected_edges = 0;
    quint32 total_capture_actions = 0;
    quint32 total_apply_actions = 0;
    quint32 edges_with_opaque_effects = 0;
    quint32 unresolved_edges = 0;
    bool all_edges_accounted_for = false;
    bool machine_ir_unchanged = true;
    bool lowering_executed = false;
    bool rewrite_authorized = false;
    bool partial = true;
    bool limited = false;
    QStringList warnings;
};

// Read-only adapter over HIR and the Phase-017 symbolic copy plan.
HirEdgeEffectsObservation observe_hir_edge_effects(
    const HirObservation& hir, const HirParallelCopyPlan& copies,
    std::stop_token stop = {});

} // namespace reworkbench
