#pragma once

#include "reworkbench/types.hpp"

namespace reworkbench {

// Address classification from the exact modeled x86-64 address-expression graph.
// The classification never proves an allocation's lifetime or source-language
// pointer ownership. No heap address is claimed without allocation evidence.
struct MemoryAddressFact {
    quint32 access_ir_node = 0;
    quint32 pointer_ir_node = 0;
    Address instruction;
    qsizetype block = -1;
    bool write = false;
    int width_bits = 0;
    QString region;  // stack_relative, mapped_image_candidate, absolute_candidate, unknown
    std::optional<qint64> entry_rsp_offset_bytes;
    std::optional<quint64> absolute_address;
    QString mapped_image_region;
};

struct MemoryAliasPair {
    quint32 first_access_ir_node = 0;
    quint32 second_access_ir_node = 0;
    QString relation; // must_overlap, proven_disjoint, may_alias
    QString reason;
};

// Relations describe addresses/ranges only, never equality of stored values.
struct MemoryRegionObservation {
    QString scope = QStringLiteral("x86_64_modeled_accesses_only");
    QVector<MemoryAddressFact> accesses;
    QVector<MemoryAliasPair> noteworthy_pairs;
    quint32 must_overlap_pairs = 0;
    quint32 proven_disjoint_pairs = 0;
    quint32 may_alias_pairs = 0;
    quint32 unknown_pointer_accesses = 0;
    bool opaque_effects_present = false;
    bool limited = false;
    bool complete = false;
    QStringList warnings;
};

} // namespace reworkbench
