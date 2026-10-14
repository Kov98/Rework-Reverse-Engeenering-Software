#pragma once
#include "reworkbench/decompiler.hpp"
#include "reworkbench/project.hpp"

namespace reworkbench {
// Only default DecompilationOptions are cached. Custom passes use the direct API.
DecompilationResult cached_decompile(const std::shared_ptr<Project>& project,
    const AnalysisSnapshot& snapshot, qsizetype index, std::stop_token stop = {});
}
