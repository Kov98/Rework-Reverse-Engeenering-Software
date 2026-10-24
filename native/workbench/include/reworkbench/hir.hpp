#pragma once

#include "reworkbench/type_evidence.hpp"
#include "reworkbench/ir_semantic_audit.hpp"

#include <optional>

namespace reworkbench {

// HIR SCAFFOLD (v0): a lossless structural view of the supported machine IR.
// It deliberately does NOT claim a recovered C/C++ AST, source types or
// structured control flow. Original IR identities remain addressable.
struct HirPhiInput {
    qsizetype predecessor_block = -1;
    quint32 source_value_id = 0;
};

struct HirValue {
    quint32 id = 0;                  // Unchanged original machine-IR node ID.
    Address source_instruction;
    qsizetype owner_block = -1;      // -1 = entry/synthetic shared value.
    QString operation;               // Exact lowered machine-IR operation.
    QString representation;          // bitvector/memory/architecture/opaque.
    int width_bits = 0;
    QString source_type_status = QStringLiteral("unknown");
    QString effect_kind = QStringLiteral("none");
    QVector<quint32> operands;
    QVector<HirPhiInput> phi_inputs;
    QStringList interpretation_clues;
    bool conflicting_interpretations = false;
    bool clues_incomplete = true;
    std::optional<quint64> constant;
    int extract_insert_bit_offset = 0;
    QString original_ir_text;
};

struct HirBlock {
    qsizetype index = -1;
    Address entry_address;
    bool reachable = false;
    QVector<quint32> belonging_values;
    QVector<quint32> ordered_definitions; // IR order; not proven source order.
    QVector<qsizetype> predecessors;
    QVector<qsizetype> successors;
    QString terminator;               // goto/conditional/return/opaque/none.
    std::optional<quint32> condition_value;
    std::optional<quint32> return_target_value;
    std::optional<Address> taken_address;
    std::optional<Address> not_taken_address;
    std::optional<qsizetype> resolved_taken_block;
    std::optional<qsizetype> resolved_not_taken_block;
    bool target_resolution_incomplete = false;
};

struct HirObservation {
    QString version = QStringLiteral("hir_scaffold_v0");
    QString semantic_status = QStringLiteral("machine_ir_structural_only");
    qsizetype entry_block = -1;
    QVector<HirValue> values;
    IrSemanticAudit ir_audit;
    QVector<HirBlock> blocks;
    QVector<quint32> synthetic_or_entry_values;
    quint32 invalid_value_references = 0;
    bool has_opaque_barriers = false;
    bool source_types_verified = false;
    bool structured_control_flow_verified = false;
    bool report_complete = false;     // Completeness only within scope.
    bool partial = true;
    bool limited = false;
    QStringList warnings;
};

// Standalone diagnostic entry point, sharing the native lift and TypeEvidence.
// No rendering of C/C++ source and no mutation of the existing IR.
HirObservation observe_function_hir(const AnalysisSnapshot& snapshot,
                                    qsizetype function_index,
                                    std::stop_token stop = {});

} // namespace reworkbench
