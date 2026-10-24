#pragma once
#include "decompiler_internal.hpp"
#include "reworkbench/abi.hpp"
#include "reworkbench/type_evidence.hpp"

namespace reworkbench::type_evidence_analysis {
// Read-only observation over a single previously lifted function.
// ABI candidates never certify arguments or original source types.
TypeEvidenceObservation observe(const decompiler_detail::Program& program,
    const std::optional<AbiProfile>& abi_candidate, std::stop_token stop);
} // namespace reworkbench::type_evidence_analysis
