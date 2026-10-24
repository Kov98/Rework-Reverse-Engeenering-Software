#pragma once

#include "reworkbench/abi.hpp"

namespace reworkbench {

// An ABI candidate is not a validated callee contract. Only advisory lists
// are published here. Unknown effects must remain unknown in the machine IR.
struct CallEffectContract {
    QString status = QStringLiteral("unverified");
    QStringList candidate_caller_saved_gprs;
    QStringList candidate_callee_saved_gprs;
    bool all_tracked_register_effects_unknown = true;
    bool memory_reads_unknown = true;
    bool memory_writes_unknown = true;
    bool control_effects_unknown = true;
    bool vector_register_effects_unknown = true;
    bool can_assume_return = false;
};

CallEffectContract conservative_call_effects(
    const std::optional<AbiProfile>& profile);

} // namespace reworkbench
