#pragma once
#include "reworkbench/types.hpp"
namespace reworkbench {
// Explicit future-use harness; calling this runs analysis, never the sample.
QJsonObject benchmark_corpus(const QJsonObject& manifest, std::stop_token stop = {}, ProgressCallback progress = {});
}
