#include "type_evidence_analysis.hpp"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <utility>

namespace reworkbench::type_evidence_analysis {
namespace {
using namespace decompiler_detail;
constexpr qsizetype max_ir_nodes_inspected = 32768;
constexpr qsizetype max_values_reported = 1536;
constexpr qsizetype max_clues_per_value = 12;

QString representation(const Type& type) {
    switch (type.kind) {
    case TypeKind::Bits: return QStringLiteral("bits");
    case TypeKind::Memory: return QStringLiteral("memory_token");
    case TypeKind::Architecture: return QStringLiteral("architecture_state");
    case TypeKind::OpaqueState: return QStringLiteral("opaque_state");
    }
    return QStringLiteral("unknown");
}

void add_clue(ValueTypeEvidence& value, const QString& kind,
              const QString& origin, const QString& confidence,
              const Node& witness, quint32 witness_id, const QString& detail) {
    if (value.clues.size() >= max_clues_per_value) {
        value.clues_truncated = true;
        return;
    }
    value.clues << TypeClue{
        kind, origin, confidence, witness.address, witness_id, detail
    };
}

} // namespace

TypeEvidenceObservation observe(const Program& program,
                                const std::optional<AbiProfile>& abi_candidate,
                                std::stop_token stop) {
    TypeEvidenceObservation report;
    report.total_ir_nodes = static_cast<quint32>(
        std::min(program.nodes.size(), qsizetype(std::numeric_limits<quint32>::max())));
    report.partial = program.incomplete || program.modeled != program.total;
    report.status = QStringLiteral("ir_type_clues_only");
    report.warnings << QStringLiteral(
        "IR widths describe bitvector storage, NOT original C/C++ types, "
        "pointer identity, signedness, floating point or ownership.");
    report.warnings << QStringLiteral(
        "confirmed_ir means the IR contains the recorded operation/width; "
        "it does not certify the original instruction semantics or a type.");

    if (program.nodes.size() > max_ir_nodes_inspected) {
        report.limited = true;
        report.partial = true;
        report.status = QStringLiteral("ir_budget_exceeded");
        report.warnings << QStringLiteral(
            "IR exceeded type-evidence inspection budget; no partial set "
            "is presented as a complete census.");
        return report;
    }

    QHash<Value, qsizetype> index;
    QSet<QString> abi_arguments;
    if (abi_candidate) {
        for (const auto& name : abi_candidate->integer_argument_registers)
            abi_arguments.insert(name);
        report.warnings << QStringLiteral(
            "ABI candidate registers are possible parameter locations, "
            "not proof that a source function uses any parameter.");
    }

    for (qsizetype i = 0; i < program.nodes.size(); ++i) {
        if ((i & 0xff) == 0) check_cancelled(stop);
        const Node& node = program.nodes[i];
        if (report.values.size() >= max_values_reported) {
            report.limited = true;
            report.partial = true;
            break;
        }
        ValueTypeEvidence evidence;
        evidence.ir_node = static_cast<quint32>(i);
        evidence.definition = node.address;
        evidence.ir_representation = representation(node.type);
        evidence.width_bits = node.type.kind == TypeKind::Bits ? node.type.bits : 0;
        add_clue(evidence, QStringLiteral("ir_representation"),
                 QStringLiteral("ir_definition"), QStringLiteral("confirmed_ir"),
                 node, static_cast<quint32>(i), evidence.ir_representation);
        if (node.type.kind == TypeKind::Bits && node.type.bits > 0)
            add_clue(evidence, QStringLiteral("bit_width"),
                     QStringLiteral("ir_definition"), QStringLiteral("confirmed_ir"),
                     node, static_cast<quint32>(i),
                     QString::number(node.type.bits));
        if (node.op == Op::Equal || node.op == Op::SignedLess
            || node.op == Op::UnsignedLess || node.op == Op::BooleanNot) {
            add_clue(evidence, QStringLiteral("predicate_result_candidate"),
                     QStringLiteral("ir_definition"), QStringLiteral("inferred"),
                     node, static_cast<quint32>(i),
                     QStringLiteral("comparison/boolean IR operator; source bool unverified"));
        }
        if (abi_candidate && node.op == Op::Input
            && abi_arguments.contains(node.text)) {
            add_clue(evidence, QStringLiteral("possible_abi_argument_register"),
                     QStringLiteral("abi_candidate"), QStringLiteral("inferred"),
                     node, static_cast<quint32>(i), node.text);
        }
        index.insert(static_cast<Value>(i), report.values.size());
        report.values << std::move(evidence);
    }

    // Operand-use clues are attached to their subject IR value. An arithmetic
    // signed operator is evidence of a signed INTERPRETATION AT THIS USE only;
    // it cannot establish the declared signedness of a source variable.
    const auto mark_operand = [&](Value operand, const Node& use,
                                  quint32 use_id, const QString& kind,
                                  const QString& detail) {
        const auto found = index.constFind(operand);
        if (found == index.cend()) return;
        auto& evidence = report.values[*found];
        if (kind == QStringLiteral("signed_use"))
            evidence.signed_operation_seen = true;
        else if (kind == QStringLiteral("unsigned_use"))
            evidence.unsigned_operation_seen = true;
        else if (kind == QStringLiteral("address_expression_use"))
            evidence.address_use_seen = true;
        add_clue(evidence, kind, QStringLiteral("operand_use"),
                 QStringLiteral("inferred"), use, use_id, detail);
    };

    for (qsizetype i = 0; i < program.nodes.size(); ++i) {
        if ((i & 0xff) == 0) check_cancelled(stop);
        const Node& use = program.nodes[i];
        const quint32 use_id = static_cast<quint32>(i);
        switch (use.op) {
        case Op::SignedLess:
            for (Value value : use.operands)
                mark_operand(value, use, use_id, QStringLiteral("signed_use"),
                             QStringLiteral("signed_comparison_at_this_use"));
            break;
        case Op::UnsignedLess:
            for (Value value : use.operands)
                mark_operand(value, use, use_id, QStringLiteral("unsigned_use"),
                             QStringLiteral("unsigned_comparison_at_this_use"));
            break;
        case Op::SignExtend:
            if (!use.operands.isEmpty())
                mark_operand(use.operands[0], use, use_id,
                             QStringLiteral("signed_use"),
                             QStringLiteral("sign_extension_at_this_use"));
            break;
        case Op::ZeroExtend:
            if (!use.operands.isEmpty())
                mark_operand(use.operands[0], use, use_id,
                             QStringLiteral("unsigned_use"),
                             QStringLiteral("zero_extension_at_this_use"));
            break;
        case Op::Load:
            if (use.operands.size() == 2)
                mark_operand(use.operands[1], use, use_id,
                             QStringLiteral("address_expression_use"),
                             QStringLiteral("read_pointer_operand_not_source_pointer_type"));
            break;
        case Op::Store:
            if (use.operands.size() == 3)
                mark_operand(use.operands[1], use, use_id,
                             QStringLiteral("address_expression_use"),
                             QStringLiteral("write_pointer_operand_not_source_pointer_type"));
            break;
        default: break;
        }
    }

    for (auto& item : report.values) {
        if (item.signed_operation_seen && item.unsigned_operation_seen) {
            item.interpretation_conflict = true;
            ++report.values_with_conflicts;
        }
        // In particular, bit width and pointer-shaped operands do not
        // imply a recoverable int/float/struct/bool/pointer declaration.
        item.source_type_status = QStringLiteral("unknown");
    }
    if (report.limited)
        report.warnings << QStringLiteral(
            "Value reporting was capped; absence of a clue is not "
            "evidence that an IR value lacks that use.");
    if (report.values_with_conflicts)
        report.warnings << QStringLiteral(
            "Some IR values have both signed and unsigned uses. "
            "Do not collapse them into a source signedness declaration.");
    report.warnings << QStringLiteral(
        "No debug-info type parser is connected here. Debug symbol "
        "names are not treated as confirmed type declarations.");
    return report;
}

} // namespace reworkbench::type_evidence_analysis

namespace reworkbench {

TypeEvidenceObservation observe_function_types(const AnalysisSnapshot& snapshot,
                                                qsizetype function_index,
                                                std::stop_token stop) {
    if (function_index < 0 || function_index >= snapshot.functions.size())
        throw std::out_of_range("Type observation function index out of range.");

    try {
        const auto program = decompiler_detail::lift(
            snapshot, snapshot.functions[function_index],
            DecompilationOptions{}, stop);
        return type_evidence_analysis::observe(
            program, suggest_abi(snapshot.metadata), stop);
    } catch (const decompiler_detail::Limit& exception) {
        TypeEvidenceObservation result;
        result.status = QStringLiteral("analysis_limited");
        result.partial = true;
        result.limited = true;
        result.warnings << QStringLiteral("Lift budget exceeded: %1")
            .arg(QString::fromUtf8(exception.what()));
        return result;
    }
}

} // namespace reworkbench
