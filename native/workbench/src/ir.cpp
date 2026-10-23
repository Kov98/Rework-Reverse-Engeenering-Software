#include "reworkbench/analysis.hpp"

#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <limits>

namespace reworkbench {
namespace {

struct Translation {
    QStringList statements;
    bool modeled = false;
};

QString literal(quint64 value, int width) {
    if (width > 0 && width < 64) value &= (quint64(1) << width) - 1;
    return hex(value);
}

QString single_line(QString text) {
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text;
}

QString quoted(QString text) {
    text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    text.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return QLatin1Char('"') + text + QLatin1Char('"');
}

bool scalar_width(int width) { return width == 8 || width == 16 || width == 32 || width == 64; }

bool general_register(const QString& name, Architecture architecture) {
    static const QRegularExpression x86(QStringLiteral("^(r(8|9|1[0-5])(b|w|d)?|[re]?(ax|bx|cx|dx|si|di|sp|bp)|[abcd][lh]|sil|dil|spl|bpl)$"));
    static const QRegularExpression arm64(QStringLiteral("^([xw]([0-9]|[12][0-9]|30)|sp|wsp|fp|lr|xzr|wzr)$"));
    if (architecture == Architecture::X86 || architecture == Architecture::X64) return x86.match(name).hasMatch();
    if (architecture == Architecture::Arm64) return arm64.match(name).hasMatch();
    return false;
}

bool x86_family(Architecture architecture) { return architecture == Architecture::X86 || architecture == Architecture::X64; }

int address_width(const Instruction& instruction, Architecture architecture) {
    bool override = false;
    for (const unsigned char byte : instruction.bytes) {
        if (byte == 0x67) override = true;
        else if (byte == 0x66 || byte == 0xf0 || byte == 0xf2 || byte == 0xf3 || byte == 0x2e || byte == 0x36 || byte == 0x3e || byte == 0x26 || byte == 0x64 || byte == 0x65) continue;
        else if (architecture == Architecture::X64 && byte >= 0x40 && byte <= 0x4f) continue;
        else break;
    }
    return architecture == Architecture::X64 ? (override ? 32 : 64) : (override ? 16 : 32);
}

std::optional<QString> effective_address(const Operand& operand, const Instruction& instruction,
                                         Architecture architecture) {
    if (!x86_family(architecture) || operand.kind != OperandKind::Memory || instruction.operands_text.contains(QLatin1Char(':'))) return std::nullopt;
    QStringList terms;
    if (!operand.memory_base.isEmpty()) {
        if (operand.memory_base == QStringLiteral("rip") || operand.memory_base == QStringLiteral("eip")) {
            if (static_cast<quint64>(instruction.bytes.size()) > std::numeric_limits<quint64>::max() - instruction.address.value) return std::nullopt;
            terms << hex(instruction.address.value + static_cast<quint64>(instruction.bytes.size()));
        } else {
            if (!general_register(operand.memory_base, architecture)) return std::nullopt;
            terms << QLatin1Char('%') + operand.memory_base;
        }
    }
    if (!operand.memory_index.isEmpty()) {
        if (!general_register(operand.memory_index, architecture) || (operand.memory_scale != 1 && operand.memory_scale != 2 && operand.memory_scale != 4 && operand.memory_scale != 8)) return std::nullopt;
        terms << QStringLiteral("(%1 * %2)").arg(QLatin1Char('%') + operand.memory_index).arg(operand.memory_scale);
    }
    if (operand.displacement != 0 || terms.isEmpty()) terms << literal(static_cast<quint64>(operand.displacement), address_width(instruction, architecture));
    return QStringLiteral("u%1(%2)").arg(address_width(instruction, architecture)).arg(terms.join(QStringLiteral(" + ")));
}

std::optional<QString> read_operand(const Operand& operand, int width,
                                    const Instruction& instruction, Architecture architecture) {
    if (!scalar_width(width)) return std::nullopt;
    if (operand.kind == OperandKind::Immediate) return QStringLiteral("u%1(%2)").arg(width).arg(literal(operand.immediate, width));
    if (operand.kind == OperandKind::Register && general_register(operand.reg, architecture) && operand.width_bits == width) {
        if (operand.reg == QStringLiteral("xzr") || operand.reg == QStringLiteral("wzr")) return QStringLiteral("u%1(0)").arg(width);
        return QLatin1Char('%') + operand.reg;
    }
    if (operand.kind == OperandKind::Memory && operand.width_bits == width) {
        if (auto address = effective_address(operand, instruction, architecture)) return QStringLiteral("load_le%1(%2)").arg(width).arg(*address);
    }
    return std::nullopt;
}

std::optional<QString> write_operand(const Operand& operand, const QString& value,
                                     const Instruction& instruction, Architecture architecture) {
    if (!scalar_width(operand.width_bits)) return std::nullopt;
    if (operand.kind == OperandKind::Register && general_register(operand.reg, architecture)) {
        if (operand.reg == QStringLiteral("xzr") || operand.reg == QStringLiteral("wzr")) return QStringLiteral("discard(%1);").arg(value);
        return QLatin1Char('%') + operand.reg + QStringLiteral(" := u%1(%2);").arg(operand.width_bits).arg(value);
    }
    if (operand.kind == OperandKind::Memory) {
        if (auto address = effective_address(operand, instruction, architecture)) return QStringLiteral("store_le%1(%2, %3);").arg(operand.width_bits).arg(*address, value);
    }
    return std::nullopt;
}

std::optional<QString> x86_condition(const QString& mnemonic) {
    // These expressions read the incoming machine flags. An earlier opaque
    // effect leaves those flags unknown; no comparison expression is invented.
    static const QMap<QString, QString> conditions = {
        { QStringLiteral("je"), QStringLiteral("ZF") }, { QStringLiteral("jz"), QStringLiteral("ZF") },
        { QStringLiteral("jne"), QStringLiteral("!ZF") }, { QStringLiteral("jnz"), QStringLiteral("!ZF") },
        { QStringLiteral("ja"), QStringLiteral("!CF && !ZF") }, { QStringLiteral("jnbe"), QStringLiteral("!CF && !ZF") },
        { QStringLiteral("jae"), QStringLiteral("!CF") }, { QStringLiteral("jnb"), QStringLiteral("!CF") }, { QStringLiteral("jnc"), QStringLiteral("!CF") },
        { QStringLiteral("jb"), QStringLiteral("CF") }, { QStringLiteral("jnae"), QStringLiteral("CF") }, { QStringLiteral("jc"), QStringLiteral("CF") },
        { QStringLiteral("jbe"), QStringLiteral("CF || ZF") }, { QStringLiteral("jna"), QStringLiteral("CF || ZF") },
        { QStringLiteral("jg"), QStringLiteral("!ZF && (SF == OF)") }, { QStringLiteral("jnle"), QStringLiteral("!ZF && (SF == OF)") },
        { QStringLiteral("jge"), QStringLiteral("SF == OF") }, { QStringLiteral("jnl"), QStringLiteral("SF == OF") },
        { QStringLiteral("jl"), QStringLiteral("SF != OF") }, { QStringLiteral("jnge"), QStringLiteral("SF != OF") },
        { QStringLiteral("jle"), QStringLiteral("ZF || (SF != OF)") }, { QStringLiteral("jng"), QStringLiteral("ZF || (SF != OF)") },
        { QStringLiteral("jo"), QStringLiteral("OF") }, { QStringLiteral("jno"), QStringLiteral("!OF") },
        { QStringLiteral("js"), QStringLiteral("SF") }, { QStringLiteral("jns"), QStringLiteral("!SF") },
        { QStringLiteral("jp"), QStringLiteral("PF") }, { QStringLiteral("jpe"), QStringLiteral("PF") },
        { QStringLiteral("jnp"), QStringLiteral("!PF") }, { QStringLiteral("jpo"), QStringLiteral("!PF") },
        { QStringLiteral("jcxz"), QStringLiteral("%cx == 0") }, { QStringLiteral("jecxz"), QStringLiteral("%ecx == 0") }, { QStringLiteral("jrcxz"), QStringLiteral("%rcx == 0") }
    };
    const auto found = conditions.constFind(mnemonic);
    if (found == conditions.cend()) return std::nullopt;
    return *found;
}

Translation translate(const Instruction& instruction, Architecture architecture) {
    Translation translated;
    const auto& operands = instruction.operands;
    const auto& mnemonic = instruction.mnemonic;
    const auto assembly = quoted(mnemonic + QLatin1Char(' ') + instruction.operands_text);
    auto opaque = [&](const QString& effects) {
        translated.statements << QStringLiteral("opaque_effects(%1, %2, %3);").arg(instruction.address.text(), quoted(effects), assembly);
    };
    if (mnemonic == QStringLiteral("nop") && instruction.flow == FlowKind::Normal && architecture != Architecture::Unknown) {
        translated.statements << QStringLiteral("nop;");
        translated.modeled = true;
        return translated;
    }
    if (x86_family(architecture) && instruction.direct_target) {
        if (instruction.flow == FlowKind::Jump && mnemonic == QStringLiteral("jmp")) {
            translated.statements << QStringLiteral("goto %1;").arg(instruction.direct_target->text());
            translated.modeled = true;
            return translated;
        }
        if (instruction.flow == FlowKind::ConditionalJump) {
            if (auto condition = x86_condition(mnemonic)) {
                if (static_cast<quint64>(instruction.bytes.size()) <= std::numeric_limits<quint64>::max() - instruction.address.value) {
                    const Address next{ instruction.address.space, instruction.address.value + static_cast<quint64>(instruction.bytes.size()) };
                    translated.statements << QStringLiteral("if (%1) goto %2; else goto %3;").arg(*condition, instruction.direct_target->text(), next.text());
                    translated.modeled = true;
                    return translated;
                }
            }
        }
    }
    if (instruction.flow != FlowKind::Normal) {
        QString description = QStringLiteral("control, registers, memory and architectural state");
        if (instruction.flow == FlowKind::Call) description = QStringLiteral("call target, return/link state, stack, registers and memory; ABI unknown");
        else if (instruction.flow == FlowKind::Return) description = QStringLiteral("return target, stack/link state and architectural state");
        opaque(description);
        return translated;
    }

    const bool plain_arm64 = architecture == Architecture::Arm64 && !instruction.operands_text.contains(QLatin1Char('[')) && !instruction.operands_text.contains(QLatin1Char('!')) && !instruction.operands_text.contains(QLatin1Char('.')) && instruction.operands_text.count(QLatin1Char(',')) == operands.size() - 1;
    if ((x86_family(architecture) || plain_arm64) && operands.size() == 2 && (mnemonic == QStringLiteral("mov") || (x86_family(architecture) && mnemonic == QStringLiteral("movabs")))) {
        const int width = operands[0].width_bits;
        if (auto value = read_operand(operands[1], width, instruction, architecture)) {
            if (auto assignment = write_operand(operands[0], *value, instruction, architecture)) {
                translated.statements << *assignment;
                translated.modeled = true;
                return translated;
            }
        }
    }
    if (x86_family(architecture) && mnemonic == QStringLiteral("lea") && operands.size() == 2 && operands[0].kind == OperandKind::Register && operands[1].kind == OperandKind::Memory) {
        if (auto address = effective_address(operands[1], instruction, architecture)) {
            if (auto assignment = write_operand(operands[0], *address, instruction, architecture)) {
                translated.statements << *assignment;
                translated.modeled = true;
                return translated;
            }
        }
    }

    static const QMap<QString, QString> x86_arithmetic = {
        { QStringLiteral("add"), QStringLiteral("add") }, { QStringLiteral("sub"), QStringLiteral("sub") },
        { QStringLiteral("and"), QStringLiteral("and") }, { QStringLiteral("or"), QStringLiteral("or") }, { QStringLiteral("xor"), QStringLiteral("xor") },
        { QStringLiteral("cmp"), QStringLiteral("sub") }, { QStringLiteral("test"), QStringLiteral("and") }
    };
    if (x86_family(architecture) && operands.size() == 2 && x86_arithmetic.contains(mnemonic)) {
        const int width = operands[0].width_bits;
        auto left = read_operand(operands[0], width, instruction, architecture);
        auto right = read_operand(operands[1], width, instruction, architecture);
        if (left && right) {
            const QString temporary = QStringLiteral("t_") + QString::number(instruction.address.value, 16);
            const QString expression = QStringLiteral("%1_u%2(%3, %4)").arg(x86_arithmetic.value(mnemonic)).arg(width).arg(*left, *right);
            if (mnemonic == QStringLiteral("cmp") || mnemonic == QStringLiteral("test")) {
                translated.statements << QStringLiteral("%1 := %2;").arg(temporary, expression);
                opaque(QStringLiteral("flags computed from ") + temporary);
                return translated;
            }
            if (auto assignment = write_operand(operands[0], temporary, instruction, architecture)) {
                translated.statements << QStringLiteral("%1 := %2;").arg(temporary, expression) << *assignment;
                // Deliberately not counted as fully modeled: no precise flag or
                // exception semantics are inferred from a mnemonic alone.
                opaque(QStringLiteral("flags for the arithmetic operation"));
                return translated;
            }
        }
    }

    static const QMap<QString, QString> arm64_arithmetic = {
        { QStringLiteral("add"), QStringLiteral("add") }, { QStringLiteral("sub"), QStringLiteral("sub") },
        { QStringLiteral("and"), QStringLiteral("and") }, { QStringLiteral("orr"), QStringLiteral("or") }, { QStringLiteral("eor"), QStringLiteral("xor") }
    };
    if (plain_arm64 && operands.size() == 3 && arm64_arithmetic.contains(mnemonic)) {
        const int width = operands[0].width_bits;
        auto left = read_operand(operands[1], width, instruction, architecture);
        auto right = read_operand(operands[2], width, instruction, architecture);
        if (left && right) {
            const QString expression = QStringLiteral("%1_u%2(%3, %4)").arg(arm64_arithmetic.value(mnemonic)).arg(width).arg(*left, *right);
            if (auto assignment = write_operand(operands[0], expression, instruction, architecture)) {
                translated.statements << *assignment;
                translated.modeled = true;
                return translated;
            }
        }
    }
    opaque(QStringLiteral("all instruction effects"));
    return translated;
}

} // namespace

RenderedFunction render_ir(const Function& function, Architecture architecture, std::stop_token stop) {
    check_cancelled(stop);
    RenderedFunction rendered;
    rendered.total_instructions = static_cast<quint32>(function.instructions.size());
    QStringList lines;
    lines << QStringLiteral("; Reworkbench own low-level IR v1 — partial semantics, not recovered C")
          << QStringLiteral("; uN and *_uN operations truncate modulo 2^N; load/store widths are bits.")
          << QStringLiteral("; Register names denote architectural slices; narrow writes preserve other bits,")
          << QStringLiteral("; except x86-64/AArch64 32-bit GPR writes zero-extend. AArch64 ZR reads zero; writes discard.")
          << QStringLiteral("; Model scope: normal nonfaulting scalar execution; x86 uses flat segments.")
          << QStringLiteral("; Segment overrides, flags arithmetic, ABI/stack calls, exceptions and atomics may be opaque.")
          << QStringLiteral("; An opaque effect prevents semantic propagation through the affected state.")
          << QStringLiteral("function %1 @ %2 {").arg(quoted(single_line(function.name)), function.address.text());
    QSet<quint64> block_starts;
    for (const auto& block : function.blocks) {
        check_cancelled(stop);
        block_starts.insert(block.address.value);
    }
    for (const auto& instruction : function.instructions) {
        check_cancelled(stop);
        if (block_starts.contains(instruction.address.value)) lines << QStringLiteral("  block %1:").arg(instruction.address.text());
        rendered.source_addresses.insert(int(lines.size()), instruction.address);
        lines << QStringLiteral("    ; %1  %2 %3").arg(instruction.address.text(), single_line(instruction.mnemonic), single_line(instruction.operands_text));
        const auto translation = translate(instruction, architecture);
        if (translation.modeled) ++rendered.modeled_instructions;
        for (const auto& statement : translation.statements) {
            rendered.source_addresses.insert(int(lines.size()), instruction.address);
            lines << QStringLiteral("    ") + statement;
        }
    }
    rendered.complete = rendered.total_instructions != 0 && rendered.modeled_instructions == rendered.total_instructions && !function.partial;
    lines << QStringLiteral("}")
          << QStringLiteral("; Fully modeled instructions in the stated abstraction: %1/%2.").arg(rendered.modeled_instructions).arg(rendered.total_instructions)
          << (rendered.complete ? QStringLiteral("; Coverage complete for this limited IR model; this is not an equivalence proof.") : QStringLiteral("; Partial: opaque effects and/or incomplete function discovery remain."));
    for (const auto& warning : function.warnings) {
        check_cancelled(stop);
        lines << QStringLiteral("; analysis: ") + single_line(warning);
    }
    rendered.text = lines.join(QLatin1Char('\n'));
    return rendered;
}

} // namespace reworkbench
