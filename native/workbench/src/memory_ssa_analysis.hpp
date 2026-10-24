#pragma once

#include "decompiler_internal.hpp"
#include "reworkbench/memory_ssa.hpp"

namespace reworkbench::memory_ssa_analysis {

// Sidecar analysis only: does not alter the original IR, and never deletes,
// coalesces or substitutes a modeled memory operation.
MemorySSAObservation observe(const decompiler_detail::Program& program,
                             std::stop_token stop);

} // namespace reworkbench::memory_ssa_analysis
