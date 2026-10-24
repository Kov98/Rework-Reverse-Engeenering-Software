#pragma once

#include "decompiler_internal.hpp"
#include "reworkbench/abi.hpp"
#include "reworkbench/stack_frame.hpp"

namespace reworkbench::stack_frame_analysis {

// Runs against the IR generated in the caller; no second lift or extra cache.
// ABI is a candidate only, so region classifications are advisory.
StackFrameObservation observe(const decompiler_detail::Program& program,
    const AbiProfile& profile, std::stop_token stop);

} // namespace reworkbench::stack_frame_analysis
