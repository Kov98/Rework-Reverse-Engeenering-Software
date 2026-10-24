#pragma once

#include "reworkbench/control_regions.hpp"

namespace reworkbench {

// v0 is a read-only proposal, NOT a validated C AST or an executable rewrite.
// The original CFG remains the only authoritative control-flow representation.
struct RegionProposal {
    QString kind; // if_else_diamond_candidate / natural_loop_candidate
    qsizetype header = -1;
    QVector<qsizetype> member_blocks;
    std::optional<qsizetype> join_block;
    std::optional<qsizetype> exit_block;
    QString confidence = QStringLiteral("diagnostic_only");
    QStringList blockers;
    bool structurally_eligible = false;
    bool rewrite_authorized = false;
};

struct RawBlockFallback {
    qsizetype block_index = -1;
    Address entry;
    QString terminator;
    QVector<qsizetype> successors;
    std::optional<quint32> condition_value_id;
    std::optional<Address> taken_address;
    std::optional<Address> not_taken_address;
    bool unknown_successor = false;
    bool reachable = false;
    bool covered_by_proposal = false;
    quint32 proposal_count = 0;
    QString fallback_reason;
};

struct HirStructuringPlan {
    QString version = QStringLiteral("hir_structuring_plan_v0");
    QVector<RegionProposal> proposals;
    QVector<RawBlockFallback> fallback_blocks;
    QVector<qsizetype> uncovered_reachable_blocks;
    quint32 eligible_proposals = 0;
    quint32 blocked_proposals = 0;
    quint32 overlapping_candidate_blocks = 0;
    bool raw_cfg_preserved = true;
    bool ir_structural_checks_passed = false;
    bool phi_edge_associations_complete = false;
    quint32 phi_parallel_dependency_edges = 0;
    quint32 phi_copy_cycle_edges = 0;
    bool phi_copy_schedule_structurally_consistent = false;
    quint32 phi_virtual_temporaries_planned = 0;
    quint32 phi_symbolic_state_transfers = 0;
    bool edge_effect_contracts_accounted_for = false;
    quint32 edge_effects_unresolved = 0;
    bool control_edges_accounted_for = false;
    quint32 critical_edges_detected = 0;
    quint32 possible_nonreturning_call_blocks = 0;
    quint32 unresolved_control_terminators = 0;
    QStringList unproved_rewrite_obligations;
    bool emitted_source_code = false;
    bool rewrite_authorized = false;
    bool all_reachable_blocks_have_fallback = false;
    bool limited = false;
    bool partial = true;
    QStringList warnings;
};

// Consumes the existing HIR and the sidecar CFG findings, never transforms
// their nodes/edges. All proposed regions remain optional and reversible.
HirStructuringPlan plan_hir_structuring(const HirObservation& hir,
    const ControlRegionObservation& control, std::stop_token stop = {});

} // namespace reworkbench
