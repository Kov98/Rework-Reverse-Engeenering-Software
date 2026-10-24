#include "stack_abi_observation.hpp"

#include <QHash>
#include <QSet>

#include <bit>
#include <optional>

namespace reworkbench::stack_observation {
namespace {
using namespace decompiler_detail;

// The analysis is intentionally narrow: expressions must be an exact affine
// function of the *initial* RSP, under modeled 64-bit scalar operations.
// No speculation through unknown instructions, register clobbers or aliases.
constexpr qint64 maximum_displacement = 1024 * 1024;
constexpr qsizetype maximum_observations = 128;
constexpr qsizetype maximum_memory_visits = 8192;
constexpr qsizetype maximum_affine_depth = 128;

struct Affine {
    int rsp_factor = 0;
    qint64 displacement = 0;
    bool operator==(const Affine&) const = default;
};

bool plausible(const Affine& value) {
    return value.rsp_factor >= 0 && value.rsp_factor <= 1
        && value.displacement >= -maximum_displacement
        && value.displacement <= maximum_displacement;
}

class StackAffineResolver {
public:
    explicit StackAffineResolver(const Program& program, std::stop_token stop)
        : program_(program), stop_(stop) {}

    std::optional<Affine> resolve(Value id) {
        return visit(id, 0);
    }

private:
    std::optional<Affine> visit(Value id, qsizetype depth) {
        if (id == no_value || id >= static_cast<Value>(program_.nodes.size())
            || depth >= maximum_affine_depth) return std::nullopt;
        const auto cached = cache_.constFind(id);
        if (cached != cache_.cend()) return cached.value();
        if (visiting_.contains(id)) return std::nullopt;
        if ((cache_.size() & 0x7f) == 0) check_cancelled(stop_);
        visiting_.insert(id);

        const auto& node = program_.nodes[id];
        std::optional<Affine> output;
        if (node.type.kind == TypeKind::Bits && node.type.bits == 64) {
            switch (node.op) {
            case Op::Input:
                if (node.text == QStringLiteral("rsp")) output = Affine{1, 0};
                break;
            case Op::Constant:
                output = Affine{0, std::bit_cast<qint64>(node.immediate)};
                break;
            case Op::Add:
            case Op::Subtract:
                if (node.operands.size() == 2) {
                    const auto a = visit(node.operands[0], depth + 1);
                    const auto b = visit(node.operands[1], depth + 1);
                    if (a && b) {
                        const int sign = node.op == Op::Add ? 1 : -1;
                        // Prove that the displacement fits a signed 64-bit
                        // integer before arithmetic. Overflow must never
                        // fabricate an apparently small stack offset.
                        const qint64 lhs = a->displacement;
                        const qint64 rhs = b->displacement;
                        const qint64 hi = std::numeric_limits<qint64>::max();
                        const qint64 lo = std::numeric_limits<qint64>::min();
                        const bool overflows = sign == 1
                            ? ((rhs > 0 && lhs > hi - rhs)
                                || (rhs < 0 && lhs < lo - rhs))
                            : ((rhs < 0 && lhs > hi + rhs)
                                || (rhs > 0 && lhs < lo + rhs));
                        if (!overflows) {
                            const Affine combined{
                                a->rsp_factor + sign * b->rsp_factor,
                                sign == 1 ? lhs + rhs : lhs - rhs
                            };
                            if (plausible(combined)) output = combined;
                        }
                    }
                }
                break;
            case Op::Phi:
                if (!node.incoming.isEmpty() && node.incoming.size() <= 64) {
                    bool consistent = true;
                    std::optional<Affine> candidate;
                    for (const auto& incoming : node.incoming) {
                        const auto value = visit(incoming.value, depth + 1);
                        if (!value || (candidate && *value != *candidate)) {
                            consistent = false;
                            break;
                        }
                        candidate = value;
                    }
                    if (consistent) output = candidate;
                }
                break;
            default: break;
            }
        }
        visiting_.remove(id);
        if (output && !plausible(*output)) output.reset();
        cache_.insert(id, output);
        return output;
    }

    const Program& program_;
    std::stop_token stop_;
    QHash<Value, std::optional<Affine>> cache_;
    QSet<Value> visiting_;
};

bool valid_access_width(int bits) {
    return bits == 8 || bits == 16 || bits == 32 || bits == 64;
}

bool possible_stack_argument(qint64 offset, qint64 first_byte) {
    return offset >= first_byte && offset < first_byte + 8 * 32
        && (offset - first_byte) % 8 == 0;
}

QString observation_key(const AbiStackAccess& value) {
    return value.instruction.text() + QLatin1Char(':')
        + QString::number(value.stack_offset_bytes) + QLatin1Char(':')
        + QString::number(value.width_bits);
}
} // namespace

FrameAccesses frame_accesses(const Program& program, std::stop_token stop) {
    FrameAccesses result;
    if (program.architecture != Architecture::X64) return result;
    StackAffineResolver affine(program, stop);
    constexpr qsizetype maximum_frame_accesses = 2048;
    constexpr quint32 maximum_unresolved = 4096;
    qsizetype inspected = 0;

    for (qsizetype node_index = 0; node_index < program.nodes.size(); ++node_index) {
        check_cancelled(stop);
        const Node& node = program.nodes[node_index];
        if (node.block < 0 || node.block >= program.blocks.size()
            || !program.blocks[node.block].reachable) continue;
        if (node.op == Op::Opaque || node.op == Op::CallOpaque)
            result.opaque_effects_present = true;
        if (node.op != Op::Load && node.op != Op::Store) continue;
        if (++inspected > maximum_frame_accesses) {
            result.limited = true;
            break;
        }

        const bool is_write = node.op == Op::Store;
        const qsizetype required = is_write ? 3 : 2;
        const qsizetype pointer_index = 1;
        const qsizetype size_index = 2;
        int width = 0;
        if (is_write) {
            if (node.operands.size() == required) {
                const Value stored = node.operands[size_index];
                if (stored < static_cast<Value>(program.nodes.size()))
                    width = program.nodes[stored].type.bits;
            }
        } else {
            width = node.type.bits;
        }
        const auto address = node.operands.size() == required
            ? affine.resolve(node.operands[pointer_index]) : std::nullopt;
        if (!address || address->rsp_factor != 1 || !valid_access_width(width)) {
            if (result.unresolved_addresses < maximum_unresolved)
                ++result.unresolved_addresses;
            else result.limited = true;
            continue;
        }

        result.accesses.push_back(StackFrameReference{
            node.address, address->displacement, width, is_write,
            static_cast<quint32>(node_index), node.block
        });
    }
    return result;
}

Observation incoming_reads(const Program& program,
                           const AbiProfile& abi, std::stop_token stop) {
    Observation report;
    if (program.architecture != Architecture::X64) return report;
    StackAffineResolver affine(program, stop);
    QSet<QString> seen;
    for (const auto& node : program.nodes) {
        check_cancelled(stop);
        if (node.op != Op::Load || node.operands.size() != 2
            || node.block < 0 || node.block >= program.blocks.size()
            || !program.blocks[node.block].reachable
            || !valid_access_width(node.type.bits)) continue;
        const auto address = affine.resolve(node.operands[1]);
        if (!address || address->rsp_factor != 1
            || !possible_stack_argument(address->displacement,
                abi.stack_argument_start_from_entry_rsp)) continue;
        AbiStackAccess access{node.address, address->displacement, node.type.bits};
        const auto key = observation_key(access);
        if (seen.contains(key)) continue;
        if (report.accesses.size() >= maximum_observations) {
            report.limited = true;
            break;
        }
        seen.insert(key);
        report.accesses << access;
    }
    return report;
}

Observation outgoing_stores(const Program& program, const Node& call,
                            const AbiProfile& abi, std::stop_token stop) {
    Observation report;
    if (program.architecture != Architecture::X64 || call.op != Op::CallOpaque)
        return report;
    const auto stack_slot = program.slot_index.constFind(QStringLiteral("rsp"));
    const auto memory_slot = program.slot_index.constFind(QStringLiteral("mem"));
    if (stack_slot == program.slot_index.cend()
        || memory_slot == program.slot_index.cend()
        || *stack_slot >= call.operands.size()
        || *memory_slot >= call.operands.size()) {
        report.unknown_memory_path = true;
        return report;
    }
    StackAffineResolver affine(program, stop);
    const auto stack_at_call = affine.resolve(call.operands[*stack_slot]);
    if (!stack_at_call || stack_at_call->rsp_factor != 1) {
        report.unknown_memory_path = true;
        return report;
    }
    const qint64 first_stack_arg_from_call_rsp =
        qint64(abi.stack_argument_start_from_entry_rsp) - 8;
    if (first_stack_arg_from_call_rsp < 0) {
        report.unknown_memory_path = true;
        return report;
    }

    QVector<Value> pending{call.operands[*memory_slot]};
    QSet<Value> seen_values;
    QSet<QString> seen_accesses;
    while (!pending.isEmpty()) {
        if ((seen_values.size() & 0x7f) == 0) check_cancelled(stop);
        if (seen_values.size() >= maximum_memory_visits) {
            report.limited = true;
            break;
        }
        const Value id = pending.takeLast();
        if (id == no_value || id >= static_cast<Value>(program.nodes.size())
            || seen_values.contains(id)) {
            if (id == no_value || id >= static_cast<Value>(program.nodes.size()))
                report.unknown_memory_path = true;
            continue;
        }
        seen_values.insert(id);
        const auto& node = program.nodes[id];

        if (node.op == Op::Phi) {
            if (node.incoming.size() > 64) {
                report.limited = true;
                continue;
            }
            for (const auto& edge : node.incoming) pending << edge.value;
            continue;
        }
        if (node.op == Op::Input && node.text == QStringLiteral("mem"))
            continue; // End of modeled memory chain, without a prior write.

        if (node.op == Op::Store && node.operands.size() == 3) {
            const auto pointer = affine.resolve(node.operands[1]);
            const Value stored = node.operands[2];
            if (pointer && pointer->rsp_factor == 1
                && stored < static_cast<Value>(program.nodes.size())) {
                const int width = program.nodes[stored].type.bits;
                const qint64 offset = pointer->displacement - stack_at_call->displacement;
                if (valid_access_width(width)
                    && possible_stack_argument(offset, first_stack_arg_from_call_rsp)) {
                    const AbiStackAccess access{node.address, offset, width};
                    const auto key = observation_key(access);
                    if (!seen_accesses.contains(key)) {
                        if (report.accesses.size() >= maximum_observations) {
                            report.limited = true;
                            break;
                        }
                        seen_accesses.insert(key);
                        report.accesses << access;
                    }
                }
            }
            pending << node.operands[0];
            continue;
        }
        // A prior call, opaque memory write, unexpected memory op, or
        // broken IR terminates this path. We cannot prove what survived.
        report.unknown_memory_path = true;
    }
    return report;
}

} // namespace reworkbench::stack_observation
