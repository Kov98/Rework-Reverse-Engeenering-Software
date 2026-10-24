#pragma once
#include "decompiler_internal.hpp"
#include "reworkbench/hir.hpp"

namespace reworkbench::hir_analysis {

// One-to-one machine-IR -> HIR scaffold adapter, no source reconstruction.
// Type evidence is supplied from the SAME lifted Program.
HirObservation observe(const decompiler_detail::Program& program,
                       const TypeEvidenceObservation& evidence,
                       std::stop_token stop);
} // namespace reworkbench::hir_analysis
