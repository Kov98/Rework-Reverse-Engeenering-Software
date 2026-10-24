#pragma once

#include "reworkbench/hir.hpp"

namespace reworkbench {

// Control-flow evidence based on the HIR v0 machine-CFG. These observations
// DO NOT establish source-level if/while/switch or exception semantics.
struct ControlBlockFacts {
    qsizetype block_index = -1;
    bool reached_from_entry = false;
    bool can_reach_known_return = false;
    QVector<qsizetype> dominators;
    QVector<qsizetype> post_dominators;
    std::optional<qsizetype> immediate_dominator;
    std::optional<qsizetype> immediate_post_dominator;
};

struct NaturalLoopCandidate {
    qsizetype header_block = -1;
    qsizetype latch_block = -1;
    QVector<qsizetype> member_blocks;
    bool single_entry_proven_in_observed_cfg = false;
    QString classification = QStringLiteral("natural_loop_candidate");
};

struct ConditionalJoinCandidate {
    qsizetype header_block = -1;
    qsizetype taken_block = -1;
    qsizetype not_taken_block = -1;
    qsizetype join_block = -1;
    QString classification = QStringLiteral("conditional_join_candidate");
};

struct IrreducibleCfgComponent {
    QVector<qsizetype> member_blocks;
    QVector<qsizetype> entry_blocks;
};

struct ControlRegionObservation {
    QString scope = QStringLiteral("hir_v0_observed_cfg_only");
    QVector<ControlBlockFacts> blocks;
    QVector<NaturalLoopCandidate> natural_loops;
    QVector<ConditionalJoinCandidate> conditional_joins;
    QVector<IrreducibleCfgComponent> irreducible_components;
    quint32 observed_backedges = 0;
    bool dominators_converged = false;
    bool post_dominators_converged = false;
    bool post_dominators_reliable = false;
    bool cfg_closed = false;
    bool has_unknown_exits = true;
    bool source_control_verified = false;
    bool partial = true;
    bool limited = false;
    QStringList warnings;
};

// Read-only sidecar over an existing HIR observation (no second lift).
ControlRegionObservation observe_hir_control_regions(
    const HirObservation& hir, std::stop_token stop = {});

} // namespace reworkbench
