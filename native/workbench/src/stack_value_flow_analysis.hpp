#pragma once

#include "decompiler_internal.hpp"
#include "reworkbench/stack_value_flow.hpp"

namespace reworkbench::stack_value_flow_analysis {

StackValueFlowObservation observe(
    const decompiler_detail::Program& program,
    const StackFrameObservation& frame,
    std::stop_token stop);

} // namespace reworkbench::stack_value_flow_analysis
