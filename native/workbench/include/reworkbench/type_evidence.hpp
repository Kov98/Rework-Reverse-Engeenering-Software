#pragma once
#include "reworkbench/types.hpp"

namespace reworkbench {

// Evidence about a MACHINE-IR value, never a recovered C/C++ type.
// "confirmed_ir" confirms only an IR fact, not semantics of the original
// program. "inferred" is an interpretation candidate, not type certainty.
struct TypeClue {
    QString kind;
    QString origin; // ir_definition, operand_use, abi_candidate
    QString confidence; // confirmed_ir, inferred, unknown
    Address instruction;
    quint32 origin_ir_node = 0;
    QString detail;
};

struct ValueTypeEvidence {
    quint32 ir_node = 0;
    Address definition;
    QString ir_representation; // bits, memory_token, architecture, opaque_state
    int width_bits = 0;
    QString source_type_status = QStringLiteral("unknown");
    bool signed_operation_seen = false;
    bool unsigned_operation_seen = false;
    bool address_use_seen = false;
    bool interpretation_conflict = false;
    QVector<TypeClue> clues;
    bool clues_truncated = false;
};

struct TypeEvidenceObservation {
    QString status = QStringLiteral("not_analyzed");
    QVector<ValueTypeEvidence> values;
    quint32 total_ir_nodes = 0;
    quint32 values_with_conflicts = 0;
    bool partial = true;
    bool limited = false;
    bool abi_was_candidate_only = true;
    QStringList warnings;
};
// Standalone public entry point: works without an ABI candidate.
TypeEvidenceObservation observe_function_types(const AnalysisSnapshot& snapshot,
    qsizetype function_index, std::stop_token stop = {});

} // namespace reworkbench
