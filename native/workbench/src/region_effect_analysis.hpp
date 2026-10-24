#pragma once
#include "decompiler_internal.hpp"
#include "reworkbench/region_effects.hpp"
namespace reworkbench::region_effect_analysis {
LocalRegionEffects observe(const decompiler_detail::Program& program,
                          const BinaryMetadata& metadata,
                          std::stop_token stop);
} // namespace reworkbench::region_effect_analysis
