#pragma once
#include "reworkbench/types.hpp"

#include <QJsonArray>
#include <QJsonObject>

namespace reworkbench {

// Files above 1 GiB are rejected. Output ranges are capped; totals remain exact.
[[nodiscard]] QJsonObject compare_binaries(const QString& left, const QString& right,
                                            std::stop_token stop = {});
[[nodiscard]] QJsonObject compare_functions(const AnalysisSnapshot& left, qsizetype left_index,
                                             const AnalysisSnapshot& right, qsizetype right_index,
                                             std::stop_token stop = {});

// Each patch: {"offset":"0x...", "expected":"aabb", "replacement":"ccdd"}.
// Expected and replacement byte counts must agree. Never modifies the source.
[[nodiscard]] QJsonObject export_patched_copy(const QString& source, const QString& destination,
                                              const QJsonArray& patches,
                                              const QString& protected_project_root = {},
                                              const QStringList& protected_paths = {},
                                              std::stop_token stop = {});

// Initial state: {"registers":{"rax":"0x1"}, "memory":[{"address":"0x...",
// "bytes":"..."}], "max_steps":10000, "entry":"0x..."}. No host memory access,
// target execution, syscalls, implicit input values, or automatic file mappings.
[[nodiscard]] QJsonObject emulate_function(const AnalysisSnapshot& snapshot, qsizetype function_index,
                                            const QJsonObject& initial = {},
                                            std::stop_token stop = {});
} // namespace reworkbench
