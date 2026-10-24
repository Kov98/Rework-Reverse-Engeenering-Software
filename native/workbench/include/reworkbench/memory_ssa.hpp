#pragma once

#include "reworkbench/types.hpp"

namespace reworkbench {

// Explicit graph of the memory-state nodes ALREADY present in the native IR.
// These are whole-memory versions; a Store is NOT a proof of disjointness and
// opaque state changes kill the ability to reason about stored values.
struct MemoryVersion {
    quint32 id = 0;                 // Index into this report's versions.
    quint32 original_ir_node = 0;  // Exact original node within this snapshot.
    QString kind;                   // entry, store, phi, unknown_effect
    Address instruction;
    qsizetype basic_block = -1;
    std::optional<quint32> previous_version;
    QVector<quint32> incoming_versions;
    QVector<qsizetype> incoming_predecessors;
    bool unknown_effect = false;
};

struct MemoryReadUse {
    quint32 load_ir_node = 0;
    Address instruction;
    qsizetype basic_block = -1;
    quint32 observed_memory_version = 0;
    bool may_contain_unknown_effects = true;
};

struct MemorySSAObservation {
    QString scope = QStringLiteral("whole_memory_ir_version_graph");
    QVector<MemoryVersion> versions;
    QVector<MemoryReadUse> reads;
    bool complete = false; // Graph bookkeeping only, not decompiler correctness.
    bool limited = false;
    bool opaque_effects_present = false;
    QStringList warnings;
};

} // namespace reworkbench
