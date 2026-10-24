#pragma once

#include "reworkbench/types.hpp"

namespace reworkbench {

// Static structural checks of the lifter's in-memory machine IR.
// A successful check does NOT prove x86 instruction semantics, ABI,
// alias disjointness or source-equivalent transformations.
struct IrSemanticIssue {
    QString code;
    QString severity; // error / warning
    quint32 ir_node = 0;
    qsizetype block = -1;
    QString detail;
};

struct IrSemanticAudit {
    QString scope = QStringLiteral("machine_ir_static_invariants_v1");
    QVector<IrSemanticIssue> issues;
    quint32 errors = 0;
    quint32 warnings = 0;
    quint32 nodes_examined = 0;
    quint32 phi_inputs_examined = 0;
    quint32 memory_operations_examined = 0;
    bool dominance_converged = false;
    bool cfg_consistent = false;
    bool operand_types_consistent = false;
    bool ssa_uses_dominated = false;
    bool memory_tokens_well_formed = false;
    bool memory_state_order_consistent = false;
    bool opaque_effects_present = false;
    bool issues_truncated = false;
    bool ir_incomplete = true;
    bool structural_checks_passed = false;
    bool semantic_equivalence_proven = false;
    bool rewrite_authorized = false;
    QStringList warnings_detail;
};

} // namespace reworkbench
