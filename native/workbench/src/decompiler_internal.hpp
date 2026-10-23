#pragma once

#include "reworkbench/decompiler.hpp"

#include <QMap>
#include <QSet>

#include <limits>
#include <utility>

namespace reworkbench::decompiler_detail {

using Value = quint32;
inline constexpr Value no_value = std::numeric_limits<Value>::max();

enum class TypeKind { Bits, Memory, Architecture, OpaqueState };
struct Type {
    TypeKind kind = TypeKind::Bits;
    int bits = 0;
    bool operator==(const Type&) const = default;
};

enum class Op {
    Input, Constant, Phi, Undefined, Opaque, StateField,
    Extract, Insert, ZeroExtend, SignExtend,
    Add, Subtract, Multiply, And, Or, Xor, Not,
    Equal, UnsignedLess, SignedLess, BooleanNot, Select, Parity,
    Load, Store
};

struct Incoming { qsizetype predecessor = -1; Value value = no_value; };
struct Node {
    Op op = Op::Input;
    Type type;
    QVector<Value> operands;
    QVector<Incoming> incoming;
    quint64 immediate = 0;
    int bit_offset = 0;
    QString text;
    Address address;
    qsizetype block = -1;
};

struct Slot { QString name; Type type; };
enum class Terminator { None, Goto, Conditional, Return, OpaqueExit };
struct Block {
    Address address;
    QVector<qsizetype> instructions;
    QVector<qsizetype> predecessors;
    QVector<qsizetype> successors;
    QVector<Value> input;
    QVector<Value> output;
    QVector<Value> definitions;
    Terminator terminator = Terminator::None;
    Value condition = no_value;
    Value return_target = no_value;
    std::optional<Address> taken;
    std::optional<Address> not_taken;
    bool reachable = false;
};

struct Program {
    Architecture architecture = Architecture::Unknown;
    int register_width = 0;
    QVector<Slot> state_slots;
    QMap<QString, qsizetype> slot_index;
    QVector<Node> nodes;
    QVector<Value> initial;
    QVector<Block> blocks;
    QMap<quint64, qsizetype> block_at;
    qsizetype entry = -1;
    quint32 modeled = 0;
    quint32 total = 0;
    QStringList warnings;
    bool incomplete = false;
};

class Limit final : public std::runtime_error {
public:
    explicit Limit(const char* text) : std::runtime_error(text) {}
};

inline quint64 mask(int bits) {
    return bits >= 64 ? std::numeric_limits<quint64>::max() : (quint64(1) << bits) - 1;
}

inline void warn(Program& program, const QString& text) {
    if (program.warnings.size() < 128 && !program.warnings.contains(text)) program.warnings << text;
}

DecompilationOptions bounded_options(DecompilationOptions options);
Program lift(const AnalysisSnapshot&, const Function&, const DecompilationOptions&, std::stop_token);

} // namespace reworkbench::decompiler_detail
