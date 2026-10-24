#include "hir_analysis.hpp"
#include "type_evidence_analysis.hpp"
#include "ir_semantic_audit.hpp"

#include <QHash>

#include <stdexcept>
#include <utility>

namespace reworkbench::hir_analysis {
namespace {
using namespace decompiler_detail;
constexpr qsizetype max_nodes = 8192;
constexpr qsizetype max_blocks = 768;
constexpr qsizetype max_clues_per_value = 12;

QString name_of(Op op) {
    switch (op) {
    case Op::Input: return QStringLiteral("input");
    case Op::Constant: return QStringLiteral("constant");
    case Op::Phi: return QStringLiteral("phi");
    case Op::Undefined: return QStringLiteral("undefined");
    case Op::Opaque: return QStringLiteral("opaque_instruction");
    case Op::CallOpaque: return QStringLiteral("opaque_call");
    case Op::StateField: return QStringLiteral("opaque_state_field");
    case Op::Extract: return QStringLiteral("extract");
    case Op::Insert: return QStringLiteral("insert");
    case Op::ZeroExtend: return QStringLiteral("zero_extend");
    case Op::SignExtend: return QStringLiteral("sign_extend");
    case Op::Add: return QStringLiteral("add_modular");
    case Op::Subtract: return QStringLiteral("subtract_modular");
    case Op::Multiply: return QStringLiteral("multiply_modular");
    case Op::And: return QStringLiteral("and");
    case Op::Or: return QStringLiteral("or");
    case Op::Xor: return QStringLiteral("xor");
    case Op::Not: return QStringLiteral("not");
    case Op::Equal: return QStringLiteral("equal");
    case Op::UnsignedLess: return QStringLiteral("unsigned_less");
    case Op::SignedLess: return QStringLiteral("signed_less");
    case Op::BooleanNot: return QStringLiteral("boolean_not");
    case Op::Select: return QStringLiteral("select");
    case Op::Parity: return QStringLiteral("even_parity");
    case Op::Load: return QStringLiteral("load_memory");
    case Op::Store: return QStringLiteral("store_memory");
    }
    return QStringLiteral("unsupported_ir_operation");
}

QString representation(TypeKind kind) {
    switch (kind) {
    case TypeKind::Bits: return QStringLiteral("bitvector");
    case TypeKind::Memory: return QStringLiteral("memory_token");
    case TypeKind::Architecture: return QStringLiteral("architecture_state");
    case TypeKind::OpaqueState: return QStringLiteral("opaque_state");
    }
    return QStringLiteral("unknown");
}

QString effect_of(Op op) {
    switch (op) {
    case Op::Load: return QStringLiteral("memory_read_may_fault");
    case Op::Store: return QStringLiteral("memory_write_may_fault");
    case Op::Opaque: return QStringLiteral("unknown_machine_state_barrier");
    case Op::CallOpaque: return QStringLiteral("unknown_call_state_barrier");
    case Op::StateField: return QStringLiteral("opaque_state_projection");
    case Op::Undefined: return QStringLiteral("unknown_value");
    default: return QStringLiteral("none");
    }
}

QString name_of(Terminator term) {
    switch (term) {
    case Terminator::None: return QStringLiteral("none");
    case Terminator::Goto: return QStringLiteral("goto");
    case Terminator::Conditional: return QStringLiteral("conditional");
    case Terminator::Return: return QStringLiteral("machine_state_return");
    case Terminator::OpaqueExit: return QStringLiteral("opaque_exit");
    }
    return QStringLiteral("unknown");
}

} // namespace

HirObservation observe(const Program& program,
                       const TypeEvidenceObservation& evidence,
                       std::stop_token stop) {
    HirObservation report;
    report.entry_block = program.entry;
    report.partial = program.incomplete || program.total != program.modeled
        || evidence.partial || evidence.limited;
    report.warnings << QStringLiteral(
        "HIR v0 preserves machine-IR nodes, bitvector widths, effect barriers "
        "and raw CFG; it does NOT recover source-language statements.");
    report.warnings << QStringLiteral(
        "Type clues describe machine IR uses only. No C/C++ declarations, "
        "stack-variable identity, signature or structured control flow "
        "is verified.");

    if (program.nodes.size() > max_nodes || program.blocks.size() > max_blocks) {
        report.limited = true;
        report.partial = true;
        report.warnings << QStringLiteral(
            "Structural HIR adaptation exceeds bounded node/block budget; "
            "no incomplete graph presented as a complete representation.");
        return report;
    }
    if (program.entry < 0 || program.entry >= program.blocks.size()) {
        report.partial = true;
        report.warnings << QStringLiteral("IR has no valid CFG entry block.");
    }

    // Link all type clues by original IR identity, never by guessed variable
    // names. A truncated TypeEvidence report may supply clues only for a
    // subset; missing ones remain explicitly incomplete.
    QHash<quint32, const ValueTypeEvidence*> typed;
    for (const auto& item : evidence.values)
        typed.insert(item.ir_node, &item);

    report.blocks.reserve(program.blocks.size());
    for (qsizetype i = 0; i < program.blocks.size(); ++i) {
        check_cancelled(stop);
        const Block& raw = program.blocks[i];
        HirBlock out;
        out.index = i;
        out.entry_address = raw.address;
        out.reachable = raw.reachable;
        out.predecessors = raw.predecessors;
        out.successors = raw.successors;
        out.terminator = name_of(raw.terminator);
        out.taken_address = raw.taken;
        out.not_taken_address = raw.not_taken;
        if (raw.condition != no_value)
            out.condition_value = raw.condition;
        if (raw.return_target != no_value)
            out.return_target_value = raw.return_target;

        const auto resolve_target = [&](const std::optional<Address>& address)
                -> std::optional<qsizetype> {
            if (!address) return std::nullopt;
            if (address->space != raw.address.space) return std::nullopt;
            const auto found = program.block_at.constFind(address->value);
            if (found == program.block_at.cend()) return std::nullopt;
            if (*found < 0 || *found >= program.blocks.size()) return std::nullopt;
            return *found;
        };
        out.resolved_taken_block = resolve_target(raw.taken);
        out.resolved_not_taken_block = resolve_target(raw.not_taken);
        out.target_resolution_incomplete =
            (raw.taken && !out.resolved_taken_block)
            || (raw.not_taken && !out.resolved_not_taken_block);
        if (out.target_resolution_incomplete) report.partial = true;

        for (Value value : raw.definitions)
            out.ordered_definitions << value;  // Includes valid and invalid IR refs.
        report.blocks << std::move(out);
    }

    const auto valid = [&](Value id) -> bool {
        return id != no_value
            && id < static_cast<Value>(program.nodes.size());
    };
    const auto record = [&](Value id) {
        if (!valid(id)) ++report.invalid_value_references;
    };

    for (qsizetype i = 0; i < program.nodes.size(); ++i) {
        if ((i & 0xff) == 0) check_cancelled(stop);
        const Node& raw = program.nodes[i];
        HirValue value;
        value.id = static_cast<quint32>(i);
        value.source_instruction = raw.address;
        value.owner_block = raw.block;
        value.operation = name_of(raw.op);
        value.representation = representation(raw.type.kind);
        value.width_bits = raw.type.kind == TypeKind::Bits ? raw.type.bits : 0;
        value.effect_kind = effect_of(raw.op);
        value.extract_insert_bit_offset = raw.bit_offset;
        value.original_ir_text = raw.text;
        if (raw.op == Op::Constant) value.constant = raw.immediate;
        if (raw.op == Op::Opaque || raw.op == Op::CallOpaque)
            report.has_opaque_barriers = true;

        for (Value operand : raw.operands) {
            record(operand);
            value.operands << operand;
        }
        for (const Incoming& input : raw.incoming) {
            record(input.value);
            if (input.predecessor < -1
                || input.predecessor >= program.blocks.size()) {
                report.partial = true;
            }
            value.phi_inputs << HirPhiInput{input.predecessor, input.value};
        }

        const auto found = typed.constFind(value.id);
        if (found != typed.cend()) {
            const ValueTypeEvidence& info = **found;
            value.source_type_status = info.source_type_status;
            value.conflicting_interpretations = info.interpretation_conflict;
            value.clues_incomplete = info.clues_truncated;
            for (const auto& clue : info.clues) {
                if (value.interpretation_clues.size() >= max_clues_per_value) {
                    value.clues_incomplete = true;
                    break;
                }
                value.interpretation_clues << clue.kind;
            }
        }

        if (raw.block == -1) report.synthetic_or_entry_values << value.id;
        else if (raw.block >= 0 && raw.block < report.blocks.size())
            report.blocks[raw.block].belonging_values << value.id;
        else {
            report.partial = true;
            ++report.invalid_value_references;
        }
        report.values << std::move(value);
    }

    for (const HirBlock& block : report.blocks) {
        check_cancelled(stop);
        for (quint32 value : block.ordered_definitions) record(value);
        if (block.condition_value) record(*block.condition_value);
        if (block.return_target_value) record(*block.return_target_value);
        for (qsizetype predecessor : block.predecessors)
            if (predecessor < 0 || predecessor >= report.blocks.size())
                report.partial = true;
        for (qsizetype successor : block.successors)
            if (successor < 0 || successor >= report.blocks.size())
                report.partial = true;
    }
    if (report.invalid_value_references)
        report.warnings << QStringLiteral(
            "Invalid/missing machine IR value references detected; "
            "do not perform HIR transformations.");
    report.partial |= report.invalid_value_references != 0;
    report.report_complete = !report.partial && !report.limited;
    return report;
}

} // namespace reworkbench::hir_analysis

namespace reworkbench {

HirObservation observe_function_hir(const AnalysisSnapshot& snapshot,
                                    qsizetype function_index,
                                    std::stop_token stop) {
    if (function_index < 0 || function_index >= snapshot.functions.size())
        throw std::out_of_range("HIR function index out of range.");
    try {
        const auto program = decompiler_detail::lift(
            snapshot, snapshot.functions[function_index],
            DecompilationOptions{}, stop);
        const auto types = type_evidence_analysis::observe(
            program, suggest_abi(snapshot.metadata), stop);
        auto hir = hir_analysis::observe(program, types, stop);
        hir.ir_audit = ir_semantic_audit::inspect(program, stop);
        if (!hir.ir_audit.structural_checks_passed) {
            hir.partial = true;
            hir.report_complete = false;
            hir.warnings << QStringLiteral(
                "Static machine-IR audit did not certify all structural "
                "invariants. HIR rewrites must remain disabled.");
        }
        return hir;
    } catch (const decompiler_detail::Limit& error) {
        HirObservation result;
        result.limited = true;
        result.partial = true;
        result.warnings << QStringLiteral("IR lift budget exceeded: %1")
            .arg(QString::fromUtf8(error.what()));
        return result;
    }
}

} // namespace reworkbench
