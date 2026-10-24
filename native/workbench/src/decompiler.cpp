#include "reworkbench/decompiler.hpp"
#include "decompiler_internal.hpp"

#include <QRegularExpression>
#include <QPair>

#include <algorithm>

namespace reworkbench {
namespace decompiler_detail {
namespace {

QString type_name(Type type) {
    switch (type.kind) {
    case TypeKind::Bits: return QStringLiteral("u%1").arg(type.bits);
    case TypeKind::Memory: return QStringLiteral("memory_state");
    case TypeKind::Architecture: return QStringLiteral("architecture_state");
    case TypeKind::OpaqueState: return QStringLiteral("opaque_state");
    }
    return QStringLiteral("unknown_type");
}

QString escaped(QString text) {
    text = text.left(1024);
    text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    text.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    text.replace(QChar(0), QStringLiteral("\\0"));
    return QLatin1Char('"') + text + QLatin1Char('"');
}

QString comment(QString text) {
    text = text.left(400);
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QStringLiteral("*/"), QStringLiteral("* /"));
    return text;
}

QString function_name(const Function& function) {
    QString name = function.name.left(160);
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    if (name.isEmpty() || name.front().isDigit()) name.prepend(QStringLiteral("fn_"));
    return name + QStringLiteral("_at_") + QString::number(function.address.value, 16);
}

class Writer {
public:
    explicit Writer(quint32 limit) : limit_(limit) {}
    void line(const QString& value = {}, const std::optional<Address>& source = {}) {
        if (truncated_) return;
        if (value.size() + 1 > limit_ - text_.size()) { truncated_ = true; return; }
        if (source) addresses_.insert(lines_, *source);
        text_ += value;
        text_ += QLatin1Char('\n');
        lines_ += int(value.count(QLatin1Char('\n'))) + 1;
    }
    QString take() {
        if (truncated_) {
            const QString marker = QStringLiteral("\n/* OUTPUT LIMIT: remaining text omitted; this listing is incomplete. */\n");
            const qsizetype retained = std::max(qsizetype(0), qsizetype(limit_) - marker.size());
            if (text_.size() > retained) text_.truncate(retained);
            // The retained prefix may end halfway through a previously written
            // instruction line. Only complete original lines retain their origin.
            auto cut = addresses_.lowerBound(int(text_.count(QLatin1Char('\n'))));
            while (cut != addresses_.end()) cut = addresses_.erase(cut);
            text_ += marker;
        }
        return std::move(text_);
    }
    bool truncated() const { return truncated_; }
    const QMap<int, Address>& addresses() const { return addresses_; }
private:
    QString text_;
    QMap<int, Address> addresses_;
    int lines_ = 0;
    qsizetype limit_;
    bool truncated_ = false;
};

struct Fact {
    enum Kind { Bottom, Constant, Varying } kind = Bottom;
    quint64 value = 0;
    bool operator==(const Fact&) const = default;
};

struct Dataflow {
    QVector<Value> alias;
    QVector<Fact> facts;
    bool converged = false;
};

Value resolve(const QVector<Value>& aliases, Value value) {
    // Every update aliases a root to a different root. The guard also contains
    // malformed internal graphs should a future transformation violate that rule.
    for (qsizetype n = 0; n <= aliases.size(); ++n) {
        if (value >= aliases.size()) throw Limit("Invalid SSA value reference.");
        if (aliases[value] == value) return value;
        value = aliases[value];
    }
    throw Limit("Cycle in SSA aliases.");
}

Value compressed_root(QVector<Value>& aliases, Value value) {
    const auto root = resolve(aliases, value);
    while (aliases[value] != value) {
        const auto next = aliases[value];
        aliases[value] = root;
        value = next;
    }
    return root;
}

Fact combine(Fact a, Fact b) {
    if (a.kind == Fact::Bottom) return b;
    if (b.kind == Fact::Bottom) return a;
    if (a.kind == Fact::Varying || b.kind == Fact::Varying || a.value != b.value)
        return { Fact::Varying, 0 };
    return a;
}

Fact evaluate(const Program& program, const Node& node, const Dataflow& flow) {
    auto fact = [&](Value value) { return flow.facts[resolve(flow.alias, value)]; };
    if (node.op == Op::Constant) return { Fact::Constant, node.immediate };
    if (node.op == Op::Phi) {
        Fact result;
        for (const auto& edge : node.incoming) result = combine(result, fact(edge.value));
        return result;
    }
    if (node.type.kind != TypeKind::Bits || node.op == Op::Input || node.op == Op::Undefined ||
        node.op == Op::StateField || node.op == Op::Load) return { Fact::Varying, 0 };
    if (node.op == Op::Select && node.operands.size() == 3) {
        const auto condition = fact(node.operands[0]);
        if (condition.kind == Fact::Constant) return fact(node.operands[condition.value ? 1 : 2]);
        const auto left = fact(node.operands[1]), right = fact(node.operands[2]);
        if (left.kind == Fact::Constant && right.kind == Fact::Constant && left.value == right.value) return left;
        return condition.kind == Fact::Bottom ? Fact{} : Fact{ Fact::Varying, 0 };
    }
    if (node.operands.size() == 2 && resolve(flow.alias, node.operands[0]) == resolve(flow.alias, node.operands[1])) {
        if (node.op == Op::Xor || node.op == Op::Subtract || node.op == Op::UnsignedLess || node.op == Op::SignedLess)
            return { Fact::Constant, 0 };
        if (node.op == Op::Equal) return { Fact::Constant, 1 };
    }
    QVector<quint64> values;
    bool bottom = false;
    for (const auto operand : node.operands) {
        const auto value = fact(operand);
        if (value.kind == Fact::Varying) return { Fact::Varying, 0 };
        if (value.kind == Fact::Bottom) bottom = true;
        values << value.value;
    }
    if (bottom) return {};
    if (values.isEmpty()) return { Fact::Varying, 0 };
    quint64 result = 0;
    const quint64 a = values[0], b = values.size() > 1 ? values[1] : 0;
    switch (node.op) {
    case Op::Extract: result = a >> node.bit_offset; break;
    case Op::Insert: {
        const auto inserted_width = program.nodes[node.operands[1]].type.bits;
        const quint64 field_mask = mask(inserted_width) << node.bit_offset;
        result = (a & ~field_mask) | ((b << node.bit_offset) & field_mask);
        break;
    }
    case Op::ZeroExtend: result = a; break;
    case Op::SignExtend: {
        const int source = program.nodes[node.operands[0]].type.bits;
        result = (a & (quint64(1) << (source - 1))) ? a | ~mask(source) : a;
        break;
    }
    case Op::Add: result = a + b; break;
    case Op::Subtract: result = a - b; break;
    case Op::Multiply: result = a * b; break;
    case Op::And: result = a & b; break;
    case Op::Or: result = a | b; break;
    case Op::Xor: result = a ^ b; break;
    case Op::Not: result = ~a; break;
    case Op::BooleanNot: result = a == 0; break;
    case Op::Equal: result = a == b; break;
    case Op::UnsignedLess: result = a < b; break;
    case Op::SignedLess: {
        const auto width = program.nodes[node.operands[0]].type.bits;
        const quint64 sign = quint64(1) << (width - 1);
        result = (a ^ sign) < (b ^ sign);
        break;
    }
    case Op::Parity: {
        unsigned int parity = 0;
        for (unsigned int bit = 0; bit < 8; ++bit) parity ^= static_cast<unsigned int>((a >> bit) & 1);
        result = parity == 0;
        break;
    }
    default: return { Fact::Varying, 0 };
    }
    return { Fact::Constant, result & mask(node.type.bits) };
}

Dataflow propagate(const Program& program, const DecompilationOptions& options, std::stop_token stop) {
    Dataflow flow;
    flow.alias.resize(program.nodes.size());
    flow.facts.resize(program.nodes.size());
    for (Value i = 0; i < static_cast<Value>(program.nodes.size()); ++i) flow.alias[i] = i;
    // Remove only proven trivial phi functions, including an unchanged value
    // carried through a loop. A nontrivial join remains a real phi function.
    for (quint32 pass = 0; pass < options.max_dataflow_iterations; ++pass) {
        bool changed = false;
        for (Value i = 0; i < static_cast<Value>(program.nodes.size()); ++i) {
            if ((i & 0xff) == 0) check_cancelled(stop);
            const auto& node = program.nodes[i];
            if (node.op != Op::Phi || compressed_root(flow.alias, i) != i) continue;
            Value common = no_value;
            bool differs = false;
            for (const auto& incoming : node.incoming) {
                const Value value = compressed_root(flow.alias, incoming.value);
                if (value == i) continue;
                if (common == no_value) common = value;
                else if (common != value) { differs = true; break; }
            }
            if (!differs && common != no_value) { flow.alias[i] = common; changed = true; }
        }
        if (!changed) break;
    }
    for (Value i = 0; i < static_cast<Value>(program.nodes.size()); ++i) {
        flow.alias[i] = compressed_root(flow.alias, i);
        if (program.nodes[i].op == Op::Constant) flow.facts[i] = { Fact::Constant, program.nodes[i].immediate };
    }
    if (!options.propagate_constants) {
        flow.converged = false;
        return flow;
    }
    for (quint32 pass = 0; pass < options.max_dataflow_iterations; ++pass) {
        bool changed = false;
        for (Value i = 0; i < static_cast<Value>(program.nodes.size()); ++i) {
            if ((i & 0xff) == 0) check_cancelled(stop);
            if (flow.alias[i] != i) continue;
            const auto next = combine(flow.facts[i], evaluate(program, program.nodes[i], flow));
            if (next != flow.facts[i]) { flow.facts[i] = next; changed = true; }
        }
        if (!changed) { flow.converged = true; break; }
    }
    if (!flow.converged) {
        // A provisional constant could later become varying. Never substitute
        // provisional lattice values when the iteration budget is exhausted.
        for (qsizetype i = 0; i < flow.facts.size(); ++i)
            if (program.nodes[i].op != Op::Constant) flow.facts[i] = { Fact::Varying, 0 };
    }
    return flow;
}

class Renderer {
public:
    Renderer(const Program& program, const Function& function, const Dataflow& flow,
             const DecompilationOptions& options, std::stop_token stop)
        : program_(program), function_(function), flow_(flow), options_(options), stop_(stop),
          code_(options.max_output_characters), ssa_(options.max_output_characters) {
        live_.resize(program.nodes.size());
        std::fill(live_.begin(), live_.end(), false);
        QVector<Value> pending;
        auto mark = [&](Value value) {
            if (value == no_value) return;
            value = resolve(flow_.alias, value);
            if (!live_[value]) { live_[value] = true; pending << value; }
        };
        for (const auto& block : program_.blocks) {
            if (!block.reachable) continue;
            for (const auto value : block.output) mark(value);
            mark(block.condition); mark(block.return_target);
        }
        while (!pending.isEmpty()) {
            check_cancelled(stop_);
            const auto value = pending.back(); pending.pop_back();
            const auto& node = program_.nodes[value];
            for (const auto operand : node.operands) mark(operand);
            for (const auto& edge : node.incoming) mark(edge.value);
        }
    }

    void render() {
        code_.line(QStringLiteral("/* Machine-state pseudocode, NOT a recovered source signature. */"));
        code_.line(QStringLiteral("/* Scalar flat little-endian memory; normal, non-faulting execution only. */"));
        code_.line(QStringLiteral("/* uN arithmetic wraps at N bits. Memory tokens preserve stores and aliasing. */"));
        code_.line(QStringLiteral("/* opaque_instruction changes the entire tracked state; ABI/types are unknown. */"));
        code_.line(QStringLiteral("machine_state %1(machine_state in) {").arg(function_name(function_)));
        declare_values();
        transfer(-1, program_.entry, QStringLiteral("    "));
        code_.line(QStringLiteral("    goto %1;").arg(label(program_.entry)));
        code_.line();

        QSet<qsizetype> consumed;
        for (qsizetype b = 0; b < program_.blocks.size(); ++b) {
            check_cancelled(stop_);
            const auto& block = program_.blocks[b];
            if (!block.reachable || consumed.contains(b)) continue;
            code_.line(label(b) + QLatin1Char(':'));
            const bool self_loop = options_.structure_control_flow && block.terminator == Terminator::Conditional &&
                (target_index(block.taken) == b || target_index(block.not_taken) == b) &&
                target_index(block.taken) != target_index(block.not_taken);
            if (self_loop) {
                render_self_loop(b);
            } else if (!render_diamond(b, consumed)) {
                body(b, QStringLiteral("    "));
                terminator(b, QStringLiteral("    "));
            }
            code_.line();
        }
        code_.line(QStringLiteral("}"));
        render_ssa();
    }

    QString code() { return code_.take(); }
    QString ssa() { return ssa_.take(); }
    const QMap<int, Address>& code_addresses() const { return code_.addresses(); }
    const QMap<int, Address>& ssa_addresses() const { return ssa_.addresses(); }
    bool truncated() const { return code_.truncated() || ssa_.truncated(); }

private:
    bool constant(Value value) const {
        value = resolve(flow_.alias, value);
        return flow_.facts[value].kind == Fact::Constant;
    }
    QString name(Value value) const {
        value = resolve(flow_.alias, value);
        const auto& node = program_.nodes[value];
        if (constant(value)) return QStringLiteral("u%1(%2)").arg(node.type.bits).arg(hex(flow_.facts[value].value));
        if (node.op == Op::Input) return QStringLiteral("in.") + node.text;
        if (node.op == Op::Phi) return node.text + QStringLiteral("_b") + QString::number(node.address.value, 16);
        return QStringLiteral("v_%1").arg(value);
    }
    QString label(qsizetype block) const { return QStringLiteral("L_") + QString::number(program_.blocks[block].address.value, 16); }
    qsizetype target_index(const std::optional<Address>& address) const {
        if (!address || address->space != function_.address.space) return -1;
        const auto found = program_.block_at.constFind(address->value);
        return found == program_.block_at.cend() || !program_.blocks[*found].reachable ? -1 : *found;
    }
    QString pack(const QVector<Value>& values) const {
        QStringList fields;
        for (qsizetype i = 0; i < values.size(); ++i)
            fields << QStringLiteral(".%1 = %2").arg(program_.state_slots[i].name, name(values[i]));
        return QStringLiteral("state{ ") + fields.join(QStringLiteral(", ")) + QStringLiteral(" }");
    }
    QString expression(Value value, int inline_depth = 0) const {
        value = resolve(flow_.alias, value);
        const auto& node = program_.nodes[value];
        if (constant(value) || node.op == Op::Input || node.op == Op::Phi) return name(value);
        auto operand = [&](qsizetype index) {
            const Value id = resolve(flow_.alias, node.operands[index]);
            const auto& source = program_.nodes[id];
            const bool small = source.type.kind == TypeKind::Bits && source.operands.size() <= 2 &&
                source.op != Op::Load && source.op != Op::Undefined && source.op != Op::StateField;
            return inline_depth > 0 && small ? expression(id, inline_depth - 1) : name(id);
        };
        auto binary = [&](const QString& operation) {
            return QStringLiteral("%1_u%2(%3, %4)").arg(operation).arg(node.type.bits).arg(operand(0), operand(1));
        };
        switch (node.op) {
        case Op::Undefined:
            return QStringLiteral("undefined_%1(%2%3)").arg(type_name(node.type), escaped(node.text),
                node.operands.isEmpty() ? QString{} : QStringLiteral(", ") + operand(0));
        case Op::Opaque:
        case Op::CallOpaque:
            return QStringLiteral("opaque_instruction(%1, %2, %3)").arg(escaped(node.address.text()), escaped(node.text), pack(node.operands));
        case Op::StateField: return QStringLiteral("%1.%2").arg(operand(0), node.text);
        case Op::Extract: return QStringLiteral("extract_u%1(%2, %3)").arg(node.type.bits).arg(operand(0)).arg(node.bit_offset);
        case Op::Insert: return QStringLiteral("insert_u%1(%2, %3, %4, %5)").arg(node.type.bits).arg(operand(0), operand(1)).arg(node.bit_offset).arg(program_.nodes[node.operands[1]].type.bits);
        case Op::ZeroExtend: return QStringLiteral("zext_u%1(%2)").arg(node.type.bits).arg(operand(0));
        case Op::SignExtend: return QStringLiteral("sext_u%1_from_u%2(%3)").arg(node.type.bits).arg(program_.nodes[node.operands[0]].type.bits).arg(operand(0));
        case Op::Add: return binary(QStringLiteral("add_wrap"));
        case Op::Subtract: return binary(QStringLiteral("sub_wrap"));
        case Op::Multiply: return binary(QStringLiteral("mul_wrap"));
        case Op::And: return binary(QStringLiteral("and"));
        case Op::Or: return binary(QStringLiteral("or"));
        case Op::Xor: return binary(QStringLiteral("xor"));
        case Op::Not: return QStringLiteral("not_u%1(%2)").arg(node.type.bits).arg(operand(0));
        case Op::BooleanNot: return QStringLiteral("!(%1)").arg(operand(0));
        case Op::Equal: return QStringLiteral("(%1 == %2)").arg(operand(0), operand(1));
        case Op::UnsignedLess: return QStringLiteral("(%1 < %2)").arg(operand(0), operand(1));
        case Op::SignedLess: return QStringLiteral("(s%1(%2) < s%1(%3))")
            .arg(program_.nodes[node.operands[0]].type.bits).arg(operand(0), operand(1));
        case Op::Select: return QStringLiteral("(%1 ? %2 : %3)").arg(operand(0), operand(1), operand(2));
        case Op::Parity: return QStringLiteral("even_parity_low8(%1)").arg(operand(0));
        case Op::Load: return QStringLiteral("load_le_u%1(%2, %3)").arg(node.type.bits).arg(operand(0), operand(1));
        case Op::Store: return QStringLiteral("store_le_u%1(%2, %3, %4)").arg(program_.nodes[node.operands[2]].type.bits).arg(operand(0), operand(1), operand(2));
        default: return name(value);
        }
    }
    void declare_values() {
        QMap<QString, QStringList> groups;
        for (Value i = 0; i < static_cast<Value>(program_.nodes.size()); ++i) {
            if ((i & 0xff) == 0) check_cancelled(stop_);
            if (!live_[i] || resolve(flow_.alias, i) != i || constant(i) || program_.nodes[i].op == Op::Input) continue;
            groups[type_name(program_.nodes[i].type)] << name(i);
        }
        for (auto i = groups.cbegin(); i != groups.cend(); ++i) {
            const auto& values = i.value();
            for (qsizetype at = 0; at < values.size(); at += 8)
                code_.line(QStringLiteral("    %1 %2;").arg(i.key(), values.mid(at, 8).join(QStringLiteral(", "))));
        }
    }
    void transfer(qsizetype predecessor, qsizetype target, const QString& indent) {
        if (target < 0) return;
        const auto& block = program_.blocks[target];
        QVector<QPair<Value, Value>> moves;
        for (const auto input : block.input) {
            if (!live_[input] || resolve(flow_.alias, input) != input || constant(input)) continue;
            const auto& phi = program_.nodes[input];
            for (const auto& edge : phi.incoming) {
                if (edge.predecessor == predecessor && resolve(flow_.alias, edge.value) != input) {
                    moves.push_back(qMakePair(input, edge.value));
                    break;
                }
            }
        }
        if (moves.isEmpty()) return;
        // Evaluate every RHS before assigning any phi destination. This is
        // essential for cyclic parallel copies on loop backedges.
        const auto serial = edge_serial_++;
        code_.line(indent + QStringLiteral("{ /* simultaneous phi transfer */"));
        for (qsizetype i = 0; i < moves.size(); ++i)
            code_.line(indent + QStringLiteral("    const %1 edge_%2_%3 = %4;")
                .arg(type_name(program_.nodes[moves[i].first].type)).arg(serial).arg(i).arg(name(moves[i].second)));
        for (qsizetype i = 0; i < moves.size(); ++i)
            code_.line(indent + QStringLiteral("    %1 = edge_%2_%3;").arg(name(moves[i].first)).arg(serial).arg(i));
        code_.line(indent + QLatin1Char('}'));
    }
    void body(qsizetype index, const QString& indent) {
        const auto& block = program_.blocks[index];
        qsizetype at = 0;
        for (const auto instruction_index : block.instructions) {
            check_cancelled(stop_);
            const auto& instruction = function_.instructions[instruction_index];
            code_.line(indent + QStringLiteral("/* %1: %2 %3 */").arg(instruction.address.text(), comment(instruction.mnemonic), comment(instruction.operands_text)), instruction.address);
            while (at < block.definitions.size() && program_.nodes[block.definitions[at]].address == instruction.address) {
                const auto value = block.definitions[at++];
                if (!live_[value] || resolve(flow_.alias, value) != value || constant(value)) continue;
                code_.line(indent + name(value) + QStringLiteral(" = ") + expression(value) + QLatin1Char(';'), instruction.address);
            }
        }
    }
    std::optional<Address> control_source(qsizetype index) const {
        const auto& block = program_.blocks[index];
        if (block.instructions.isEmpty()) return {};
        const auto& instruction = function_.instructions[block.instructions.back()];
        // A normal fallthrough or unresolved end is synthesized by CFG recovery.
        // Explicit machine control is attributable to this final instruction.
        if (instruction.flow == FlowKind::Jump || instruction.flow == FlowKind::ConditionalJump ||
            instruction.flow == FlowKind::Return || instruction.flow == FlowKind::Trap) return instruction.address;
        return {};
    }
    void edge(qsizetype from, const std::optional<Address>& address, const QString& indent) {
        const auto target = target_index(address);
        const auto source = control_source(from);
        if (target >= 0) {
            transfer(from, target, indent);
            code_.line(indent + QStringLiteral("goto %1;").arg(label(target)), source);
        } else if (address) {
            code_.line(indent + QStringLiteral("return transfer_to(%1, %2); /* outside recovered body */").arg(escaped(address->text()), pack(program_.blocks[from].output)), source);
        } else {
            code_.line(indent + QStringLiteral("return unresolved_control(%1, %2);").arg(escaped(program_.blocks[from].address.text()), pack(program_.blocks[from].output)), source);
        }
    }
    void terminator(qsizetype index, const QString& indent) {
        const auto& block = program_.blocks[index];
        const auto source = control_source(index);
        switch (block.terminator) {
        case Terminator::Conditional:
            code_.line(indent + QStringLiteral("if (%1) {").arg(condition(block.condition)), source);
            edge(index, block.taken, indent + QStringLiteral("    "));
            code_.line(indent + QStringLiteral("} else {"), source);
            edge(index, block.not_taken, indent + QStringLiteral("    "));
            code_.line(indent + QLatin1Char('}'));
            break;
        case Terminator::Goto: edge(index, block.taken, indent); break;
        case Terminator::Return:
            code_.line(indent + QStringLiteral("return machine_return(%1, %2);").arg(name(block.return_target), pack(block.output)), source);
            break;
        default:
            code_.line(indent + QStringLiteral("return unresolved_control(%1, %2);").arg(escaped(block.address.text()), pack(block.output)), source);
            break;
        }
    }
    QString condition(Value value) const {
        const auto resolved = resolve(flow_.alias, value);
        const auto& node = program_.nodes[resolved];
        const bool repeatable = node.op != Op::Undefined && node.op != Op::Load && node.op != Op::StateField;
        return repeatable && node.type.kind == TypeKind::Bits && node.op != Op::Phi && node.op != Op::Input ? expression(value, 3) : name(value);
    }
    void render_self_loop(qsizetype index) {
        const auto& block = program_.blocks[index];
        const bool taken_repeats = target_index(block.taken) == index;
        code_.line(QStringLiteral("    for (;;) { /* proven single-block loop */"));
        body(index, QStringLiteral("        "));
        code_.line(QStringLiteral("        if (%1) {").arg(taken_repeats ? condition(block.condition) : QStringLiteral("!(%1)").arg(condition(block.condition))), control_source(index));
        transfer(index, index, QStringLiteral("            "));
        code_.line(QStringLiteral("            continue;"), control_source(index));
        code_.line(QStringLiteral("        }"));
        code_.line(QStringLiteral("        break;"), control_source(index));
        code_.line(QStringLiteral("    }"));
        edge(index, taken_repeats ? block.not_taken : block.taken, QStringLiteral("    "));
    }
    bool render_diamond(qsizetype index, QSet<qsizetype>& consumed) {
        const auto& head = program_.blocks[index];
        if (!options_.structure_control_flow || head.terminator != Terminator::Conditional) return false;
        const auto left = target_index(head.taken), right = target_index(head.not_taken);
        if (left < 0 || right < 0 || left == right || left == index || right == index ||
            left == program_.entry || right == program_.entry || consumed.contains(left) || consumed.contains(right)) return false;
        const auto& a = program_.blocks[left]; const auto& b = program_.blocks[right];
        auto sole_predecessor = [&](const Block& block) {
            qsizetype count = 0;
            for (const auto predecessor : block.predecessors)
                if (program_.blocks[predecessor].reachable) { if (predecessor != index) return false; ++count; }
            return count == 1;
        };
        if (a.terminator != Terminator::Goto || b.terminator != Terminator::Goto || !sole_predecessor(a) || !sole_predecessor(b)) return false;
        const auto join = target_index(a.taken);
        if (join < 0 || join != target_index(b.taken) || join == left || join == right || join == index) return false;
        body(index, QStringLiteral("    "));
        code_.line(QStringLiteral("    if (%1) {").arg(condition(head.condition)), control_source(index));
        transfer(index, left, QStringLiteral("        "));
        body(left, QStringLiteral("        "));
        transfer(left, join, QStringLiteral("        "));
        code_.line(QStringLiteral("    } else {"), control_source(index));
        transfer(index, right, QStringLiteral("        "));
        body(right, QStringLiteral("        "));
        transfer(right, join, QStringLiteral("        "));
        code_.line(QStringLiteral("    }"));
        code_.line(QStringLiteral("    goto %1;").arg(label(join)));
        consumed.insert(left); consumed.insert(right);
        return true;
    }
    void render_ssa() {
        ssa_.line(QStringLiteral("; Typed machine-state SSA; instruction addresses are retained."));
        ssa_.line(QStringLiteral("; phi[entry] denotes external machine state; other inputs name predecessor blocks."));
        for (const auto value : program_.initial)
            ssa_.line(QStringLiteral("%1 : %2 = input(%3)").arg(name(value), type_name(program_.nodes[value].type), escaped(program_.nodes[value].text)));
        for (qsizetype b = 0; b < program_.blocks.size(); ++b) {
            check_cancelled(stop_);
            const auto& block = program_.blocks[b];
            if (!block.reachable) continue;
            ssa_.line(); ssa_.line(label(b) + QLatin1Char(':'));
            for (const auto input : block.input) {
                if (!live_[resolve(flow_.alias, input)]) continue;
                QStringList incoming;
                for (const auto& edge : program_.nodes[input].incoming)
                    incoming << QStringLiteral("[%1: %2]").arg(edge.predecessor < 0 ? QStringLiteral("entry") : label(edge.predecessor), name(edge.value));
                if (resolve(flow_.alias, input) != input || constant(input))
                    ssa_.line(QStringLiteral("  ; %1_%2 simplified to %3").arg(program_.nodes[input].text).arg(b).arg(name(input)));
                else ssa_.line(QStringLiteral("  %1 : %2 = phi(%3)").arg(name(input), type_name(program_.nodes[input].type), incoming.join(QStringLiteral(", "))));
            }
            for (const auto value : block.definitions) {
                if (!live_[value] || resolve(flow_.alias, value) != value || program_.nodes[value].op == Op::Constant) continue;
                if (constant(value)) {
                    ssa_.line(QStringLiteral("  ; v_%1 : %2 folded to %3 ; %4").arg(value)
                        .arg(type_name(program_.nodes[value].type), name(value), program_.nodes[value].address.text()), program_.nodes[value].address);
                    continue;
                }
                ssa_.line(QStringLiteral("  %1 : %2 = %3 ; %4").arg(name(value), type_name(program_.nodes[value].type), expression(value), program_.nodes[value].address.text()), program_.nodes[value].address);
            }
            if (block.terminator == Terminator::Conditional)
                ssa_.line(QStringLiteral("  branch %1, %2, %3").arg(name(block.condition), block.taken ? block.taken->text() : QStringLiteral("unknown"), block.not_taken ? block.not_taken->text() : QStringLiteral("unknown")), control_source(b));
            else if (block.terminator == Terminator::Return)
                ssa_.line(QStringLiteral("  machine_return %1, %2").arg(name(block.return_target), pack(block.output)), control_source(b));
            else ssa_.line(QStringLiteral("  transfer %1, %2").arg(block.taken ? block.taken->text() : QStringLiteral("unknown"), pack(block.output)), control_source(b));
        }
    }

    const Program& program_;
    const Function& function_;
    const Dataflow& flow_;
    const DecompilationOptions& options_;
    std::stop_token stop_;
    QVector<bool> live_;
    Writer code_, ssa_;
    quint64 edge_serial_ = 0;
};

DecompilationResult fallback(const Function& function, const DecompilationOptions& options,
                             const QString& reason, std::stop_token stop) {
    DecompilationResult result;
    result.warnings << reason;
    Writer text(options.max_output_characters);
    text.line(QStringLiteral("/* No typed pseudocode is claimed: %1 */").arg(comment(reason)));
    text.line(QStringLiteral("/* The following bounded listing preserves opaque instruction effects only. */"));
    text.line(QStringLiteral("opaque_listing %1 {").arg(function_name(function)));
    const qsizetype count = std::min(function.instructions.size(), qsizetype(options.max_instructions));
    for (qsizetype i = 0; i < count && !text.truncated(); ++i) {
        check_cancelled(stop);
        const auto& instruction = function.instructions[i];
        text.line(QStringLiteral("    opaque_instruction(%1, %2); /* all state/control unknown */")
            .arg(escaped(instruction.address.text()), escaped(instruction.mnemonic + QLatin1Char(' ') + instruction.operands_text)), instruction.address);
    }
    if (count < function.instructions.size()) text.line(QStringLiteral("    /* instruction ceiling: further instructions omitted */"));
    text.line(QStringLiteral("}"));
    result.total_instructions = static_cast<quint32>(std::min(function.instructions.size(), qsizetype(std::numeric_limits<quint32>::max())));
    result.output_truncated = text.truncated() || count < function.instructions.size();
    result.pseudocode = text.take();
    result.pseudocode_addresses = text.addresses();
    result.ssa_text = QStringLiteral("; Typed SSA is unavailable. ") + reason + QLatin1Char('\n');
    return result;
}

} // namespace
} // namespace decompiler_detail

DecompilationResult decompile_function(const AnalysisSnapshot& snapshot, qsizetype function_index,
                                       const DecompilationOptions& requested, std::stop_token stop) {
    check_cancelled(stop);
    if (function_index < 0 || function_index >= snapshot.functions.size())
        throw std::out_of_range("Decompilation function index is out of range.");
    const auto options = decompiler_detail::bounded_options(requested);
    const auto& function = snapshot.functions[function_index];
    try {
        auto program = decompiler_detail::lift(snapshot, function, options, stop);
        auto flow = decompiler_detail::propagate(program, options, stop);
        DecompilationResult result;
        result.modeled_instructions = program.modeled;
        result.total_instructions = static_cast<quint32>(function.instructions.size());
        result.ir_nodes = static_cast<quint32>(program.nodes.size());
        for (const auto& block : program.blocks) if (block.reachable) ++result.reachable_blocks;
        result.warnings = program.warnings;
        result.warnings << QStringLiteral("Output uses explicit machine-state inputs and non-faulting flat-memory intrinsics; original signatures, source types, exceptions and ABI are not recovered.");
        result.dataflow_converged = flow.converged;
        if (options.propagate_constants && !flow.converged)
            result.warnings << QStringLiteral("Dataflow iteration ceiling reached; provisional constants were discarded.");
        decompiler_detail::Renderer renderer(program, function, flow, options, stop);
        renderer.render();
        result.output_truncated = renderer.truncated();
        result.pseudocode = renderer.code();
        result.ssa_text = renderer.ssa();
        result.pseudocode_addresses = renderer.code_addresses();
        result.ssa_addresses = renderer.ssa_addresses();
        if (result.output_truncated) result.warnings << QStringLiteral("Listing output ceiling reached; output is incomplete.");
        result.complete = !program.incomplete && !result.output_truncated && program.total > 0 &&
            program.modeled == program.total && program.total == result.total_instructions;
        return result;
    } catch (const decompiler_detail::Limit& limit) {
        return decompiler_detail::fallback(function, options, QString::fromUtf8(limit.what()), stop);
    }
}

} // namespace reworkbench
