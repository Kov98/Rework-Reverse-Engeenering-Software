#pragma once

#include "reworkbench/types.hpp"

namespace reworkbench {

inline constexpr const char* decompiler_semantics_version = "reworkbench-scalar-ssa-1";

// These limits are clamped to hard ceilings; zero never means unlimited.
struct DecompilationOptions {
    quint32 max_instructions = 4096;
    quint32 max_blocks = 4096;
    quint32 max_ir_nodes = 196608;
    quint32 max_dataflow_iterations = 96;
    quint32 max_output_characters = 2 * 1024 * 1024;
    bool structure_control_flow = true;
    bool propagate_constants = true;
};

struct DecompilationResult {
    QString pseudocode;
    QString ssa_text;
    // Zero-based output lines with an exact original instruction; synthetic lines are omitted.
    QMap<int, Address> pseudocode_addresses;
    QMap<int, Address> ssa_addresses;
    QStringList warnings;
    quint32 modeled_instructions = 0;
    quint32 total_instructions = 0;
    quint32 reachable_blocks = 0;
    quint32 ir_nodes = 0;
    bool dataflow_converged = false;
    bool output_truncated = false;
    // Coverage of the documented non-faulting, flat-memory machine model;
    // never a claim to recover the original source or its function signature.
    bool complete = false;
};

DecompilationResult decompile_function(const AnalysisSnapshot& snapshot,
    qsizetype function_index, const DecompilationOptions& options = {},
    std::stop_token stop = {});

} // namespace reworkbench
