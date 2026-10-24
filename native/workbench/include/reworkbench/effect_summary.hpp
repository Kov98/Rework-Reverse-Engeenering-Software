#pragma once

#include "reworkbench/interprocedural.hpp"
#include "reworkbench/region_effects.hpp"

namespace reworkbench {

// Describes only evidence seen by the current, partial x86/x64 lifter.
// "inferred" means an IR observation, NOT a verified program-level effect.
// Verified effects require an independent semantic proof that this pass does
// not supply; hence no summary produced here may claim verified purity.
struct FunctionEffectEvidence {
    QString domain;          // memory
    QString observation;     // modeled_load / modeled_store / opaque_effect
    QString confidence = QStringLiteral("inferred");
    Address instruction;
    quint32 ir_node = 0;
};

struct FunctionEffectSummary {
    quint32 function_index = 0;
    Address entry;
    QString name;
    QString evidence_level = QStringLiteral("unknown"); // unknown / inferred / verified (reserved)
    QVector<FunctionEffectEvidence> evidence;
    QVector<RegionEffectSummary> regional_effects;
    bool regional_effects_partial = true;

    bool direct_read_observed = false;
    bool direct_write_observed = false;
    bool transitive_read_observed = false;
    bool transitive_write_observed = false;
    // Function index containing a modeled Load/Store supporting a MAY fact.
    std::optional<quint32> read_witness_function_index;
    std::optional<quint32> write_witness_function_index;
    bool may_reach_unresolved_call = true;

    // Unknowns represent a MAY effect. Even a modeled-only body is not
    // proof that unobserved calls, memory aliases or traps are absent.
    bool may_read_unknown_memory = true;
    bool may_write_unknown_memory = true;
    bool register_effects_unknown = true;
    bool control_effects_unknown = true;
    bool signature_verified = false;
    bool purity_verified = false;
    bool analysis_partial = true;
    bool report_truncated = false;
    QStringList warnings;
};

struct InterproceduralEffectsObservation {
    QString status = QStringLiteral("not_analyzed");
    QVector<FunctionEffectSummary> functions;
    quint32 analyzed_bodies = 0;
    quint32 direct_edges = 0;
    quint32 unresolved_edges = 0;
    bool limited = false;
    bool complete = false;
    QStringList warnings;
};

// Read-only analysis: no call site is simplified, no register is restored,
// and no memory optimization is permitted by these advisory summaries.
InterproceduralEffectsObservation observe_interprocedural_effects(
    const AnalysisSnapshot& snapshot, std::stop_token stop = {});

} // namespace reworkbench
