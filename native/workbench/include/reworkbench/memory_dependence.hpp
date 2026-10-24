#pragma once

#include "reworkbench/stack_value_flow.hpp"

namespace reworkbench {

// Stack-only *candidate* memory-use graph. This is an intermediate step
// toward MemorySSA, NOT globally complete MemorySSA or proof of variable identity.
struct MemoryUseCandidate {
    Address instruction;
    quint32 read_ir_node = 0;
    qint64 entry_rsp_offset_bytes = 0;
    quint32 size_bytes = 0;
    QVector<quint32> possible_defining_store_nodes;
    bool may_use_entry_memory = false;
    bool needs_memory_phi = false; // Multiple modeled defs or entry/unknown source.
    bool may_depend_on_unknown_write = false;
    bool has_partial_alias = false;
    bool incomplete_producers = false;
};

struct StackAliasPair {
    qint64 first_offset_bytes = 0;
    quint32 first_size_bytes = 0;
    qint64 second_offset_bytes = 0;
    quint32 second_size_bytes = 0;
    QString relation; // exact_range or partial_overlap, never original source alias.
};

struct MemoryDependenceObservation {
    QString scope = QStringLiteral("modeled_rsp_relative_stack_only");
    QVector<MemoryUseCandidate> uses;
    QVector<StackAliasPair> overlap_pairs;
    quint32 proven_disjoint_range_pairs = 0;
    bool complete = false;
    bool limited = false;
    bool unknown_memory_effects = true;
    QStringList warnings;
};

MemoryDependenceObservation summarize_memory_dependences(
    const StackFrameObservation& frame, const StackValueFlowObservation& flow);

} // namespace reworkbench
