#pragma once

#include "decompiler_internal.hpp"
#include "reworkbench/function_abi.hpp"
#include "reworkbench/stack_frame.hpp"

namespace reworkbench::stack_observation {

// These are bounded, advisory observations of modeled SSA values, not a proof
// of arguments. Offsets are measured from the callee's entry RSP for reads,
// and from the caller's pre-CALL RSP for stores, respectively.
struct Observation {
    QVector<AbiStackAccess> accesses;
    bool limited = false;
    bool unknown_memory_path = false;
};

// Raw modeled Load/Store access from a concrete affine entry-RSP address.
// Used as evidence for stack slots, not as a recovered variable declaration.
struct FrameAccesses {
    QVector<StackFrameReference> accesses;
    quint32 unresolved_addresses = 0;
    bool opaque_effects_present = false;
    bool limited = false;
};

FrameAccesses frame_accesses(const decompiler_detail::Program& program,
    std::stop_token stop);

Observation incoming_reads(const decompiler_detail::Program& program,
    const AbiProfile& abi, std::stop_token stop);

Observation outgoing_stores(const decompiler_detail::Program& program,
    const decompiler_detail::Node& call, const AbiProfile& abi,
    std::stop_token stop);

} // namespace reworkbench::stack_observation
