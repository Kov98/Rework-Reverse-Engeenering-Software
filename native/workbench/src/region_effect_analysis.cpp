#include "region_effect_analysis.hpp"
#include "memory_region_analysis.hpp"

#include <QHash>

namespace reworkbench::region_effect_analysis {
namespace {
using namespace decompiler_detail;
constexpr qsizetype max_sites_per_region = 16;

qsizetype region_index(const QString& name) {
    if (name == QStringLiteral("stack_relative")) return 0;
    if (name == QStringLiteral("mapped_image_candidate")) return 1;
    if (name == QStringLiteral("absolute_candidate")) return 2;
    return 3; // Unknown includes every unsupported or unclassified pointer.
}
} // namespace

LocalRegionEffects observe(const Program& program,
                           const BinaryMetadata& metadata,
                           std::stop_token stop) {
    LocalRegionEffects local;
    local.regions = empty_region_effects();
    const bool supported = program.architecture == Architecture::X64;
    QHash<quint32, QString> region_by_ir_node;
    if (supported) {
        const auto mapped = memory_region_analysis::observe(program, metadata, stop);
        local.classification_limited = mapped.limited;
        local.opaque_effects_present = mapped.opaque_effects_present;
        local.partial = mapped.limited || !mapped.complete || program.incomplete;
        for (const auto& access : mapped.accesses)
            region_by_ir_node.insert(access.access_ir_node, access.region);
        if (mapped.limited)
            local.warnings << QStringLiteral(
                "Region classification exceeded its bounded budget. "
                "Unclassified modeled accesses are reported as unknown.");
        if (mapped.unknown_pointer_accesses)
            local.warnings << QStringLiteral(
                "Pointer origins are unresolved for some accesses; "
                "unknown can refer to stack, heap, globals or mappings.");
    } else {
        local.partial = true;
        local.classification_limited = true;
        local.warnings << QStringLiteral(
            "Region classifier currently supports x86-64 only; "
            "modeled accesses on this architecture have unknown region.");
    }

    // Iterate ALL reachable modeled accesses, even when the classifier's
    // reporting cap has been reached. This preserves positive read/write
    // observations and never invents a region for missing classifications.
    for (qsizetype i = 0; i < program.nodes.size(); ++i) {
        if ((i & 0x7f) == 0) check_cancelled(stop);
        const auto& node = program.nodes[i];
        if (node.block < 0 || node.block >= program.blocks.size()
            || !program.blocks[node.block].reachable) continue;
        if (node.op == Op::Opaque || node.op == Op::CallOpaque) {
            local.opaque_effects_present = true;
            local.partial = true;
            continue; // Unknown effects are not an observed Load/Store.
        }
        if (node.op != Op::Load && node.op != Op::Store) continue;

        const auto found = region_by_ir_node.constFind(static_cast<quint32>(i));
        const QString name = found == region_by_ir_node.cend()
            ? QStringLiteral("unknown") : *found;
        auto& row = local.regions[region_index(name)];
        const bool write = node.op == Op::Store;
        if (write) ++row.observed_stores;
        else ++row.observed_loads;

        if (row.sites.size() < max_sites_per_region) {
            RegionEffectSite site;
            site.instruction = node.address;
            site.ir_node = static_cast<quint32>(i);
            site.write = write;
            site.confidence = name == QStringLiteral("unknown")
                ? QStringLiteral("unknown") : QStringLiteral("inferred");
            row.sites << site;
        } else {
            row.samples_truncated = true;
        }
    }
    if (local.opaque_effects_present)
        local.warnings << QStringLiteral(
            "Opaque instructions/calls may read or write any region. "
            "No absence-of-effects claim is permitted.");
    local.warnings << QStringLiteral(
        "Region labels describe symbolic address expressions, not "
        "verified heap ownership, program-level purity or memory safety.");
    return local;
}
