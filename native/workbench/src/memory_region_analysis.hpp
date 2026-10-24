#pragma once

#include "decompiler_internal.hpp"
#include "reworkbench/memory_regions.hpp"

namespace reworkbench::memory_region_analysis {

// Read-only sidecar: cannot affect IR semantics, optimizations or pseudocode.
MemoryRegionObservation observe(const decompiler_detail::Program& program,
    const BinaryMetadata& metadata, std::stop_token stop);

} // namespace reworkbench::memory_region_analysis
