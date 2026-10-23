#pragma once
#include "reworkbench/binary_image.hpp"

namespace reworkbench {

[[nodiscard]] AnalysisSnapshot analyze_binary(const QString& path,
                                               const OpenOptions& options = {},
                                               const AnalysisLimits& limits = {},
                                               std::stop_token stop = {},
                                               ProgressCallback progress = {});
[[nodiscard]] RenderedFunction render_ir(const Function& function,
                                          Architecture architecture,
                                          std::stop_token stop = {});
[[nodiscard]] QString decoder_description();

} // namespace reworkbench
