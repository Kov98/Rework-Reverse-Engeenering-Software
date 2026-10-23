#include "reworkbench/investigation.hpp"

#include <QMap>
#include <QSet>
#include <algorithm>
#include <bit>
#include <chrono>
#include <limits>
#include <map>

namespace reworkbench {
namespace {
struct Halt { QString reason; };
[[noreturn]] void halt(const QString& reason) { throw Halt{reason}; }
[[noreturn]] void invalid(const QString& reason) { throw std::runtime_error(reason.toStdString()); }
quint64 mask(int bits) { return bits == 64 ? std::numeric_limits<quint64>::max() : ((quint64{1} << bits) - 1); }
quint64 parse_value(const QJsonValue& value, const QString& name) {
    if (!value.isString()) invalid(name + QStringLiteral(" must be an unsigned hexadecimal or decimal string."));
    auto text = value.toString();
    const bool hexadecimal = text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive);
    if (hexadecimal) text.remove(0, 2);
    if (text.isEmpty() || text.size() > 20) invalid(name + QStringLiteral(" has invalid width."));
    for (const QChar c : text) {
        const auto u = c.unicode();
        if (!(u >= '0' && u <= '9') && !(hexadecimal && ((u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F')))) invalid(name + QStringLiteral(" is invalid."));
    }
    bool ok = false;
    const auto result = text.toULongLong(&ok, hexadecimal ? 16 : 10);
    if (!ok) invalid(name + QStringLiteral(" exceeds 64 bits."));
    return result;
}
int bounded_int(const QJsonObject& object, const QString& name, int fallback, int minimum, int maximum) {
    if (!object.contains(name)) return fallback;
    const auto value = object.value(name);
    if (!value.isDouble() || value.toDouble() < minimum || value.toDouble() > maximum || value.toDouble() != value.toInt())
        invalid(name + QStringLiteral(" is outside the supported integer range."));
    return value.toInt();
}
struct Slice { QString base; int offset; int width; };
std::optional<Slice> register_slice(QString name, bool x64) {
    name = name.toLower();
    static const QStringList roots{QStringLiteral("rax"), QStringLiteral("rcx"), QStringLiteral("rdx"), QStringLiteral("rbx"),
        QStringLiteral("rsp"), QStringLiteral("rbp"), QStringLiteral("rsi"), QStringLiteral("rdi")};
    static const QStringList legacy32{QStringLiteral("eax"), QStringLiteral("ecx"), QStringLiteral("edx"), QStringLiteral("ebx"),
        QStringLiteral("esp"), QStringLiteral("ebp"), QStringLiteral("esi"), QStringLiteral("edi")};
    static const QStringList legacy16{QStringLiteral("ax"), QStringLiteral("cx"), QStringLiteral("dx"), QStringLiteral("bx"),
        QStringLiteral("sp"), QStringLiteral("bp"), QStringLiteral("si"), QStringLiteral("di")};
    static const QStringList low8{QStringLiteral("al"), QStringLiteral("cl"), QStringLiteral("dl"), QStringLiteral("bl"),
        QStringLiteral("spl"), QStringLiteral("bpl"), QStringLiteral("sil"), QStringLiteral("dil")};
    static const QStringList high8{QStringLiteral("ah"), QStringLiteral("ch"), QStringLiteral("dh"), QStringLiteral("bh")};
    for (qsizetype i = 0; i < roots.size(); ++i) {
        if (x64 && name == roots[i]) return Slice{roots[i], 0, 64};
        if (name == legacy32[i]) return Slice{roots[i], 0, 32};
        if (name == legacy16[i]) return Slice{roots[i], 0, 16};
        if ((x64 || i < 4) && name == low8[i]) return Slice{roots[i], 0, 8};
        if (i < 4 && name == high8[i]) return Slice{roots[i], 8, 8};
    }
    if (x64) {
        for (int i = 8; i <= 15; ++i) {
            const auto root = QStringLiteral("r%1").arg(i);
            if (name == root) return Slice{root, 0, 64};
            if (name == root + QLatin1Char('d')) return Slice{root, 0, 32};
            if (name == root + QLatin1Char('w')) return Slice{root, 0, 16};
            if (name == root + QLatin1Char('b')) return Slice{root, 0, 8};
        }
    }
    return std::nullopt;
}
struct Value { quint64 value = 0; quint64 known = 0; };
struct Memory { quint64 address = 0; QByteArray bytes; bool writable = true; };
class Machine {
public:
    bool x64;
    quint64 ip = 0;
    quint64 next_ip = 0;
    QMap<QString, Value> regs;
    QMap<QString, bool> flags;
    QVector<Memory> memory;
    explicit Machine(bool wide) : x64(wide) {}
    quint64 address_mask() const { return mask(x64 ? 64 : 32); }
    Slice slice(const QString& name) const {
        const auto result = register_slice(name, x64);
        if (!result) halt(QStringLiteral("Unsupported register: ") + name);
        return *result;
    }
    quint64 read_register(const QString& name) const {
        if (name == QStringLiteral("rip") && x64) return next_ip;
        if (name == QStringLiteral("eip")) return next_ip & mask(32);
        const auto part = slice(name);
        const auto value = regs.value(part.base);
        const auto bits = mask(part.width) << part.offset;
        if ((value.known & bits) != bits) halt(QStringLiteral("Unknown input register: ") + name);
        return (value.value >> part.offset) & mask(part.width);
    }
    void write_register(const QString& name, quint64 value) {
        const auto part = slice(name);
        const auto bits = mask(part.width) << part.offset;
        auto& state = regs[part.base];
        state.value = (state.value & ~bits) | ((value & mask(part.width)) << part.offset);
        state.known |= bits;
        if (part.width == 32) {
            // Long-mode 32-bit writes zero the upper half. Outside long mode it
            // is outside the visible register set, but also safely canonicalized.
            state.value &= mask(32);
            state.known = std::numeric_limits<quint64>::max();
        }
    }
    quint64 effective_address(const Operand& op) const {
        quint64 address = static_cast<quint64>(op.displacement);
        if (!op.memory_base.isEmpty()) address += read_register(op.memory_base);
        if (!op.memory_index.isEmpty()) {
            if (op.memory_scale != 1 && op.memory_scale != 2 && op.memory_scale != 4 && op.memory_scale != 8) halt(QStringLiteral("Unsupported index scale."));
            address += read_register(op.memory_index) * static_cast<quint64>(op.memory_scale);
        }
        return address & address_mask();
    }
    const Memory& mapping(quint64 address, int count) const {
        for (const auto& region : memory) {
            if (address >= region.address && address - region.address <= static_cast<quint64>(region.bytes.size())
                && static_cast<quint64>(count) <= static_cast<quint64>(region.bytes.size()) - (address - region.address)) return region;
        }
        halt(QStringLiteral("Memory access is outside explicitly supplied mappings at ") + hex(address));
    }
    quint64 read_memory(quint64 address, int width) const {
        const auto& region = mapping(address, width / 8);
        const auto offset = static_cast<qsizetype>(address - region.address);
        quint64 result = 0;
        for (int i = 0; i < width / 8; ++i) result |= quint64{static_cast<quint8>(region.bytes[offset + i])} << (8 * i);
        return result;
    }
    void write_memory(quint64 address, int width, quint64 value) {
        const auto& found = mapping(address, width / 8);
        if (!found.writable) halt(QStringLiteral("Write to a read-only supplied mapping."));
        for (qsizetype index = 0; index < memory.size(); ++index) {
            if (memory.at(index).address != found.address) continue;
            const auto offset = static_cast<qsizetype>(address - found.address);
            // Take the index before mutable access detaches the QVector.
            auto& region = memory[index];
            for (int i = 0; i < width / 8; ++i) region.bytes[offset + i] = static_cast<char>(value >> (8 * i));
            return;
        }
        halt(QStringLiteral("Internal mapping lookup failed."));
    }
    int width(const Operand& op) const {
        const int result = op.kind == OperandKind::Register ? slice(op.reg).width : op.width_bits;
        if (result != 8 && result != 16 && result != 32 && result != 64) halt(QStringLiteral("Unsupported scalar operand width."));
        if (!x64 && result == 64) halt(QStringLiteral("64-bit scalar operation outside x64 is unsupported."));
        return result;
    }
    quint64 read(const Operand& op, int operation_width) const {
        switch (op.kind) {
        case OperandKind::Register: return read_register(op.reg) & mask(operation_width);
        case OperandKind::Immediate: return op.immediate & mask(operation_width);
        case OperandKind::Memory:
            if (width(op) != operation_width) halt(QStringLiteral("Mixed-width memory operand is unsupported."));
            return read_memory(effective_address(op), operation_width);
        default: halt(QStringLiteral("Unsupported operand."));
        }
    }
    void write(const Operand& op, quint64 value, int operation_width) {
        if (width(op) != operation_width) halt(QStringLiteral("Destination width mismatch."));
        if (op.kind == OperandKind::Register) write_register(op.reg, value);
        else if (op.kind == OperandKind::Memory) write_memory(effective_address(op), operation_width, value);
        else halt(QStringLiteral("Destination is not a supported register or memory operand."));
    }
    bool flag(const QString& name) const {
        if (!flags.contains(name)) halt(QStringLiteral("Branch requires unknown flag: ") + name);
        return flags.value(name);
    }
    void basic_flags(quint64 result, int width) {
        result &= mask(width);
        flags[QStringLiteral("zf")] = result == 0;
        flags[QStringLiteral("sf")] = (result & (quint64{1} << (width - 1))) != 0;
        flags[QStringLiteral("pf")] = (std::popcount(static_cast<quint8>(result)) % 2) == 0;
    }
    void arithmetic_flags(quint64 a, quint64 b, quint64 result, int width, bool subtraction) {
        const auto m = mask(width), sign = quint64{1} << (width - 1);
        a &= m; b &= m; result &= m;
        basic_flags(result, width);
        flags[QStringLiteral("cf")] = subtraction ? a < b : result < a;
        flags[QStringLiteral("of")] = subtraction ? ((a ^ b) & (a ^ result) & sign) != 0 : ((~(a ^ b)) & (a ^ result) & sign) != 0;
        flags[QStringLiteral("af")] = ((a ^ b ^ result) & 0x10) != 0;
    }
    bool branch(const QString& mnemonic) const {
        if (mnemonic == QStringLiteral("je") || mnemonic == QStringLiteral("jz")) return flag(QStringLiteral("zf"));
        if (mnemonic == QStringLiteral("jne") || mnemonic == QStringLiteral("jnz")) return !flag(QStringLiteral("zf"));
        if (mnemonic == QStringLiteral("ja") || mnemonic == QStringLiteral("jnbe")) return !flag(QStringLiteral("cf")) && !flag(QStringLiteral("zf"));
        if (mnemonic == QStringLiteral("jae") || mnemonic == QStringLiteral("jnb") || mnemonic == QStringLiteral("jnc")) return !flag(QStringLiteral("cf"));
        if (mnemonic == QStringLiteral("jb") || mnemonic == QStringLiteral("jc") || mnemonic == QStringLiteral("jnae")) return flag(QStringLiteral("cf"));
        if (mnemonic == QStringLiteral("jbe") || mnemonic == QStringLiteral("jna")) return flag(QStringLiteral("cf")) || flag(QStringLiteral("zf"));
        if (mnemonic == QStringLiteral("jg") || mnemonic == QStringLiteral("jnle")) return !flag(QStringLiteral("zf")) && flag(QStringLiteral("sf")) == flag(QStringLiteral("of"));
        if (mnemonic == QStringLiteral("jge") || mnemonic == QStringLiteral("jnl")) return flag(QStringLiteral("sf")) == flag(QStringLiteral("of"));
        if (mnemonic == QStringLiteral("jl") || mnemonic == QStringLiteral("jnge")) return flag(QStringLiteral("sf")) != flag(QStringLiteral("of"));
        if (mnemonic == QStringLiteral("jle") || mnemonic == QStringLiteral("jng")) return flag(QStringLiteral("zf")) || flag(QStringLiteral("sf")) != flag(QStringLiteral("of"));
        if (mnemonic == QStringLiteral("jo")) return flag(QStringLiteral("of"));
        if (mnemonic == QStringLiteral("jno")) return !flag(QStringLiteral("of"));
        if (mnemonic == QStringLiteral("js")) return flag(QStringLiteral("sf"));
        if (mnemonic == QStringLiteral("jns")) return !flag(QStringLiteral("sf"));
        if (mnemonic == QStringLiteral("jp") || mnemonic == QStringLiteral("jpe")) return flag(QStringLiteral("pf"));
        if (mnemonic == QStringLiteral("jnp") || mnemonic == QStringLiteral("jpo")) return !flag(QStringLiteral("pf"));
        halt(QStringLiteral("Unsupported branch condition: ") + mnemonic);
    }
    void execute(const Instruction& ins) {
        const auto op = ins.mnemonic.toLower();
        if (ins.operands_text.contains(QLatin1Char(':'))) halt(QStringLiteral("Segment overrides or segmented operands are unsupported."));
        // Capstone's operand records do not retain all prefix semantics. Reject
        // address/operand overrides and repeat/lock prefixes rather than guess.
        bool operand_override = false;
        for (qsizetype i = 0; i < ins.bytes.size(); ++i) {
            const auto byte = static_cast<quint8>(ins.bytes[i]);
            if (byte == 0x26 || byte == 0x2e || byte == 0x36 || byte == 0x3e || byte == 0x64 || byte == 0x65)
                halt(QStringLiteral("Segment prefixes are unsupported."));
            if (byte == 0x67 || byte == 0xf0 || byte == 0xf2 || byte == 0xf3) {
                if (op == QStringLiteral("endbr64") || op == QStringLiteral("endbr32")) break;
                halt(QStringLiteral("Address override, repeat or lock prefixes are unsupported."));
            }
            if (byte == 0x66) operand_override = true;
            if (byte != 0x66 && !(x64 && byte >= 0x40 && byte <= 0x4f)) break;
        }
        if (op == QStringLiteral("nop") || op == QStringLiteral("endbr64") || op == QStringLiteral("endbr32")) return;
        if (ins.flow == FlowKind::Call) halt(QStringLiteral("Calls require an explicit callee model; execution stopped before the call."));
        if (ins.flow == FlowKind::Return) halt(QStringLiteral("return"));
        if (ins.flow == FlowKind::Trap) halt(QStringLiteral("Trap or privileged operation."));
        if (ins.flow == FlowKind::Jump || ins.flow == FlowKind::ConditionalJump) {
            if (ins.flow == FlowKind::Jump && op != QStringLiteral("jmp")) halt(QStringLiteral("Unsupported jump form."));
            if (!ins.direct_target || ins.direct_target->space != QStringLiteral("ram")) halt(QStringLiteral("Indirect or non-RAM jump is unsupported."));
            if (ins.flow == FlowKind::Jump || branch(op)) next_ip = ins.direct_target->value & address_mask();
            return;
        }
        const auto& args = ins.operands;
        if (op == QStringLiteral("push") || op == QStringLiteral("pop")) {
            if (args.size() != 1) halt(QStringLiteral("Invalid stack operation shape."));
            const int bits = x64 ? 64 : 32;
            // 16-bit push/pop and pop/push SP addressing subtleties are rejected.
            if ((args[0].kind != OperandKind::Immediate && width(args[0]) != bits) || operand_override) halt(QStringLiteral("Unsupported stack operand width."));
            const auto sp_name = x64 ? QStringLiteral("rsp") : QStringLiteral("esp");
            const auto sp = read_register(sp_name);
            if (args[0].kind == OperandKind::Memory || (args[0].kind == OperandKind::Register && slice(args[0].reg).base == QStringLiteral("rsp"))) halt(QStringLiteral("Stack operands using memory/SP are not modeled."));
            if (op == QStringLiteral("push")) {
                const auto value = read(args[0], bits), new_sp = (sp - static_cast<quint64>(bits / 8)) & address_mask();
                write_memory(new_sp, bits, value); write_register(sp_name, new_sp);
            } else {
                if (args[0].kind != OperandKind::Register) halt(QStringLiteral("POP destination is unsupported."));
                const auto value = read_memory(sp, bits);
                write(args[0], value, bits); write_register(sp_name, (sp + static_cast<quint64>(bits / 8)) & address_mask());
            }
            return;
        }
        if (args.isEmpty() || args.size() > 2) halt(QStringLiteral("Unsupported instruction shape: ") + op);
        const int bits = width(args[0]);
        if (op == QStringLiteral("mov") || op == QStringLiteral("movabs")) {
            if (args.size() != 2) halt(QStringLiteral("MOV needs two operands."));
            if (args[1].kind == OperandKind::Register && width(args[1]) != bits) halt(QStringLiteral("MOV register width mismatch."));
            write(args[0], read(args[1], bits), bits); return;
        }
        if (op == QStringLiteral("lea")) {
            if (args.size() != 2 || args[0].kind != OperandKind::Register || args[1].kind != OperandKind::Memory) halt(QStringLiteral("Unsupported LEA shape."));
            write(args[0], effective_address(args[1]), bits); return;
        }
        if (args.size() == 2 && args[1].kind == OperandKind::Register && width(args[1]) != bits) halt(QStringLiteral("Mixed-width arithmetic is unsupported."));
        const bool same_register = args.size() == 2 && args[0].kind == OperandKind::Register && args[1].kind == OperandKind::Register && args[0].reg == args[1].reg;
        const auto a = (same_register && (op == QStringLiteral("xor") || op == QStringLiteral("sub"))) ? 0 : read(args[0], bits);
        if (op == QStringLiteral("not") || op == QStringLiteral("neg") || op == QStringLiteral("inc") || op == QStringLiteral("dec")) {
            if (args.size() != 1) halt(QStringLiteral("Unary operation needs one operand."));
            if (op == QStringLiteral("not")) { write(args[0], ~a, bits); return; }
            const auto result = op == QStringLiteral("neg") ? quint64{0} - a : op == QStringLiteral("inc") ? a + 1 : a - 1;
            const auto old_cf = flags.contains(QStringLiteral("cf")) ? std::optional<bool>{flags.value(QStringLiteral("cf"))} : std::nullopt;
            if (op == QStringLiteral("neg")) arithmetic_flags(0, a, result, bits, true);
            else arithmetic_flags(a, 1, result, bits, op == QStringLiteral("dec"));
            if (op != QStringLiteral("neg")) { if (old_cf) flags[QStringLiteral("cf")] = *old_cf; else flags.remove(QStringLiteral("cf")); }
            write(args[0], result, bits); return;
        }
        if (args.size() != 2) halt(QStringLiteral("Unsupported unary instruction: ") + op);
        const auto b = (same_register && (op == QStringLiteral("xor") || op == QStringLiteral("sub"))) ? 0 : read(args[1], bits);
        quint64 result = 0;
        if (op == QStringLiteral("add") || op == QStringLiteral("sub") || op == QStringLiteral("cmp")) {
            result = op == QStringLiteral("add") ? a + b : a - b;
            arithmetic_flags(a, b, result, bits, op != QStringLiteral("add"));
            if (op != QStringLiteral("cmp")) write(args[0], result, bits);
            return;
        }
        if (op == QStringLiteral("and") || op == QStringLiteral("or") || op == QStringLiteral("xor") || op == QStringLiteral("test")) {
            result = op == QStringLiteral("or") ? a | b : op == QStringLiteral("xor") ? a ^ b : a & b;
            basic_flags(result, bits); flags[QStringLiteral("cf")] = false; flags[QStringLiteral("of")] = false; flags.remove(QStringLiteral("af"));
            if (op != QStringLiteral("test")) write(args[0], result, bits);
            return;
        }
        halt(QStringLiteral("Unmodeled instruction: ") + op);
    }
};
} // namespace

QJsonObject emulate_function(const AnalysisSnapshot& snapshot, qsizetype function_index,
                             const QJsonObject& initial, std::stop_token stop) {
    if (function_index < 0 || function_index >= snapshot.functions.size()) invalid(QStringLiteral("Emulation function index is out of range."));
    if (snapshot.metadata.architecture != Architecture::X86 && snapshot.metadata.architecture != Architecture::X64)
        invalid(QStringLiteral("The scalar interpreter currently supports only x86 and x64."));
    if (snapshot.metadata.byte_order != ByteOrder::Little) invalid(QStringLiteral("The scalar interpreter requires little endian input."));
    const auto& function = snapshot.functions[function_index];
    if (function.instructions.size() > 4096) invalid(QStringLiteral("Emulation is limited to functions with at most 4096 instructions."));
    Machine machine(snapshot.metadata.architecture == Architecture::X64);
    const int max_steps = bounded_int(initial, QStringLiteral("max_steps"), 10000, 1, 100000);
    const int trace_limit = bounded_int(initial, QStringLiteral("trace_limit"), 256, 0, 2048);
    const int timeout_ms = bounded_int(initial, QStringLiteral("timeout_ms"), 2000, 1, 10000);
    if (initial.contains(QStringLiteral("registers")) && !initial.value(QStringLiteral("registers")).isObject()) invalid(QStringLiteral("registers must be an object."));
    const auto regs = initial.value(QStringLiteral("registers")).toObject();
    if (regs.size() > 80) invalid(QStringLiteral("Too many initial registers."));
    try {
        for (auto it = regs.begin(); it != regs.end(); ++it) {
            const auto part = machine.slice(it.key());
            const auto value = parse_value(it.value(), it.key());
            if ((value & ~mask(part.width)) != 0) invalid(QStringLiteral("Register value exceeds its width: ") + it.key());
            const auto before = machine.regs.value(part.base);
            const auto bits = mask(part.width) << part.offset;
            if (((before.value ^ (value << part.offset)) & before.known & bits) != 0) invalid(QStringLiteral("Conflicting initial register aliases."));
            // Initial descriptions constrain bits; unlike executing a MOV to
            // EAX, merely providing an initial EAX does not establish high RAX.
            machine.regs[part.base] = Value{(before.value & ~bits) | (value << part.offset), before.known | bits};
        }
    } catch (const Halt& error) { invalid(error.reason); }
    if (initial.contains(QStringLiteral("flags")) && !initial.value(QStringLiteral("flags")).isObject()) invalid(QStringLiteral("flags must be an object."));
    const auto flags = initial.value(QStringLiteral("flags")).toObject();
    const QSet<QString> allowed_flags{QStringLiteral("cf"), QStringLiteral("zf"), QStringLiteral("sf"), QStringLiteral("of"), QStringLiteral("pf"), QStringLiteral("af")};
    for (auto it = flags.begin(); it != flags.end(); ++it) {
        if (!allowed_flags.contains(it.key()) || !it.value().isBool()) invalid(QStringLiteral("Invalid initial flag."));
        machine.flags[it.key()] = it.value().toBool();
    }
    if (initial.contains(QStringLiteral("memory")) && !initial.value(QStringLiteral("memory")).isArray()) invalid(QStringLiteral("memory must be an array."));
    const auto memory = initial.value(QStringLiteral("memory")).toArray();
    if (memory.size() > 256) invalid(QStringLiteral("At most 256 memory mappings may be supplied."));
    qsizetype total_bytes = 0;
    for (const auto& value : memory) {
        check_cancelled(stop);
        if (!value.isObject()) invalid(QStringLiteral("A memory mapping must be an object."));
        const auto object = value.toObject();
        const auto address = parse_value(object.value(QStringLiteral("address")), QStringLiteral("memory address"));
        const auto encoded_value = object.value(QStringLiteral("bytes"));
        if (!encoded_value.isString()) invalid(QStringLiteral("Memory bytes must be hexadecimal."));
        const auto encoded = encoded_value.toString();
        if (encoded.isEmpty() || encoded.size() % 2 || encoded.size() > 2 * 1024 * 1024) invalid(QStringLiteral("Invalid memory mapping byte count."));
        for (const QChar c : encoded) {
            const auto u = c.unicode();
            if (!((u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F'))) invalid(QStringLiteral("Memory contains invalid hexadecimal bytes."));
        }
        auto data = QByteArray::fromHex(encoded.toLatin1());
        total_bytes += data.size();
        if (total_bytes > 1024 * 1024) invalid(QStringLiteral("Initial memory is limited to 1 MiB."));
        if (address > machine.address_mask() || static_cast<quint64>(data.size() - 1) > machine.address_mask() - address)
            invalid(QStringLiteral("Memory mapping exceeds the architecture address space."));
        if (object.contains(QStringLiteral("writable")) && !object.value(QStringLiteral("writable")).isBool()) invalid(QStringLiteral("Memory writable must be boolean."));
        machine.memory.push_back(Memory{address, std::move(data), object.value(QStringLiteral("writable")).toBool(true)});
    }
    std::sort(machine.memory.begin(), machine.memory.end(), [](const Memory& a, const Memory& b) { return a.address < b.address; });
    for (qsizetype i = 1; i < machine.memory.size(); ++i) {
        const auto& before = machine.memory[i - 1]; const auto& after = machine.memory[i];
        if (after.address - before.address < static_cast<quint64>(before.bytes.size())) invalid(QStringLiteral("Overlapping memory mappings are rejected."));
    }
    std::map<quint64, const Instruction*> instructions;
    for (const auto& ins : function.instructions) {
        if (ins.address.space != QStringLiteral("ram") || ins.bytes.isEmpty() || ins.bytes.size() > 15
            || ins.address.value > machine.address_mask() || static_cast<quint64>(ins.bytes.size() - 1) > machine.address_mask() - ins.address.value)
            invalid(QStringLiteral("Invalid instruction range in snapshot."));
        if (!instructions.emplace(ins.address.value, &ins).second) invalid(QStringLiteral("Duplicate instruction address in snapshot."));
    }
    machine.ip = initial.contains(QStringLiteral("entry")) ? parse_value(initial.value(QStringLiteral("entry")), QStringLiteral("entry")) : function.address.value;
    if (initial.contains(QStringLiteral("entry")) && !instructions.contains(machine.ip)) invalid(QStringLiteral("The requested entry is not a decoded instruction in this function."));
    QJsonArray trace;
    QString reason = QStringLiteral("step-limit");
    QString status = QStringLiteral("stopped");
    int steps = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (; steps < max_steps;) {
        check_cancelled(stop);
        if (std::chrono::steady_clock::now() >= deadline) { reason = QStringLiteral("time-limit"); break; }
        const auto found = instructions.find(machine.ip);
        if (found == instructions.end()) { reason = QStringLiteral("Control flow left the supplied function or reached an undecoded address."); break; }
        const auto& ins = *found->second;
        machine.next_ip = (machine.ip + static_cast<quint64>(ins.bytes.size())) & machine.address_mask();
        if (trace.size() < trace_limit) trace.push_back(QJsonObject{{QStringLiteral("address"), ins.address.text()}, {QStringLiteral("instruction"), ins.mnemonic + QLatin1Char(' ') + ins.operands_text}});
        // Instruction-atomic state: unsupported input, permissions and partial
        // writes cannot leave a half-executed instruction in the returned state.
        const auto before_regs = machine.regs;
        const auto before_flags = machine.flags;
        const auto before_memory = machine.memory; // implicitly shared QByteArrays
        try {
            machine.execute(ins);
            machine.ip = machine.next_ip;
            ++steps;
        } catch (const Halt& error) {
            machine.regs = before_regs; machine.flags = before_flags; machine.memory = before_memory;
            reason = error.reason;
            if (reason == QStringLiteral("return")) { status = QStringLiteral("returned"); ++steps; }
            break;
        }
    }
    QJsonObject registers, output_flags;
    for (auto it = machine.regs.cbegin(); it != machine.regs.cend(); ++it) {
        const auto visible_mask = machine.x64 ? mask(64) : mask(32);
        registers.insert(it.key(), QJsonObject{{QStringLiteral("value"), hex(it.value().value & visible_mask)},
            {QStringLiteral("known_mask"), hex(it.value().known & visible_mask)}, {QStringLiteral("fully_known"), (it.value().known & visible_mask) == visible_mask}});
    }
    for (auto it = machine.flags.cbegin(); it != machine.flags.cend(); ++it) output_flags.insert(it.key(), it.value());
    QJsonArray output_memory;
    for (const auto& region : machine.memory) output_memory.push_back(QJsonObject{{QStringLiteral("address"), hex(region.address)},
        {QStringLiteral("bytes"), QString::fromLatin1(region.bytes.toHex())}, {QStringLiteral("writable"), region.writable}});
    return {{QStringLiteral("kind"), QStringLiteral("bounded-scalar-interpreter")}, {QStringLiteral("status"), status},
        {QStringLiteral("reason"), reason}, {QStringLiteral("steps"), steps}, {QStringLiteral("pc"), hex(machine.ip)},
        {QStringLiteral("registers"), registers}, {QStringLiteral("flags"), output_flags}, {QStringLiteral("memory"), output_memory},
        {QStringLiteral("trace"), trace}, {QStringLiteral("trace_truncated"), steps > trace.size()},
        {QStringLiteral("function_partial"), function.partial},
        {QStringLiteral("warning"), QStringLiteral("Interprets decoded snapshot instructions only. Unknown inputs stop execution. No operating system, exceptions, floating point, SIMD, atomics, concurrency, or external calls are modeled. A return stops before reading a caller return address.")}};
}
} // namespace reworkbench
