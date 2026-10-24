#pragma once

#include "decompiler_internal.hpp"
#include "reworkbench/ir_semantic_audit.hpp"

namespace reworkbench::ir_semantic_audit {

// Read-only static inspection of the same Program used by the HIR adapter.
// Does not execute the binary or authorize code motion / CFG rewrites.
IrSemanticAudit inspect(const decompiler_detail::Program& program,
                        std::stop_token stop);

} // namespace reworkbench::ir_semantic_audit
