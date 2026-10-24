#pragma once

#include "reworkbench/edge_effects.hpp"

namespace reworkbench {

// v1 is an observational description of the recovered CFG only.
// Neither "noreturn" nor an indirect destination is inferred as fact.
struct HirTerminatorEvidence {
    qsizetype block_index = -1;
    QString observed_kind;
    QString normalized_kind = QStringLiteral("unknown");
    QVector<qsizetype> observed_successors;
    bool observed_successor_set_consistent = false;
    bool opaque_call_seen_in_block = false;
    bool call_noreturn_unverified = false;
    bool indirect_or_external_target_possible = false;
    bool exceptional_successors_unverified = true;
    bool source_terminator_semantics_proven = false;
    QStringList blockers;
};

struct HirControlEdgeEvidence {
    qsizetype predecessor_block = -1;
    qsizetype successor_block = -1;
    QString branch_arm = QStringLiteral("unknown");
    bool cfg_reciprocal_edge = false;
    bool critical_edge_in_observed_cfg = false;
    bool needs_edge_split_if_lowered = false;
    bool effect_contract_found = false;
    bool effect_contract_unresolved = true;
    bool exceptional_transfer_unverified = true;
    bool rewriting_safe = false;
    QStringList blockers;
};

struct HirControlSafetyObservation {
    QString version = QStringLiteral("hir_control_edges_v1");
    QVector<HirTerminatorEvidence> terminators;
    QVector<HirControlEdgeEvidence> edges;
    quint32 reachable_blocks = 0;
    quint32 critical_edges = 0;
    quint32 potentially_nonreturning_calls = 0;
    quint32 unresolved_terminators = 0;
    quint32 edges_needing_splitting = 0;
    quint32 missing_effect_contracts = 0;
    bool all_observed_edges_accounted_for = false;
    bool exceptions_and_indirects_proven = false;
    bool source_control_verified = false;
    bool rewriting_authorized = false;
    bool limited = false;
    bool partial = true;
    QStringList warnings;
};

HirControlSafetyObservation observe_hir_control_safety(
    const HirObservation& hir, const HirEdgeEffectsObservation& effects,
    std::stop_token stop = {});

} // namespace reworkbench
