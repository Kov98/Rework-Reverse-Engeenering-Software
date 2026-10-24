#include "reworkbench/call_effects.hpp"

namespace reworkbench {

CallEffectContract conservative_call_effects(
    const std::optional<AbiProfile>& profile) {
    CallEffectContract effects;
    if (profile) {
        effects.status = QStringLiteral("abi_candidate_only");
        effects.candidate_caller_saved_gprs = profile->caller_saved_gprs;
        effects.candidate_callee_saved_gprs = profile->callee_saved_gprs;
    }
    // An import, indirect target, syscall, custom ABI, external assembly,
    // exception, or incomplete function may invalidate inferred preservation.
    // Do NOT elide register, memory or control effects based on file format.
    return effects;
}

} // namespace reworkbench
