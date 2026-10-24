#pragma once

#include "reworkbench/types.hpp"

namespace reworkbench {

struct InterproceduralCallsite {
    quint32 caller_index = 0;
    Address call_instruction;
    std::optional<Address> direct_target;
    std::optional<quint32> recovered_callee_index;
    QString classification; // internal_direct / external_or_undiscovered / indirect
};

struct InterproceduralFunction {
    quint32 function_index = 0;
    Address entry;
    QString name;
    quint32 direct_internal_calls = 0;
    quint32 unresolved_calls = 0;
    bool recovered_body_partial = false;
    bool direct_self_recursion_observed = false;
    bool may_transitively_reach_unresolved = true;
    bool register_effects_unknown = true;
    bool memory_effects_unknown = true;
};

struct InterproceduralObservation {
    QVector<InterproceduralCallsite> calls;
    QVector<InterproceduralFunction> functions;
    QString graph_status = QStringLiteral("not_analyzed");
    bool limited = false;
    bool complete = false;
    QStringList warnings;
};

// A direct-call discovery graph plus a *may reach unresolved* fixed point.
// This is not a proven transitive effect model or proof of call targets.
InterproceduralObservation observe_interprocedural_calls(
    const AnalysisSnapshot& snapshot, std::stop_token stop = {});

} // namespace reworkbench
