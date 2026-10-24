#include "reworkbench/function_abi.hpp"
#include "decompiler_internal.hpp"
#include "stack_abi_observation.hpp"
#include "stack_frame_analysis.hpp"
#include "stack_value_flow_analysis.hpp"
#include "memory_ssa_analysis.hpp"
#include "memory_region_analysis.hpp"
#include "type_evidence_analysis.hpp"

#include <QSet>

#include <algorithm>
#include <stdexcept>

namespace reworkbench {
namespace {

using namespace decompiler_detail;

// Traverse only the proven, modeled part of the value graph. In particular,
// Op::Opaque consumes the entire machine state as an implementation detail;
// treating those operands as real argument reads would invent parameters.
template <typename Visitor>
void modeled_ancestors(const Program& program, Value root, Visitor&& visitor,
                       std::stop_token stop) {
    QVector<Value> pending{root};
    QSet<Value> seen;
    while (!pending.isEmpty()) {
        if ((seen.size() & 0x7f) == 0) check_cancelled(stop);
        const Value id = pending.takeLast();
        if (id == no_value || id >= static_cast<Value>(program.nodes.size())
            || seen.contains(id)) continue;
        seen.insert(id);
        const Node& node = program.nodes[id];
        if (node.op == Op::Opaque || node.op == Op::CallOpaque || node.op == Op::StateField
            || node.op == Op::Undefined) continue;
        visitor(node);
        for (const Value operand : node.operands) pending << operand;
        for (const Incoming& incoming : node.incoming) pending << incoming.value;
    }
}

bool has_modeled_producer(const Program& program, Value value, std::stop_token stop) {
    bool produced = false;
    modeled_ancestors(program, value, [&](const Node& node) {
        if (node.op != Op::Input && node.op != Op::Phi
            && !node.address.space.isEmpty() && node.block >= 0)
            produced = true;
    }, stop);
    return produced;
}

QVector<Address> bounded_producers(const Program& program, Value value,
                                   qsizetype maximum, std::stop_token stop) {
    QVector<Address> producers;
    QSet<QString> visited;
    modeled_ancestors(program, value, [&](const Node& node) {
        if (node.op == Op::Input || node.op == Op::Phi || node.block < 0) return;
        const auto key = node.address.text();
        if (visited.contains(key)) return;
        visited.insert(key);
        if (producers.size() < maximum) producers << node.address;
    }, stop);
    return producers;
}

void note_observable_inputs(const Program& program, Value value,
                            const QSet<QString>& allowed, QSet<QString>& found,
                            std::stop_token stop) {
    modeled_ancestors(program, value, [&](const Node& node) {
        if (node.op == Op::Input && allowed.contains(node.text))
            found.insert(node.text);
    }, stop);
}

QStringList ordered_observations(const QStringList& abi_order, const QSet<QString>& found) {
    QStringList result;
    for (const auto& name : abi_order)
        if (found.contains(name)) result << name;
    return result;
}

} // namespace

FunctionAbiObservation observe_function_abi(const AnalysisSnapshot& snapshot,
                                            qsizetype function_index,
                                            const std::optional<AbiProfile>& explicit_profile,
                                            std::stop_token stop) {
    if (function_index < 0 || function_index >= snapshot.functions.size())
        throw std::out_of_range("ABI observation function index out of range.");
    FunctionAbiObservation result;
    const Function& function = snapshot.functions[function_index];
    result.total_instructions = static_cast<quint32>(
        std::min(function.instructions.size(),
                 qsizetype(std::numeric_limits<quint32>::max())));
    result.partial = function.partial;
    if (explicit_profile && (snapshot.metadata.architecture != Architecture::X64
                             || snapshot.metadata.bits != 64)) {
        result.status = QStringLiteral("unsupported_architecture");
        result.warnings << QStringLiteral(
            "An explicitly chosen x86-64 ABI cannot be applied to this architecture.");
        return result;
    }
    const auto profile = explicit_profile ? explicit_profile : suggest_abi(snapshot.metadata);
    if (!profile) {
        result.warnings << QStringLiteral(
            "No supported ABI hint; no calling-convention inference attempted.");
        return result;
    }
    result.profile_id = profile->id;
    result.status = explicit_profile ? QStringLiteral("user_selected_candidate")
                                     : QStringLiteral("candidate_only");

    try {
        const auto program = decompiler_detail::lift(
            snapshot, function, DecompilationOptions{}, stop);
        result.modeled_instructions = program.modeled;
        result.partial = result.partial || program.incomplete
            || program.modeled != program.total;
        result.warnings = program.warnings;
        result.frame = stack_frame_analysis::observe(program, *profile, stop);
        result.value_flow = stack_value_flow_analysis::observe(program, result.frame, stop);
        result.memory_dependence = summarize_memory_dependences(result.frame, result.value_flow);
        result.memory_ssa = memory_ssa_analysis::observe(program, stop);
        result.memory_regions = memory_region_analysis::observe(program, snapshot.metadata, stop);
        result.type_evidence = type_evidence_analysis::observe(program, profile, stop);
        if (result.frame.partial || result.type_evidence.limited
            || result.memory_regions.limited
            || !result.memory_regions.complete || result.memory_ssa.limited
            || !result.memory_ssa.complete || result.memory_dependence.limited || result.value_flow.limited
            || !result.value_flow.dataflow_converged) result.partial = true;
        const auto incoming = stack_observation::incoming_reads(program, *profile, stop);
        result.possible_incoming_stack_reads = incoming.accesses;
        result.incoming_stack_observation_limited = incoming.limited;
        if (incoming.limited) {
            result.partial = true;
            result.warnings << QStringLiteral(
                "Incoming stack-access observations were capped; report is incomplete.");
        }
        result.warnings << QStringLiteral(
            "File-format ABI is a candidate, not proof. Observations are not "
            "source parameters, return types, or validated calling conventions.");

        QSet<QString> allowed;
        for (const auto& reg : profile->integer_argument_registers) allowed.insert(reg);
        QSet<QString> observed_inputs;
        const auto slot = [&](const QString& name) -> qsizetype {
            const auto it = program.slot_index.constFind(name);
            return it == program.slot_index.cend() ? -1 : *it;
        };

        // Only modeled observable effects, not every live-in machine register.
        // Phi cycles are safe because modeled_ancestors tracks visited values.
        for (const auto& block : program.blocks) {
            check_cancelled(stop);
            if (!block.reachable) continue;

            if (block.terminator == Terminator::Conditional)
                note_observable_inputs(program, block.condition, allowed, observed_inputs, stop);

            for (const Value definition : block.definitions) {
                if (definition >= static_cast<Value>(program.nodes.size())) continue;
                const Node& node = program.nodes[definition];
                if (node.op != Op::Store) continue;
                // Store operands are [memory_state, address, stored_value].
                // The memory token is not an argument read by itself.
                for (qsizetype i = 1; i < node.operands.size(); ++i)
                    note_observable_inputs(program, node.operands[i],
                                           allowed, observed_inputs, stop);
            }

            if (block.terminator == Terminator::Return) {
                for (const auto& reg : profile->integer_return_registers) {
                    const qsizetype s = slot(reg);
                    if (s < 0 || s >= block.output.size()) continue;
                    const Value value = block.output[s];
                    // An unmodified input register must not be called a return.
                    if (!has_modeled_producer(program, value, stop)) continue;
                    if (!result.possible_integer_return_writes.contains(reg))
                        result.possible_integer_return_writes << reg;
                    note_observable_inputs(program, value,
                                           allowed, observed_inputs, stop);
                }
            }
        }

        result.observed_integer_input_dependencies =
            ordered_observations(profile->integer_argument_registers, observed_inputs);

        // Index the opaque pre-call state once, rather than rescanning the
        // entire IR for every call in a large binary.
        QMap<QString, QString> recovered_functions;
        for (const auto& recovered : snapshot.functions)
            recovered_functions.insert(recovered.address.text(), recovered.name);
        QMap<QString, const Node*> call_boundaries;
        for (const auto& node : program.nodes)
            if (node.op == Op::CallOpaque && node.block >= 0)
                call_boundaries.insert(node.address.text(), &node);

        // A call stays opaque in the existing lifter. We can record the IR
        // version at the call boundary, but *not* turn those registers into
        // definite arguments or preserve unproven callee-saved registers.
        for (const auto& instruction : function.instructions) {
            check_cancelled(stop);
            if (instruction.flow != FlowKind::Call) continue;
            if (result.calls.size() >= 512) {
                result.warnings << QStringLiteral(
                    "Callsite report capped at 512 sites; other sites omitted.");
                result.partial = true;
                break;
            }
            AbiCallObservation call;
            call.instruction = instruction.address;
            call.target = instruction.direct_target;
            call.effects = conservative_call_effects(profile);
            if (call.target) {
                const auto found = recovered_functions.constFind(call.target->text());
                if (found != recovered_functions.cend()) {
                    call.target_in_snapshot = true;
                    call.recovered_target_name = *found;
                }
            }

            const Node* boundary = call_boundaries.value(instruction.address.text(), nullptr);
            if (boundary && boundary->text.startsWith(instruction.mnemonic)) {
                const auto outgoing = stack_observation::outgoing_stores(
                    program, *boundary, *profile, stop);
                call.possible_outgoing_stack_stores = outgoing.accesses;
                call.stack_observation_limited = outgoing.limited;
                call.stack_memory_path_unknown = outgoing.unknown_memory_path;
                if (outgoing.limited) result.partial = true;
                for (const auto& reg : profile->integer_argument_registers) {
                    const qsizetype index = slot(reg);
                    if (index < 0 || index >= boundary->operands.size()) continue;
                    const auto producers = bounded_producers(
                        program, boundary->operands[index], 6, stop);
                    if (producers.isEmpty()) continue;
                    call.possible_argument_producers << AbiRegisterProvenance{reg, producers};
                }
            }
            result.calls << std::move(call);
        }

        if (result.partial)
            result.warnings << QStringLiteral(
                "Analysis is partial or contains opaque instructions; "
                "negative findings are not evidence that an argument is unused.");
    } catch (const decompiler_detail::Limit& error) {
        result.partial = true;
        result.status = QStringLiteral("analysis_limited");
        result.warnings << QString::fromUtf8(error.what());
        result.warnings << QStringLiteral(
            "Machine-state lift did not finish; no signature inferred.");
    }
    return result;
}

} // namespace reworkbench
