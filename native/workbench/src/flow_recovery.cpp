#include "flow_recovery.hpp"
#include <QRegularExpression>
#include <limits>

namespace reworkbench::detail {
namespace {
QString canonical_register(const QString& name) {
    static const QMap<QString, QString> aliases{
        {"eax", "rax"}, {"ebx", "rbx"}, {"ecx", "rcx"}, {"edx", "rdx"},
        {"esi", "rsi"}, {"edi", "rdi"}, {"esp", "rsp"}, {"ebp", "rbp"}
    };
    if (aliases.contains(name)) return aliases.value(name);
    static const QRegularExpression extended(QStringLiteral("^r(8|9|1[0-5])d$"));
    if (extended.match(name).hasMatch()) return name.left(name.size() - 1);
    static const QRegularExpression full(QStringLiteral("^(r(ax|bx|cx|dx|si|di|sp|bp)|r(8|9|1[0-5]))$"));
    return full.match(name).hasMatch() ? name : QString{};
}
bool add(quint64 left, quint64 right, quint64& value) {
    if (right > std::numeric_limits<quint64>::max() - left) return false;
    value = left + right; return true;
}
std::optional<quint64> displaced(quint64 base, qint64 displacement) {
    quint64 result = 0;
    if (displacement >= 0) return add(base, quint64(displacement), result) ? std::optional<quint64>(result) : std::nullopt;
    const quint64 magnitude = quint64(-(displacement + 1)) + 1;
    return magnitude <= base ? std::optional<quint64>(base - magnitude) : std::nullopt;
}
}
StaticFlowResolver::StaticFlowResolver(const BinaryFile& file, const BinaryMetadata& metadata, const QSet<QString>& relocation_slots, bool allow_memory_pointers)
    : file_(file), metadata_(metadata), relocation_slots_(relocation_slots), allow_memory_pointers_(allow_memory_pointers) {}

std::optional<quint64> StaticFlowResolver::register_value(const QString& name) const {
    const auto canonical = canonical_register(name);
    const auto found = constants_.constFind(canonical);
    if (canonical.isEmpty() || found == constants_.cend()) return {};
    return name.startsWith(QLatin1Char('e')) || name.endsWith(QLatin1Char('d')) ? (*found & 0xffffffffULL) : *found;
}
std::optional<quint64> StaticFlowResolver::pointer(Address address) const {
    const quint64 width = metadata_.bits == 64 ? 8 : metadata_.bits == 32 ? 4 : 0;
    if (!width || !allow_memory_pointers_) return {};
    for (quint64 before = 1; before <= 7 && before <= address.value; ++before)
        if (relocation_slots_.contains(Address{address.space, address.value - before}.text())) return {};
    for (quint64 delta = 0; delta < width; ++delta) {
        if (delta > std::numeric_limits<quint64>::max() - address.value || relocation_slots_.contains(Address{address.space, address.value + delta}.text())) return {};
    }
    // Never dereference a loader-populated/import slot or writable mapping as
    // though its file bytes were a proven runtime function pointer.
    bool readonly = false;
    for (const auto& region : metadata_.regions) {
        if (region.start.space != address.space || address.value < region.start.value) continue;
        const quint64 delta = address.value - region.start.value;
        if (delta > region.virtual_size || width > region.virtual_size - delta) continue;
        if (region.writable) return {};
        readonly = true;
    }
    const auto offset = file_offset(metadata_, address, width);
    if (!readonly || !offset) return {};
    const auto bytes = file_.read(*offset, width);
    quint64 value = 0;
    for (quint64 index = 0; index < width; ++index) {
        const auto at = metadata_.byte_order == ByteOrder::Little ? index : width - index - 1;
        value |= quint64(static_cast<unsigned char>(bytes[qsizetype(at)])) << (index * 8);
    }
    return value;
}
std::optional<quint64> StaticFlowResolver::operand_address(const Operand& operand, const Instruction& instruction, bool with_index) const {
    if (operand.kind != OperandKind::Memory || instruction.operands_text.contains(QLatin1Char(':'))) return {};
    // Address-size overrides require wrapping arithmetic not represented here.
    for (unsigned char byte : instruction.bytes) {
        if (byte == 0x67) return {};
        if (byte == 0x66 || byte == 0xf0 || byte == 0xf2 || byte == 0xf3 || byte == 0x2e || byte == 0x36 || byte == 0x3e || byte == 0x26 || byte == 0x64 || byte == 0x65 || (metadata_.architecture == Architecture::X64 && byte >= 0x40 && byte <= 0x4f)) continue;
        break;
    }
    quint64 base = 0;
    if (operand.memory_base == QStringLiteral("rip") || operand.memory_base == QStringLiteral("eip")) {
        if (!add(instruction.address.value, quint64(instruction.bytes.size()), base)) return {};
    } else if (!operand.memory_base.isEmpty()) {
        const auto value = register_value(operand.memory_base); if (!value) return {}; base = *value;
    }
    if (with_index && !operand.memory_index.isEmpty()) {
        const auto value = register_value(operand.memory_index);
        if (!value || operand.memory_scale <= 0 || operand.memory_scale > 8 || *value > std::numeric_limits<quint64>::max() / quint64(operand.memory_scale)) return {};
        if (!add(base, *value * quint64(operand.memory_scale), base)) return {};
    }
    auto result = displaced(base, operand.displacement);
    if (result && metadata_.bits == 32) *result &= 0xffffffffULL;
    return result;
}
bool StaticFlowResolver::resolve(Instruction& instruction) {
    if ((metadata_.architecture != Architecture::X86 && metadata_.architecture != Architecture::X64) || instruction.direct_target ||
        (instruction.flow != FlowKind::Call && instruction.flow != FlowKind::Jump) || instruction.operands.size() != 1) return false;
    const auto& operand = instruction.operands.front();
    if (operand.width_bits != metadata_.bits) return false;
    std::optional<quint64> target;
    if (operand.kind == OperandKind::Register) target = register_value(operand.reg);
    else if (operand.kind == OperandKind::Memory) {
        if (const auto address = operand_address(operand, instruction, true)) target = pointer({instruction.address.space, *address});
        if (!target && instruction.flow == FlowKind::Jump && table_bound_ &&
            canonical_register(operand.memory_index) == table_bound_->first && operand.memory_scale == metadata_.bits / 8 && table_bound_->second < 256) {
            const auto base = operand_address(operand, instruction, false);
            if (!base) return false;
            QVector<Address> targets;
            for (quint64 index = 0; index <= table_bound_->second; ++index) {
                quint64 slot = 0;
                if (!add(*base, index * quint64(operand.memory_scale), slot)) return false;
                const auto value = pointer({instruction.address.space, slot});
                const Address address{instruction.address.space, value.value_or(0)};
                if (!value || !executable_address(metadata_, address) || !file_offset(metadata_, address)) return false;
                if (!targets.contains(address)) targets.push_back(address);
            }
            instruction.indirect_targets = std::move(targets);
            return !instruction.indirect_targets.isEmpty();
        }
    }
    const Address address{instruction.address.space, target.value_or(0)};
    if (!target || !executable_address(metadata_, address) || !file_offset(metadata_, address)) return false;
    instruction.indirect_targets = {address};
    // Even a read-only file pointer is an image assumption, not a literal
    // branch operand valid for every caller-provided machine/memory state.
    // Preserve the original indirect instruction for the lifter/interpreter.
    return true;
}
void StaticFlowResolver::observe(const Instruction& instruction) {
    if (metadata_.architecture != Architecture::X86 && metadata_.architecture != Architecture::X64) return;
    const auto previous_comparison = comparison_;
    comparison_.reset(); table_bound_.reset();
    const auto& operands = instruction.operands;
    if (instruction.flow == FlowKind::ConditionalJump && (instruction.mnemonic == QStringLiteral("ja") || instruction.mnemonic == QStringLiteral("jnbe")) && previous_comparison && instruction.direct_target) {
        table_bound_ = previous_comparison; return;
    }
    if (instruction.flow != FlowKind::Normal) { constants_.clear(); return; }
    if (instruction.mnemonic == QStringLiteral("cmp") && operands.size() == 2 && operands[0].kind == OperandKind::Register && operands[1].kind == OperandKind::Immediate && operands[0].width_bits == metadata_.bits && operands[1].immediate < 256) {
        const auto name = canonical_register(operands[0].reg);
        if (!name.isEmpty()) comparison_ = std::pair{name, operands[1].immediate};
        return;
    }
    if (instruction.mnemonic == QStringLiteral("nop")) return;
    if (operands.size() == 2 && operands[0].kind == OperandKind::Register && (operands[0].width_bits == 32 || operands[0].width_bits == 64)) {
        const auto name = canonical_register(operands[0].reg);
        if (!name.isEmpty()) {
            std::optional<quint64> value;
            if (instruction.mnemonic == QStringLiteral("mov") || instruction.mnemonic == QStringLiteral("movabs")) {
                if (operands[1].kind == OperandKind::Immediate) value = operands[1].immediate;
                else if (operands[1].kind == OperandKind::Register) value = register_value(operands[1].reg);
                else if (operands[1].kind == OperandKind::Memory && operands[1].width_bits == metadata_.bits)
                    if (const auto address = operand_address(operands[1], instruction, true)) value = pointer({instruction.address.space, *address});
            } else if (instruction.mnemonic == QStringLiteral("lea")) value = operand_address(operands[1], instruction, true);
            else if (instruction.mnemonic == QStringLiteral("xor") && operands[1].kind == OperandKind::Register && operands[0].reg == operands[1].reg) value = 0;
            if (value) {
                constants_[name] = operands[0].width_bits == 32 ? *value & 0xffffffffULL : *value;
                return;
            }
        }
    }
    // Unknown writes, calls, memory stores and partial writes kill all facts.
    constants_.clear();
}
}
