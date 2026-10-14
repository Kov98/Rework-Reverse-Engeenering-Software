#include "decompiler_internal.hpp"

#include <QQueue>

#include <algorithm>
#include <array>

namespace reworkbench::decompiler_detail {
namespace {

constexpr Type bits(int width) { return { TypeKind::Bits, width }; }

std::optional<quint64> continuation(const Instruction& instruction) {
    if (instruction.bytes.isEmpty() || static_cast<quint64>(instruction.bytes.size()) >
        std::numeric_limits<quint64>::max() - instruction.address.value) return std::nullopt;
    return instruction.address.value + static_cast<quint64>(instruction.bytes.size());
}

struct Register { QString root; int width = 0; int offset = 0; };

std::optional<Register> register_info(QString name, Architecture architecture) {
    name = name.toLower();
    const bool wide = architecture == Architecture::X64;
    const std::array<QString, 8> roots = { QStringLiteral("ax"), QStringLiteral("bx"),
        QStringLiteral("cx"), QStringLiteral("dx"), QStringLiteral("si"),
        QStringLiteral("di"), QStringLiteral("sp"), QStringLiteral("bp") };
    for (qsizetype i = 0; i < static_cast<qsizetype>(roots.size()); ++i) {
        const auto& short_name = roots[static_cast<size_t>(i)];
        const QString root = (wide ? QStringLiteral("r") : QStringLiteral("e")) + short_name;
        if (wide && name == QStringLiteral("r") + short_name) return Register{ root, 64, 0 };
        if (name == QStringLiteral("e") + short_name) return Register{ root, 32, 0 };
        if (name == short_name) return Register{ root, 16, 0 };
        if (i < 4) {
            if (name == short_name.left(1) + QLatin1Char('l')) return Register{ root, 8, 0 };
            if (name == short_name.left(1) + QLatin1Char('h')) return Register{ root, 8, 8 };
        } else if (wide && name == short_name + QLatin1Char('l')) return Register{ root, 8, 0 };
    }
    if (wide) {
        for (int i = 8; i <= 15; ++i) {
            const QString root = QStringLiteral("r%1").arg(i);
            if (name == root) return Register{ root, 64, 0 };
            if (name == root + QLatin1Char('d')) return Register{ root, 32, 0 };
            if (name == root + QLatin1Char('w')) return Register{ root, 16, 0 };
            if (name == root + QLatin1Char('b')) return Register{ root, 8, 0 };
        }
    }
    return std::nullopt;
}

bool scalar_width(int width) { return width == 8 || width == 16 || width == 32 || width == 64; }

struct Prefixes { bool unsupported = false; bool operand_override = false; int address_width = 0; };
Prefixes prefixes(const Instruction& instruction, Architecture architecture) {
    Prefixes result;
    result.address_width = architecture == Architecture::X64 ? 64 : 32;
    for (const unsigned char byte : instruction.bytes) {
        if (byte == 0x67) result.address_width = architecture == Architecture::X64 ? 32 : 16;
        else if (byte == 0x66) result.operand_override = true;
        else if (byte == 0xf0 || byte == 0xf2 || byte == 0xf3) result.unsupported = true;
        else if (byte == 0x2e || byte == 0x36 || byte == 0x3e || byte == 0x26 || byte == 0x64 || byte == 0x65) {
            // Segment overrides and branch hints are deliberately not normalized
            // into the flat-memory model: their effect is visible as opaque.
            result.unsupported = true;
        } else if (architecture == Architecture::X64 && byte >= 0x40 && byte <= 0x4f) continue;
        else break;
    }
    return result;
}

void build_cfg(Program& program, const Function& function, const DecompilationOptions& options,
               std::stop_token stop) {
    if (function.instructions.size() > options.max_instructions)
        throw Limit("Instruction ceiling reached before decompilation.");
    QMap<quint64, qsizetype> ordered;
    QSet<quint64> leaders;
    for (qsizetype i = 0; i < function.instructions.size(); ++i) {
        check_cancelled(stop);
        const auto& instruction = function.instructions[i];
        if (instruction.address.space != function.address.space || instruction.bytes.isEmpty() ||
            !continuation(instruction) || ordered.contains(instruction.address.value))
            throw Limit("Invalid, duplicated or overflowing instruction addresses in the snapshot.");
        ordered.insert(instruction.address.value, i);
    }
    if (!ordered.contains(function.address.value))
        throw Limit("The function entry is not a recovered instruction.");
    leaders.insert(function.address.value);
    for (auto i = ordered.cbegin(); i != ordered.cend(); ++i) {
        const auto& instruction = function.instructions[i.value()];
        if (instruction.direct_target && instruction.direct_target->space == function.address.space &&
            (instruction.flow == FlowKind::Jump || instruction.flow == FlowKind::ConditionalJump) &&
            ordered.contains(instruction.direct_target->value)) leaders.insert(instruction.direct_target->value);
        auto following = i; ++following;
        if (following == ordered.cend()) continue;
        const auto next = *continuation(instruction);
        if (following.key() < next) throw Limit("Overlapping instructions cannot be given a single CFG.");
        if (following.key() != next || instruction.flow != FlowKind::Normal || !instruction.falls_through)
            leaders.insert(following.key());
    }
    for (auto i = ordered.cbegin(); i != ordered.cend(); ++i) {
        check_cancelled(stop);
        if (program.blocks.isEmpty() || leaders.contains(i.key())) {
            if (program.blocks.size() >= options.max_blocks) throw Limit("Basic-block ceiling reached.");
            Block block;
            block.address = { function.address.space, i.key() };
            program.block_at.insert(i.key(), program.blocks.size());
            program.blocks.push_back(std::move(block));
        }
        program.blocks.back().instructions.push_back(i.value());
    }
    program.entry = program.block_at.value(function.address.value, -1);
    for (qsizetype i = 0; i < program.blocks.size(); ++i) {
        auto& block = program.blocks[i];
        const auto& last = function.instructions[block.instructions.back()];
        const auto next = continuation(last);
        auto edge = [&](const std::optional<Address>& target) {
            if (!target || target->space != function.address.space) return;
            const auto found = program.block_at.constFind(target->value);
            if (found == program.block_at.cend() || block.successors.contains(*found)) return;
            block.successors << *found;
            program.blocks[*found].predecessors << i;
        };
        if (last.flow == FlowKind::ConditionalJump || last.flow == FlowKind::Jump) {
            block.taken = last.direct_target;
            edge(block.taken);
        }
        if (last.falls_through && next && last.flow != FlowKind::Jump &&
            last.flow != FlowKind::Return && last.flow != FlowKind::Trap) {
            block.not_taken = Address{ last.address.space, *next };
            edge(block.not_taken);
        }
    }
    QQueue<qsizetype> queue;
    queue.enqueue(program.entry);
    while (!queue.isEmpty()) {
        check_cancelled(stop);
        const qsizetype index = queue.dequeue();
        if (program.blocks[index].reachable) continue;
        program.blocks[index].reachable = true;
        for (const auto next : program.blocks[index].successors)
            if (!program.blocks[next].reachable) queue.enqueue(next);
    }
    for (const auto& block : program.blocks) {
        if (!block.reachable) {
            warn(program, QStringLiteral("Unreachable recovered instructions are excluded from executable pseudocode."));
            program.incomplete = true;
        }
    }
}

class Builder {
public:
    Builder(Program& program, const DecompilationOptions& options, std::stop_token stop)
        : program_(program), options_(options), stop_(stop) {}

    Value node(Op op, Type type, QVector<Value> operands = {}, QString text = {}, quint64 immediate = 0) {
        if ((program_.nodes.size() & 0x7f) == 0) check_cancelled(stop_);
        if (program_.nodes.size() >= options_.max_ir_nodes) throw Limit("Typed-IR node ceiling reached.");
        Node value;
        value.op = op; value.type = type; value.operands = std::move(operands);
        value.text = std::move(text); value.immediate = immediate;
        value.block = current_block_; value.address = current_address_;
        const auto id = static_cast<Value>(program_.nodes.size());
        program_.nodes.push_back(std::move(value));
        if (current_block_ >= 0 && op != Op::Phi) program_.blocks[current_block_].definitions << id;
        return id;
    }

    Value constant(quint64 value, int width) { return node(Op::Constant, bits(width), {}, {}, value & mask(width)); }
    Value binary(Op op, Value a, Value b, int width) { return node(op, bits(width), { a, b }); }
    Value unary(Op op, Value a, int width) { return node(op, bits(width), { a }); }
    Value extract(Value value, int offset, int width) {
        const QString key = QStringLiteral("%1:%2:%3").arg(value).arg(offset).arg(width);
        const auto existing = extracts_.constFind(key);
        if (existing != extracts_.cend()) return *existing;
        const auto id = unary(Op::Extract, value, width);
        program_.nodes[id].bit_offset = offset;
        extracts_.insert(key, id);
        return id;
    }
    Value adapt(Value value, int width, bool sign = false) {
        const int source_width = program_.nodes[value].type.bits;
        if (source_width == width) return value;
        return source_width < width ? unary(sign ? Op::SignExtend : Op::ZeroExtend, value, width) : extract(value, 0, width);
    }
    Value state(const QString& name) const { return state_[program_.slot_index.value(name)]; }
    void set(const QString& name, Value value) { state_[program_.slot_index.value(name)] = value; }

    Value read_register(const Register& reg) {
        const Value value = state(reg.root);
        return reg.width == program_.register_width && reg.offset == 0 ? value : extract(value, reg.offset, reg.width);
    }
    void write_register(const Register& reg, Value value) {
        value = adapt(value, reg.width);
        if (reg.width == program_.register_width) set(reg.root, value);
        else if (program_.register_width == 64 && reg.width == 32 && reg.offset == 0)
            set(reg.root, unary(Op::ZeroExtend, value, 64));
        else {
            const auto merged = node(Op::Insert, bits(program_.register_width), { state(reg.root), value });
            program_.nodes[merged].bit_offset = reg.offset;
            set(reg.root, merged);
        }
    }

    std::optional<Value> address(const Operand& operand, const Instruction& instruction) {
        if (operand.kind != OperandKind::Memory || instruction.operands_text.contains(QLatin1Char(':')))
            return std::nullopt;
        const int width = prefixes(instruction, program_.architecture).address_width;
        Value result = constant(static_cast<quint64>(operand.displacement), width);
        auto base = [&](const QString& name) -> std::optional<Value> {
            if (name == QStringLiteral("rip") || name == QStringLiteral("eip")) {
                if ((name == QStringLiteral("rip") && width != 64) ||
                    (name == QStringLiteral("eip") && width != 32)) return std::nullopt;
                return constant(*continuation(instruction), width);
            }
            const auto info = register_info(name, program_.architecture);
            if (!info || info->offset != 0 || info->width != width) return std::nullopt;
            return read_register(*info);
        };
        if (!operand.memory_base.isEmpty()) {
            const auto value = base(operand.memory_base);
            if (!value) return std::nullopt;
            result = binary(Op::Add, result, *value, width);
        }
        if (!operand.memory_index.isEmpty()) {
            if (operand.memory_index == QStringLiteral("rip") || operand.memory_index == QStringLiteral("eip"))
                return std::nullopt;
            const auto value = base(operand.memory_index);
            if (!value || (operand.memory_scale != 1 && operand.memory_scale != 2 &&
                operand.memory_scale != 4 && operand.memory_scale != 8)) return std::nullopt;
            result = binary(Op::Add, result,
                binary(Op::Multiply, *value, constant(static_cast<quint64>(operand.memory_scale), width), width), width);
        }
        return adapt(result, program_.register_width);
    }

    std::optional<Value> read(const Operand& operand, int width, const Instruction& instruction) {
        if (!scalar_width(width)) return std::nullopt;
        if (operand.kind == OperandKind::Immediate) return constant(operand.immediate, width);
        if (operand.kind == OperandKind::Register) {
            const auto reg = register_info(operand.reg, program_.architecture);
            if (!reg || reg->width != width || operand.width_bits != width) return std::nullopt;
            return read_register(*reg);
        }
        if (operand.kind == OperandKind::Memory && operand.width_bits == width) {
            const auto pointer = address(operand, instruction);
            if (pointer) return node(Op::Load, bits(width), { state(QStringLiteral("mem")), *pointer });
        }
        return std::nullopt;
    }

    bool writable(const Operand& operand) const {
        if (!scalar_width(operand.width_bits)) return false;
        if (operand.kind == OperandKind::Memory) return true;
        if (operand.kind != OperandKind::Register) return false;
        const auto reg = register_info(operand.reg, program_.architecture);
        return reg && reg->width == operand.width_bits;
    }
    bool write(const Operand& operand, Value value, const Instruction& instruction) {
        if (!writable(operand)) return false;
        if (operand.kind == OperandKind::Register) {
            write_register(*register_info(operand.reg, program_.architecture), value);
            return true;
        }
        const auto pointer = address(operand, instruction);
        if (!pointer) return false;
        set(QStringLiteral("mem"), node(Op::Store, { TypeKind::Memory, 0 },
            { state(QStringLiteral("mem")), *pointer, adapt(value, operand.width_bits) }));
        return true;
    }

    void result_flags(Value result, int width) {
        set(QStringLiteral("ZF"), binary(Op::Equal, result, constant(0, width), 1));
        set(QStringLiteral("SF"), extract(result, width - 1, 1));
        set(QStringLiteral("PF"), unary(Op::Parity, extract(result, 0, 8), 1));
    }
    void arithmetic_flags(Value left, Value right, Value result, int width, bool subtraction, bool keep_carry = false) {
        if (!keep_carry) set(QStringLiteral("CF"), binary(Op::UnsignedLess, subtraction ? left : result,
            subtraction ? right : left, 1));
        const auto difference = binary(Op::Xor, left, right, width);
        const auto changed_sign = binary(Op::Xor, left, result, width);
        const auto overflow = binary(Op::And, subtraction ? difference : unary(Op::Not, difference, width), changed_sign, width);
        set(QStringLiteral("OF"), extract(overflow, width - 1, 1));
        const auto auxiliary = binary(Op::Xor, difference, result, width);
        set(QStringLiteral("AF"), extract(auxiliary, 4, 1));
        result_flags(result, width);
    }
    void logical_flags(Value result, int width) {
        set(QStringLiteral("CF"), constant(0, 1));
        set(QStringLiteral("OF"), constant(0, 1));
        set(QStringLiteral("AF"), node(Op::Undefined, bits(1), {}, QStringLiteral("AF after logical instruction")));
        result_flags(result, width);
    }

    std::optional<Value> condition(QString mnemonic) {
        if (mnemonic.startsWith(QStringLiteral("cmov"))) mnemonic.remove(0, 4);
        else if (mnemonic.startsWith(QStringLiteral("set"))) mnemonic.remove(0, 3);
        else if (mnemonic.startsWith(QLatin1Char('j'))) mnemonic.remove(0, 1);
        const auto cf = state(QStringLiteral("CF")), zf = state(QStringLiteral("ZF"));
        const auto sf = state(QStringLiteral("SF")), of = state(QStringLiteral("OF"));
        auto not_ = [&](Value v) { return unary(Op::BooleanNot, v, 1); };
        // Replace a CMP flag expression only while every relevant flag still
        // has exactly the definition produced by that comparison. MOV/LEA can
        // intervene; an opaque effect or another flag writer breaks this proof.
        if (comparison_.valid && comparison_.cf == cf && comparison_.zf == zf &&
            comparison_.sf == sf && comparison_.of == of) {
            const auto left = comparison_.left, right = comparison_.right;
            if (mnemonic == QStringLiteral("e") || mnemonic == QStringLiteral("z")) return binary(Op::Equal, left, right, 1);
            if (mnemonic == QStringLiteral("ne") || mnemonic == QStringLiteral("nz")) return not_(binary(Op::Equal, left, right, 1));
            if (mnemonic == QStringLiteral("b") || mnemonic == QStringLiteral("nae") || mnemonic == QStringLiteral("c")) return binary(Op::UnsignedLess, left, right, 1);
            if (mnemonic == QStringLiteral("ae") || mnemonic == QStringLiteral("nb") || mnemonic == QStringLiteral("nc")) return not_(binary(Op::UnsignedLess, left, right, 1));
            if (mnemonic == QStringLiteral("a") || mnemonic == QStringLiteral("nbe")) return binary(Op::UnsignedLess, right, left, 1);
            if (mnemonic == QStringLiteral("be") || mnemonic == QStringLiteral("na")) return not_(binary(Op::UnsignedLess, right, left, 1));
            if (mnemonic == QStringLiteral("l") || mnemonic == QStringLiteral("nge")) return binary(Op::SignedLess, left, right, 1);
            if (mnemonic == QStringLiteral("ge") || mnemonic == QStringLiteral("nl")) return not_(binary(Op::SignedLess, left, right, 1));
            if (mnemonic == QStringLiteral("g") || mnemonic == QStringLiteral("nle")) return binary(Op::SignedLess, right, left, 1);
            if (mnemonic == QStringLiteral("le") || mnemonic == QStringLiteral("ng")) return not_(binary(Op::SignedLess, right, left, 1));
        }
        if (mnemonic == QStringLiteral("e") || mnemonic == QStringLiteral("z")) return zf;
        if (mnemonic == QStringLiteral("ne") || mnemonic == QStringLiteral("nz")) return not_(zf);
        if (mnemonic == QStringLiteral("a") || mnemonic == QStringLiteral("nbe")) return binary(Op::And, not_(cf), not_(zf), 1);
        if (mnemonic == QStringLiteral("ae") || mnemonic == QStringLiteral("nb") || mnemonic == QStringLiteral("nc")) return not_(cf);
        if (mnemonic == QStringLiteral("b") || mnemonic == QStringLiteral("nae") || mnemonic == QStringLiteral("c")) return cf;
        if (mnemonic == QStringLiteral("be") || mnemonic == QStringLiteral("na")) return binary(Op::Or, cf, zf, 1);
        const auto same_sign = binary(Op::Equal, sf, of, 1);
        if (mnemonic == QStringLiteral("g") || mnemonic == QStringLiteral("nle")) return binary(Op::And, not_(zf), same_sign, 1);
        if (mnemonic == QStringLiteral("ge") || mnemonic == QStringLiteral("nl")) return same_sign;
        if (mnemonic == QStringLiteral("l") || mnemonic == QStringLiteral("nge")) return not_(same_sign);
        if (mnemonic == QStringLiteral("le") || mnemonic == QStringLiteral("ng")) return binary(Op::Or, zf, not_(same_sign), 1);
        if (mnemonic == QStringLiteral("o")) return of;
        if (mnemonic == QStringLiteral("no")) return not_(of);
        if (mnemonic == QStringLiteral("s")) return sf;
        if (mnemonic == QStringLiteral("ns")) return not_(sf);
        if (mnemonic == QStringLiteral("p") || mnemonic == QStringLiteral("pe")) return state(QStringLiteral("PF"));
        if (mnemonic == QStringLiteral("np") || mnemonic == QStringLiteral("po")) return not_(state(QStringLiteral("PF")));
        QString count_register;
        if (mnemonic == QStringLiteral("cxz")) count_register = QStringLiteral("cx");
        else if (mnemonic == QStringLiteral("ecxz")) count_register = QStringLiteral("ecx");
        else if (mnemonic == QStringLiteral("rcxz")) count_register = QStringLiteral("rcx");
        if (!count_register.isEmpty()) {
            const auto reg = register_info(count_register, program_.architecture);
            if (reg) return binary(Op::Equal, read_register(*reg), constant(0, reg->width), 1);
        }
        return std::nullopt;
    }

    void opaque(const Instruction& instruction, const QString& reason) {
        comparison_.valid = false;
        const auto effect = node(Op::Opaque, { TypeKind::OpaqueState, 0 }, state_,
            instruction.mnemonic.left(64) + QLatin1Char(' ') + instruction.operands_text.left(512) + QStringLiteral(" | ") + reason);
        for (qsizetype i = 0; i < program_.slots.size(); ++i)
            state_[i] = node(Op::StateField, program_.slots[i].type, { effect }, program_.slots[i].name);
        program_.incomplete = true;
    }

    bool translate(const Instruction& instruction) {
        const auto pre = prefixes(instruction, program_.architecture);
        const auto& mnemonic = instruction.mnemonic;
        const auto& operands = instruction.operands;
        auto& block = program_.blocks[current_block_];
        if (pre.unsupported) return false;
        if (instruction.flow == FlowKind::Jump && mnemonic == QStringLiteral("jmp") && instruction.direct_target) {
            block.terminator = Terminator::Goto;
            return true;
        }
        if (instruction.flow == FlowKind::ConditionalJump) {
            if (const auto predicate = condition(mnemonic)) {
                block.condition = *predicate;
                block.terminator = Terminator::Conditional;
                return true;
            }
            return false;
        }
        if (instruction.flow == FlowKind::Return && mnemonic == QStringLiteral("ret") &&
            !pre.operand_override && (operands.isEmpty() || (operands.size() == 1 &&
                operands[0].kind == OperandKind::Immediate && operands[0].immediate <= 0xffff))) {
            const QString sp = program_.register_width == 64 ? QStringLiteral("rsp") : QStringLiteral("esp");
            const auto old_sp = state(sp);
            block.return_target = node(Op::Load, bits(program_.register_width), { state(QStringLiteral("mem")), old_sp });
            const quint64 adjustment = static_cast<quint64>(program_.register_width / 8) +
                (operands.isEmpty() ? 0 : operands[0].immediate);
            set(sp, binary(Op::Add, old_sp, constant(adjustment, program_.register_width), program_.register_width));
            block.terminator = Terminator::Return;
            return true;
        }
        if (instruction.flow != FlowKind::Normal) return false;
        if (mnemonic == QStringLiteral("nop")) return true;
        if ((mnemonic == QStringLiteral("mov") || mnemonic == QStringLiteral("movabs")) && operands.size() == 2 && writable(operands[0])) {
            const auto source = read(operands[1], operands[0].width_bits, instruction);
            return source && write(operands[0], *source, instruction);
        }
        if ((mnemonic == QStringLiteral("movzx") || mnemonic == QStringLiteral("movsx") || mnemonic == QStringLiteral("movsxd")) &&
            operands.size() == 2 && operands[0].kind == OperandKind::Register && writable(operands[0]) &&
            scalar_width(operands[1].width_bits) && operands[1].width_bits < operands[0].width_bits) {
            const auto source = read(operands[1], operands[1].width_bits, instruction);
            return source && write(operands[0], adapt(*source, operands[0].width_bits, mnemonic != QStringLiteral("movzx")), instruction);
        }
        if (mnemonic == QStringLiteral("lea") && operands.size() == 2 && operands[0].kind == OperandKind::Register && writable(operands[0])) {
            const auto source = address(operands[1], instruction);
            return source && write(operands[0], *source, instruction);
        }
        if (mnemonic.startsWith(QStringLiteral("set")) && operands.size() == 1 && operands[0].width_bits == 8 && writable(operands[0])) {
            const auto predicate = condition(mnemonic);
            return predicate && write(operands[0], adapt(*predicate, 8), instruction);
        }
        if (mnemonic.startsWith(QStringLiteral("cmov")) && operands.size() == 2 && operands[0].kind == OperandKind::Register &&
            writable(operands[0]) && operands[0].width_bits >= 16) {
            const int width = operands[0].width_bits;
            const auto predicate = condition(mnemonic);
            const auto old = read(operands[0], width, instruction), source = read(operands[1], width, instruction);
            if (!predicate || !old || !source) return false;
            return write(operands[0], node(Op::Select, bits(width), { *predicate, *source, *old }), instruction);
        }
        if ((mnemonic == QStringLiteral("not") || mnemonic == QStringLiteral("neg") || mnemonic == QStringLiteral("inc") || mnemonic == QStringLiteral("dec")) &&
            operands.size() == 1 && writable(operands[0])) {
            const int width = operands[0].width_bits;
            const auto left = read(operands[0], width, instruction);
            if (!left) return false;
            const auto right = constant(mnemonic == QStringLiteral("neg") ? 0 : 1, width);
            Value result;
            if (mnemonic == QStringLiteral("not")) result = unary(Op::Not, *left, width);
            else if (mnemonic == QStringLiteral("inc")) result = binary(Op::Add, *left, right, width);
            else if (mnemonic == QStringLiteral("neg")) result = binary(Op::Subtract, right, *left, width);
            else result = binary(Op::Subtract, *left, right, width);
            if (!write(operands[0], result, instruction)) return false;
            if (mnemonic != QStringLiteral("not")) arithmetic_flags(
                mnemonic == QStringLiteral("neg") ? right : *left,
                mnemonic == QStringLiteral("neg") ? *left : right,
                result, width, mnemonic != QStringLiteral("inc"), mnemonic == QStringLiteral("inc") || mnemonic == QStringLiteral("dec"));
            return true;
        }
        const bool arithmetic = mnemonic == QStringLiteral("add") || mnemonic == QStringLiteral("sub") ||
            mnemonic == QStringLiteral("cmp") || mnemonic == QStringLiteral("and") || mnemonic == QStringLiteral("or") ||
            mnemonic == QStringLiteral("xor") || mnemonic == QStringLiteral("test");
        if (arithmetic && operands.size() == 2 && scalar_width(operands[0].width_bits)) {
            const bool compare = mnemonic == QStringLiteral("cmp") || mnemonic == QStringLiteral("test");
            if (!compare && !writable(operands[0])) return false;
            const int width = operands[0].width_bits;
            const auto left = read(operands[0], width, instruction), right = read(operands[1], width, instruction);
            if (!left || !right) return false;
            const Op op = mnemonic == QStringLiteral("add") ? Op::Add :
                (mnemonic == QStringLiteral("sub") || mnemonic == QStringLiteral("cmp")) ? Op::Subtract :
                (mnemonic == QStringLiteral("and") || mnemonic == QStringLiteral("test")) ? Op::And :
                mnemonic == QStringLiteral("or") ? Op::Or : Op::Xor;
            const auto result = binary(op, *left, *right, width);
            if (!compare && !write(operands[0], result, instruction)) return false;
            if (op == Op::Add || op == Op::Subtract) arithmetic_flags(*left, *right, result, width, op == Op::Subtract);
            else logical_flags(result, width);
            if (mnemonic == QStringLiteral("cmp")) comparison_ = { true, *left, *right,
                state(QStringLiteral("CF")), state(QStringLiteral("ZF")), state(QStringLiteral("SF")), state(QStringLiteral("OF")) };
            return true;
        }
        if (mnemonic == QStringLiteral("push") && operands.size() == 1 && !pre.operand_override) {
            const int width = program_.register_width;
            const auto source = read(operands[0], width, instruction);
            if (!source) return false;
            const QString sp = width == 64 ? QStringLiteral("rsp") : QStringLiteral("esp");
            const auto next_sp = binary(Op::Subtract, state(sp), constant(static_cast<quint64>(width / 8), width), width);
            set(sp, next_sp);
            set(QStringLiteral("mem"), node(Op::Store, { TypeKind::Memory, 0 }, { state(QStringLiteral("mem")), next_sp, *source }));
            return true;
        }
        if (mnemonic == QStringLiteral("pop") && operands.size() == 1 && !pre.operand_override &&
            operands[0].kind == OperandKind::Register && operands[0].width_bits == program_.register_width && writable(operands[0])) {
            const int width = program_.register_width;
            const QString sp = width == 64 ? QStringLiteral("rsp") : QStringLiteral("esp");
            const auto source = node(Op::Load, bits(width), { state(QStringLiteral("mem")), state(sp) });
            set(sp, binary(Op::Add, state(sp), constant(static_cast<quint64>(width / 8), width), width));
            // POP ESP/RSP writes the popped value after the stack increment.
            return write(operands[0], source, instruction);
        }
        if (mnemonic == QStringLiteral("leave") && operands.isEmpty() && !pre.operand_override) {
            const int width = program_.register_width;
            const QString sp = width == 64 ? QStringLiteral("rsp") : QStringLiteral("esp");
            const QString bp = width == 64 ? QStringLiteral("rbp") : QStringLiteral("ebp");
            const auto old_bp = state(bp);
            set(sp, binary(Op::Add, old_bp, constant(static_cast<quint64>(width / 8), width), width));
            set(bp, node(Op::Load, bits(width), { state(QStringLiteral("mem")), old_bp }));
            return true;
        }
        return false;
    }

    void run(const Function& function) {
        const QStringList roots = program_.register_width == 64 ?
            QStringList{ QStringLiteral("rax"), QStringLiteral("rbx"), QStringLiteral("rcx"), QStringLiteral("rdx"),
                QStringLiteral("rsi"), QStringLiteral("rdi"), QStringLiteral("rsp"), QStringLiteral("rbp"),
                QStringLiteral("r8"), QStringLiteral("r9"), QStringLiteral("r10"), QStringLiteral("r11"),
                QStringLiteral("r12"), QStringLiteral("r13"), QStringLiteral("r14"), QStringLiteral("r15") } :
            QStringList{ QStringLiteral("eax"), QStringLiteral("ebx"), QStringLiteral("ecx"), QStringLiteral("edx"),
                QStringLiteral("esi"), QStringLiteral("edi"), QStringLiteral("esp"), QStringLiteral("ebp") };
        auto slot = [&](const QString& name, Type type) {
            program_.slot_index.insert(name, program_.slots.size());
            program_.slots.push_back({ name, type });
            program_.initial << node(Op::Input, type, {}, name);
        };
        for (const auto& root : roots) slot(root, bits(program_.register_width));
        for (const auto& flag : { "CF", "PF", "AF", "ZF", "SF", "OF" }) slot(QString::fromLatin1(flag), bits(1));
        slot(QStringLiteral("mem"), { TypeKind::Memory, 0 });
        slot(QStringLiteral("arch"), { TypeKind::Architecture, 0 });
        for (qsizetype b = 0; b < program_.blocks.size(); ++b) {
            auto& block = program_.blocks[b];
            if (!block.reachable) continue;
            current_block_ = b; current_address_ = block.address;
            for (const auto& description : program_.slots)
                block.input << node(Op::Phi, description.type, {}, description.name);
        }
        for (qsizetype b = 0; b < program_.blocks.size(); ++b) {
            auto& block = program_.blocks[b];
            if (!block.reachable) continue;
            check_cancelled(stop_);
            current_block_ = b; state_ = block.input;
            comparison_.valid = false;
            for (const auto index : block.instructions) {
                const auto& instruction = function.instructions[index];
                current_address_ = instruction.address;
                ++program_.total;
                if (translate(instruction)) ++program_.modeled;
                else {
                    opaque(instruction, instruction.flow == FlowKind::Call ? QStringLiteral("ABI, callee and side effects unknown") : QStringLiteral("semantics not modeled"));
                    if (instruction.flow == FlowKind::Jump && !instruction.indirect_targets.isEmpty())
                        warn(program_, QStringLiteral("Recovered indirect-branch candidates remain opaque transfers; no switch index or first-target condition is invented."));
                    if (instruction.flow == FlowKind::ConditionalJump) {
                        const auto branch = node(Op::Undefined, bits(1), { state(QStringLiteral("arch")) }, QStringLiteral("opaque branch outcome"));
                        block.condition = branch;
                        block.terminator = Terminator::Conditional;
                    } else if (instruction.flow == FlowKind::Jump) block.terminator = Terminator::Goto;
                    else if (instruction.flow == FlowKind::Return || instruction.flow == FlowKind::Trap)
                        block.terminator = Terminator::OpaqueExit;
                }
            }
            if (block.terminator == Terminator::None) {
                block.terminator = block.not_taken ? Terminator::Goto : Terminator::OpaqueExit;
                block.taken = block.not_taken;
                if (!block.not_taken) {
                    program_.incomplete = true;
                    warn(program_, QStringLiteral("A recovered path ends without a modeled return or transfer."));
                }
            }
            block.output = state_;
        }
        for (qsizetype b = 0; b < program_.blocks.size(); ++b) {
            auto& block = program_.blocks[b];
            if (!block.reachable) continue;
            for (qsizetype s = 0; s < program_.slots.size(); ++s) {
                auto& phi = program_.nodes[block.input[s]];
                if (b == program_.entry) phi.incoming.push_back({ -1, program_.initial[s] });
                for (const auto predecessor : block.predecessors)
                    if (program_.blocks[predecessor].reachable)
                        phi.incoming.push_back({ predecessor, program_.blocks[predecessor].output[s] });
                if (phi.incoming.isEmpty()) throw Limit("Reachable block has no SSA input.");
            }
        }
    }

private:
    Program& program_;
    const DecompilationOptions& options_;
    std::stop_token stop_;
    qsizetype current_block_ = -1;
    Address current_address_;
    QVector<Value> state_;
    QMap<QString, Value> extracts_;
    struct Comparison {
        bool valid = false;
        Value left = no_value, right = no_value;
        Value cf = no_value, zf = no_value, sf = no_value, of = no_value;
    } comparison_;
};

} // namespace

DecompilationOptions bounded_options(DecompilationOptions options) {
    options.max_instructions = std::clamp(options.max_instructions, quint32(1), quint32(4096));
    options.max_blocks = std::clamp(options.max_blocks, quint32(1), quint32(4096));
    options.max_ir_nodes = std::clamp(options.max_ir_nodes, quint32(256), quint32(262144));
    options.max_dataflow_iterations = std::clamp(options.max_dataflow_iterations, quint32(1), quint32(256));
    options.max_output_characters = std::clamp(options.max_output_characters, quint32(4096), quint32(4 * 1024 * 1024));
    return options;
}

Program lift(const AnalysisSnapshot& snapshot, const Function& function,
             const DecompilationOptions& options, std::stop_token stop) {
    Program program;
    program.architecture = snapshot.metadata.architecture;
    if (program.architecture != Architecture::X86 && program.architecture != Architecture::X64)
        throw Limit("Typed semantic decompilation currently supports scalar x86/x64 only.");
    if (snapshot.metadata.byte_order != ByteOrder::Little)
        throw Limit("x86/x64 decompilation requires little-endian instruction metadata.");
    program.register_width = program.architecture == Architecture::X64 ? 64 : 32;
    program.incomplete = function.partial;
    if (function.partial) warn(program, QStringLiteral("The recovered function is partial; omitted paths cannot be reconstructed by decompilation."));
    for (qsizetype i = 0; i < std::min(function.warnings.size(), qsizetype(16)); ++i)
        warn(program, function.warnings[i].left(512));
    build_cfg(program, function, options, stop);
    Builder builder(program, options, stop);
    builder.run(function);
    if (program.modeled != program.total)
        warn(program, QStringLiteral("Opaque instructions/calls invalidate all registers, flags, memory and other architectural state; no ABI is assumed."));
    return program;
}

} // namespace reworkbench::decompiler_detail
