#pragma once

#include "reworkbench/stack_frame.hpp"

namespace reworkbench {

// Reaching modeled writes for an individual RSP-relative storage location.
// A relation is MAY-reach, not a proof of the source variable, runtime value
// or a safe opportunity for constant folding.
struct StackReadRelation {
    Address read_instruction;
    quint32 read_ir_node = 0;
    qint64 entry_rsp_offset_bytes = 0;
    quint32 size_bytes = 0;
    QVector<Address> possible_source_writes;
    QVector<quint32> possible_source_write_nodes;
    bool may_read_entry_memory = false;
    bool unknown_memory_effect_may_intervene = false;
    bool ambiguous_overlapping_write = false;
    bool reporting_limited = false;
};

// A modeled Store generation with read uses that it MAY reach. This is not
// a recovered source-variable lifetime, and absent readers prove no deadness.
struct StackWriteGeneration {
    Address write_instruction;
    quint32 write_ir_node = 0;
    qint64 entry_rsp_offset_bytes = 0;
    quint32 size_bytes = 0;
    QVector<Address> possible_reaching_reads;
    bool may_cross_basic_blocks = false;
};

struct StackValueFlowObservation {
    QVector<StackReadRelation> read_relations;
    QVector<StackWriteGeneration> write_generations;
    quint32 locations_examined = 0;
    bool limited = false;
    bool dataflow_converged = false;
    bool has_unknown_aliases = false;
    QStringList warnings;
};

} // namespace reworkbench
