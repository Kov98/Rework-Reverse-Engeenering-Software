#pragma once

#include "reworkbench/types.hpp"

namespace reworkbench {

// A memory access to an affine RSP-relative address. These are evidence
// locations, not debug symbols, original variable names or inferred C types.
struct StackFrameReference {
    Address instruction;
    qint64 entry_rsp_offset_bytes = 0;
    int width_bits = 0;
    bool write = false;
    // Stable identity within the current IR snapshot; not a binary address.
    quint32 ir_node_index = 0;
    qsizetype basic_block_index = -1;
};

// Distinct (offset,size) ranges deliberately remain distinct. Overlap may
// represent aliases, different lifetimes, spills, or a larger aggregate.
struct StackSlotCandidate {
    qint64 entry_rsp_offset_bytes = 0;
    quint32 size_bytes = 0;
    QString region; // Storage region candidate, NOT a verified source variable.
    quint32 modeled_reads = 0;
    quint32 modeled_writes = 0;
    bool overlaps_other_candidates = false;
    QVector<StackFrameReference> evidence;
};

struct StackFrameObservation {
    QString address_basis = QStringLiteral("callee_rsp_at_entry");
    QVector<StackSlotCandidate> slots;
    quint32 unresolved_memory_accesses = 0;
    bool unknown_machine_effects_present = false;
    bool limited = false;
    bool partial = true;
    QStringList warnings;
};

} // namespace reworkbench
