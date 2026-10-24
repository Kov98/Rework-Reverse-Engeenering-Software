#pragma once
#include "reworkbench/memory_regions.hpp"

namespace reworkbench {
struct RegionEffectSite {
    Address instruction;
    quint32 ir_node = 0;
    bool write = false;
    QString confidence = QStringLiteral("unknown");
};
struct RegionEffectSummary {
    QString region;
    quint32 observed_loads = 0;
    quint32 observed_stores = 0;
    bool transitive_read_observed = false;
    bool transitive_write_observed = false;
    std::optional<quint32> read_witness_function_index;
    std::optional<quint32> write_witness_function_index;
    QVector<RegionEffectSite> sites;
    bool samples_truncated = false;
};
struct LocalRegionEffects {
    QVector<RegionEffectSummary> regions;
    bool partial = true;
    bool classification_limited = false;
    bool opaque_effects_present = false;
    QStringList warnings;
};
inline QVector<RegionEffectSummary> empty_region_effects() {
    QVector<RegionEffectSummary> result;
    for (const char* label : {"stack_relative", "mapped_image_candidate",
                              "absolute_candidate", "unknown"}) {
        RegionEffectSummary item;
        item.region = QString::fromLatin1(label);
        result.push_back(item);
    }
    return result;
}
} // namespace reworkbench
