#include "stack_frame_analysis.hpp"
#include "stack_abi_observation.hpp"

#include <QSet>

#include <algorithm>
#include <map>
#include <utility>

namespace reworkbench::stack_frame_analysis {
namespace {

constexpr qsizetype maximum_slots = 512;
constexpr qsizetype maximum_evidence_per_slot = 64;

// Region classification describes the address relative to the function's
// *entry* RSP, not the provenance or lifetime of a C/C++ source variable.
QString region_for(qint64 offset, quint32 bytes, const AbiProfile& profile) {
    const qint64 end = offset + static_cast<qint64>(bytes);
    const qint64 first_argument =
        static_cast<qint64>(profile.stack_argument_start_from_entry_rsp);
    if (offset < 0 && end <= 0)
        return QStringLiteral("local_spill_or_outgoing_area");
    if (offset >= 0 && end <= 8)
        return QStringLiteral("return_address_area");
    if (offset >= first_argument && end > offset)
        return QStringLiteral("incoming_argument_address_candidate");
    if (profile.id == QStringLiteral("windows-x64")
        && offset >= 8 && end <= 40)
        return QStringLiteral("home_space_or_caller_storage");
    return QStringLiteral("unknown_or_boundary_overlapping_area");
}

bool intersects(const StackSlotCandidate& a, const StackSlotCandidate& b) {
    const qint64 a_end = a.entry_rsp_offset_bytes
        + static_cast<qint64>(a.size_bytes);
    const qint64 b_end = b.entry_rsp_offset_bytes
        + static_cast<qint64>(b.size_bytes);
    return a.entry_rsp_offset_bytes < b_end
        && b.entry_rsp_offset_bytes < a_end;
}

bool already_recorded(const StackSlotCandidate& slot,
                      const StackFrameReference& reference) {
    for (const auto& prior : slot.evidence) {
        if (prior.instruction == reference.instruction
            && prior.entry_rsp_offset_bytes == reference.entry_rsp_offset_bytes
            && prior.width_bits == reference.width_bits
            && prior.write == reference.write)
            return true;
    }
    return false;
}

} // namespace

StackFrameObservation observe(const decompiler_detail::Program& program,
                              const AbiProfile& profile, std::stop_token stop) {
    StackFrameObservation result;
    if (program.architecture != Architecture::X64) {
        result.warnings << QStringLiteral(
            "RSP-relative stack-frame observations require x86-64 IR.");
        return result;
    }

    const auto accesses = stack_observation::frame_accesses(program, stop);
    result.unresolved_memory_accesses = accesses.unresolved_addresses;
    result.unknown_machine_effects_present = accesses.opaque_effects_present;
    result.limited = accesses.limited;
    result.partial = program.incomplete || accesses.limited
        || accesses.opaque_effects_present || accesses.unresolved_addresses != 0;

    // Keep width-distinct and offset-distinct slots separate; a union can
    // accidentally invent an aggregate, type, alias or variable lifetime.
    using Key = std::pair<qint64, quint32>;
    std::map<Key, qsizetype> index;
    for (const auto& access : accesses.accesses) {
        decompiler_detail::check_cancelled(stop);
        if (access.width_bits <= 0 || access.width_bits > 64
            || access.width_bits % 8 != 0) continue;
        const quint32 bytes = static_cast<quint32>(access.width_bits / 8);
        const Key key{access.entry_rsp_offset_bytes, bytes};
        const auto found = index.find(key);
        if (found == index.end()) {
            if (result.slots.size() >= maximum_slots) {
                result.limited = true;
                result.partial = true;
                break;
            }
            StackSlotCandidate slot;
            slot.entry_rsp_offset_bytes = access.entry_rsp_offset_bytes;
            slot.size_bytes = bytes;
            slot.region = region_for(slot.entry_rsp_offset_bytes, bytes, profile);
            index.emplace(key, result.slots.size());
            result.slots << std::move(slot);
        }
        auto& slot = result.slots[index.at(key)];
        if (already_recorded(slot, access)) continue;
        if (slot.evidence.size() >= maximum_evidence_per_slot) {
            result.limited = true;
            result.partial = true;
            continue;
        }
        slot.evidence << access;
        if (access.write) ++slot.modeled_writes;
        else ++slot.modeled_reads;
    }

    // Mark intersecting memory ranges, without pretending distinct accesses
    // represent a single source variable or prove aliasing along one path.
    for (qsizetype i = 0; i < result.slots.size(); ++i) {
        decompiler_detail::check_cancelled(stop);
        for (qsizetype j = i + 1; j < result.slots.size(); ++j) {
            if (!intersects(result.slots[i], result.slots[j])) continue;
            result.slots[i].overlaps_other_candidates = true;
            result.slots[j].overlaps_other_candidates = true;
        }
    }

    std::sort(result.slots.begin(), result.slots.end(),
        [](const StackSlotCandidate& left, const StackSlotCandidate& right) {
            if (left.entry_rsp_offset_bytes != right.entry_rsp_offset_bytes)
                return left.entry_rsp_offset_bytes < right.entry_rsp_offset_bytes;
            return left.size_bytes < right.size_bytes;
        });

    result.warnings << QStringLiteral(
        "Stack locations are address/width observations only. Multiple "
        "locations can represent one variable, and one location can be "
        "reused by different variables or call arguments.");
    if (result.unresolved_memory_accesses)
        result.warnings << QStringLiteral(
            "Some memory addresses could not be represented as an affine "
            "offset of entry RSP; their stack usage is unknown.");
    if (result.unknown_machine_effects_present)
        result.warnings << QStringLiteral(
            "Opaque instructions or calls may alter stack memory and "
            "registers; complete variable and lifetime recovery is impossible.");
    if (result.limited)
        result.warnings << QStringLiteral(
            "Stack-frame observation reached an analysis/reporting limit.");

    return result;
}

} // namespace reworkbench::stack_frame_analysis
