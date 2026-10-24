#include "stack_value_flow_analysis.hpp"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <limits>

namespace reworkbench::stack_value_flow_analysis {
namespace {
using namespace decompiler_detail;

constexpr qsizetype max_blocks = 192;
constexpr qsizetype max_locations = 128;
constexpr qsizetype max_events = 8192;
constexpr qsizetype max_results = 512;
constexpr qsizetype max_source_addresses = 32;
constexpr int max_iterations = 256;

struct Event {
    enum Kind { Read, Write, UnknownWrite } kind = UnknownWrite;
    quint32 ir_node = 0;
    qint64 offset = 0;
    quint32 bytes = 0;
};

struct State {
    QSet<quint32> reaching_writes; // Exact modeled Store IR node IDs.
    bool from_entry = false;
    bool unknown_effect = false;
    bool overlap = false;

    bool operator==(const State&) const = default;
};

bool overlaps(const Event& event, const StackSlotCandidate& slot) {
    const qint64 slot_end = slot.entry_rsp_offset_bytes
        + static_cast<qint64>(slot.size_bytes);
    const qint64 event_end = event.offset + static_cast<qint64>(event.bytes);
    return event.offset < slot_end && slot.entry_rsp_offset_bytes < event_end;
}

bool exact(const Event& event, const StackSlotCandidate& slot) {
    return event.offset == slot.entry_rsp_offset_bytes
        && event.bytes == slot.size_bytes;
}

void merge(State& destination, const State& source) {
    destination.reaching_writes.unite(source.reaching_writes);
    destination.from_entry |= source.from_entry;
    destination.unknown_effect |= source.unknown_effect;
    destination.overlap |= source.overlap;
    // Bound per-slot dataflow memory on adversarial CFGs. If too many
    // definitions could reach, report UNKNOWN rather than truncate and
    // accidentally treat a remaining writer as the only possible source.
    constexpr qsizetype max_reaching_writes = 64;
    if (destination.reaching_writes.size() > max_reaching_writes) {
        destination.reaching_writes.clear();
        destination.from_entry = false;
        destination.unknown_effect = true;
    }
}

// Forward may-reaching-definition transfer; never remove an opaque clobber
// unless a later modeled Store completely overwrites the exact slot.
void transfer(State& state, const Event& event, const StackSlotCandidate& slot) {
    if (event.kind == Event::UnknownWrite) {
        state.reaching_writes.clear();
        state.from_entry = false;
        state.unknown_effect = true;
        return;
    }
    if (event.kind != Event::Write || !overlaps(event, slot)) return;
    state.reaching_writes.clear();
    state.from_entry = false;
    if (exact(event, slot)) {
        state.reaching_writes.insert(event.ir_node);
        state.unknown_effect = false;
        state.overlap = false;
    } else {
        // A partial/overlapping store does not prove that the slot has the
        // value of a single original source variable.
        state.unknown_effect = true;
        state.overlap = true;
    }
}

struct Events {
    QVector<QVector<Event>> blocks;
    bool truncated = false;
    bool unknown_store_seen = false;
};

Events collect_events(const Program& program, const StackFrameObservation& frame,
                      std::stop_token stop) {
    Events result;
    result.blocks.resize(program.blocks.size());
    QHash<quint32, StackFrameReference> known;
    for (const auto& slot : frame.slots)
        for (const auto& access : slot.evidence)
            known.insert(access.ir_node_index, access);

    qsizetype count = 0;
    for (qsizetype i = 0; i < program.nodes.size(); ++i) {
        if ((i & 0xff) == 0) check_cancelled(stop);
        const auto& node = program.nodes[i];
        if (node.block < 0 || node.block >= program.blocks.size()
            || !program.blocks[node.block].reachable) continue;
        Event event;
        event.ir_node = static_cast<quint32>(i);
        if (node.op == Op::Opaque || node.op == Op::CallOpaque) {
            event.kind = Event::UnknownWrite;
            result.unknown_store_seen = true;
        } else if (node.op == Op::Store) {
            const auto found = known.constFind(event.ir_node);
            if (found == known.cend()) {
                event.kind = Event::UnknownWrite;
                result.unknown_store_seen = true;
            } else {
                event.kind = Event::Write;
                event.offset = found->entry_rsp_offset_bytes;
                event.bytes = static_cast<quint32>(found->width_bits / 8);
            }
        } else if (node.op == Op::Load) {
            const auto found = known.constFind(event.ir_node);
            if (found == known.cend()) continue;
            event.kind = Event::Read;
            event.offset = found->entry_rsp_offset_bytes;
            event.bytes = static_cast<quint32>(found->width_bits / 8);
        } else {
            continue;
        }

        if (count++ >= max_events) {
            result.truncated = true;
            break;
        }
        result.blocks[node.block] << event;
    }
    return result;
}

bool solve_for_location(const Program& program, const Events& events,
                        const StackSlotCandidate& slot,
                        QVector<State>& incoming, std::stop_token stop) {
    QVector<State> outgoing(program.blocks.size());
    incoming.resize(program.blocks.size());

    for (int iteration = 0; iteration < max_iterations; ++iteration) {
        bool changed = false;
        for (qsizetype b = 0; b < program.blocks.size(); ++b) {
            if ((b & 0x1f) == 0) check_cancelled(stop);
            const auto& block = program.blocks[b];
            if (!block.reachable) continue;
            State next;
            if (b == program.entry) next.from_entry = true;

            for (qsizetype predecessor : block.predecessors) {
                if (predecessor >= 0 && predecessor < program.blocks.size()
                    && program.blocks[predecessor].reachable)
                    merge(next, outgoing[predecessor]);
            }
            State finished = next;
            for (const auto& event : events.blocks[b])
                transfer(finished, event, slot);
            if (!(incoming[b] == next) || !(outgoing[b] == finished)) {
                incoming[b] = std::move(next);
                outgoing[b] = std::move(finished);
                changed = true;
            }
        }
        if (!changed) return true;
    }
    return false; // No possibly stale reaching-def result is published.
}

} // namespace

StackValueFlowObservation observe(const Program& program,
                                  const StackFrameObservation& frame,
                                  std::stop_token stop) {
    StackValueFlowObservation result;
    result.warnings << QStringLiteral(
        "Reaching writes are possible modeled producers, not source variable "
        "lifetimes or a proof that a read has one runtime value.");

    if (program.architecture != Architecture::X64 || program.entry < 0
        || program.entry >= program.blocks.size()) {
        result.limited = true;
        result.warnings << QStringLiteral(
            "Storage-value flow requires a valid x86-64 CFG.");
        return result;
    }
    if (frame.limited || program.blocks.size() > max_blocks
        || frame.slots.size() > max_locations) {
        result.limited = true;
        result.warnings << QStringLiteral(
            "CFG or stack observation exceeds bounded analysis limits.");
        return result;
    }

    const Events events = collect_events(program, frame, stop);
    if (events.truncated) {
        result.limited = true;
        result.warnings << QStringLiteral(
            "Memory-event ceiling reached; no incomplete reaching-write "
            "claims were published.");
        return result;
    }
    result.has_unknown_aliases = events.unknown_store_seen
        || frame.unresolved_memory_accesses != 0
        || frame.unknown_machine_effects_present;

    QHash<quint32, qsizetype> generation_index;
    constexpr qsizetype max_generations = 512;
    for (const auto& slot : frame.slots) {
        for (const auto& access : slot.evidence) {
            if (!access.write || generation_index.contains(access.ir_node_index))
                continue;
            if (result.write_generations.size() >= max_generations) {
                result.limited = true;
                result.warnings << QStringLiteral(
                    "Write-generation reporting capped; no claim of complete "
                    "memory-write coverage.");
                break;
            }
            generation_index.insert(access.ir_node_index,
                                    result.write_generations.size());
            result.write_generations << StackWriteGeneration{
                access.instruction, access.ir_node_index,
                access.entry_rsp_offset_bytes,
                static_cast<quint32>(access.width_bits / 8), {}, false
            };
        }
    }

    QSet<quint32> recorded_reads;
    for (const auto& slot : frame.slots) {
        check_cancelled(stop);
        ++result.locations_examined;
        QVector<State> incoming;
        if (!solve_for_location(program, events, slot, incoming, stop)) {
            result.limited = true;
            result.warnings << QStringLiteral(
                "Reaching-write fixed point did not converge within budget; "
                "results for remaining storage locations were not published.");
            return result;
        }

        for (qsizetype b = 0; b < program.blocks.size(); ++b) {
            if ((b & 0x1f) == 0) check_cancelled(stop);
            if (!program.blocks[b].reachable) continue;
            State state = incoming[b];
            for (const Event& event : events.blocks[b]) {
                if (event.kind == Event::Read && exact(event, slot)
                    && !recorded_reads.contains(event.ir_node)) {
                    if (result.read_relations.size() >= max_results) {
                        result.limited = true;
                        result.warnings << QStringLiteral(
                            "Read-relation output was capped.");
                        return result;
                    }
                    recorded_reads.insert(event.ir_node);
                    StackReadRelation relation;
                    relation.read_ir_node = event.ir_node;
                    relation.read_instruction = program.nodes[event.ir_node].address;
                    relation.entry_rsp_offset_bytes = slot.entry_rsp_offset_bytes;
                    relation.size_bytes = slot.size_bytes;
                    relation.may_read_entry_memory = state.from_entry;
                    relation.unknown_memory_effect_may_intervene =
                        state.unknown_effect;
                    relation.ambiguous_overlapping_write = state.overlap;
                    result.has_unknown_aliases |= state.overlap
                        || state.unknown_effect;
                    QVector<quint32> writers = state.reaching_writes.values();
                    std::sort(writers.begin(), writers.end());
                    for (const quint32 writer : writers) {
                        if (relation.possible_source_writes.size()
                            >= max_source_addresses) {
                            relation.reporting_limited = true;
                            result.limited = true;
                            break;
                        }
                        if (writer < static_cast<quint32>(program.nodes.size())) {
                            relation.possible_source_writes
                                << program.nodes[writer].address;
                            relation.possible_source_write_nodes << writer;
                            const auto generation = generation_index.constFind(writer);
                            if (generation != generation_index.cend()) {
                                auto& item = result.write_generations[*generation];
                                if (!item.possible_reaching_reads.contains(
                                        relation.read_instruction))
                                    item.possible_reaching_reads
                                        << relation.read_instruction;
                                if (program.nodes[writer].block != b)
                                    item.may_cross_basic_blocks = true;
                            }
                        }
                    }
                    result.read_relations << std::move(relation);
                }
                transfer(state, event, slot);
            }
        }
    }
    result.dataflow_converged = true;
    if (result.has_unknown_aliases)
        result.warnings << QStringLiteral(
            "Unknown calls, unknown-address stores, and overlapping writes "
            "may alias stack storage. No cross-call value is assumed stable.");
    if (result.limited)
        result.warnings << QStringLiteral(
            "Some source-write lists were truncated; treat them as incomplete.");
    return result;
}

} // namespace reworkbench::stack_value_flow_analysis
