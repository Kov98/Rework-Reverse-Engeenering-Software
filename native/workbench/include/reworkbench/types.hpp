#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <chrono>
#include <functional>
#include <optional>
#include <stop_token>
#include <stdexcept>

namespace reworkbench {

enum class Architecture { Unknown, X86, X64, Arm, Thumb, Arm64, Mips };
enum class ByteOrder { Little, Big };
enum class BinaryFormat { Unknown, Raw, Pe, Elf, MachO, FatMachO };
enum class FlowKind { Normal, Call, Jump, ConditionalJump, Return, Trap };
enum class OperandKind { Register, Immediate, Memory, Other };

struct Address {
    QString space = QStringLiteral("ram");
    quint64 value = 0;
    [[nodiscard]] QString text() const { return space + QStringLiteral(":0x") + QString::number(value, 16); }
    bool operator==(const Address&) const = default;
};

inline QString hex(quint64 value) { return QStringLiteral("0x") + QString::number(value, 16); }
QString architecture_name(Architecture architecture);
QString format_name(BinaryFormat format);
Architecture architecture_from_name(const QString& name);

struct OpenOptions {
    bool raw = false;
    Architecture raw_architecture = Architecture::Unknown;
    ByteOrder raw_byte_order = ByteOrder::Little;
    quint64 raw_base = 0;
    QVector<Address> entries;
    std::optional<quint32> slice_index;
    QString symbol_map_path;
    QString symbol_map_sha256;
};

struct Region {
    QString name;
    Address start;
    quint64 virtual_size = 0;
    std::optional<quint64> file_offset;
    quint64 file_size = 0;
    bool readable = false;
    bool writable = false;
    bool executable = false;
};

struct Symbol {
    Address address;
    QString name;
    QString kind;
};

struct Import {
    QString name;
    QString library;
    std::optional<Address> slot;
};

struct Slice {
    quint32 index = 0;
    QString label;
    quint64 file_offset = 0;
    quint64 size = 0;
    Architecture architecture = Architecture::Unknown;
};

struct Probe {
    BinaryFormat format = BinaryFormat::Unknown;
    QVector<Slice> slices;
    bool raw_required = false;
};

struct BinaryMetadata {
    QString source_path;
    QString original_name;
    QString sha256;
    quint64 size = 0;
    BinaryFormat format = BinaryFormat::Unknown;
    Architecture architecture = Architecture::Unknown;
    ByteOrder byte_order = ByteOrder::Little;
    int bits = 0;
    quint64 image_base = 0;
    QVector<Address> entries;
    QVector<Region> regions;
    QVector<Symbol> symbols;
    QVector<Import> imports;
    QStringList warnings;
};

struct Operand {
    OperandKind kind = OperandKind::Other;
    QString reg;
    quint64 immediate = 0;
    // x86/x64 encoded immediate size, from Capstone's instruction encoding.
    // Distinct from width_bits, which describes the logical operand size.
    // Zero means absent or unverified; consumers must not guess.
    int encoded_immediate_bits = 0;
    QString memory_base;
    QString memory_index;
    int memory_scale = 1;
    qint64 displacement = 0;
    int width_bits = 0;
};

struct Instruction {
    Address address;
    QByteArray bytes;
    QString mnemonic;
    QString operands_text;
    QVector<Operand> operands;
    FlowKind flow = FlowKind::Normal;
    std::optional<Address> direct_target;
    std::optional<Address> memory_target;
    QVector<Address> indirect_targets;
    bool falls_through = true;
    quint32 native_id = 0;
};

struct BasicBlock {
    Address address;
    QVector<quint32> instruction_indices;
    QVector<Address> successors;
};

struct Function {
    Address address;
    QString name;
    QVector<Instruction> instructions;
    QVector<BasicBlock> blocks;
    bool partial = false;
    QStringList warnings;
};

struct Reference {
    Address source;
    Address target;
    QString type;
    std::optional<Address> source_function;
};

struct ByteString {
    quint64 file_offset = 0;
    QString value;
};

struct AnalysisLimits {
    quint64 max_instructions = 200000;
    quint32 max_function_instructions = 4096;
    quint32 max_functions = 10000;
    int timeout_seconds = 300;
    quint32 max_strings = 10000;
};

struct AnalysisSnapshot {
    BinaryMetadata metadata;
    OpenOptions options;
    QVector<Function> functions;
    QVector<Reference> references;
    QVector<ByteString> strings;
    double entropy = 0;
    bool strings_truncated = false;
    QString state = QStringLiteral("ready");
    QString engine_version = QStringLiteral("reworkbench-native-0.3");
    QJsonObject enrichment;
    QStringList capabilities;
    QStringList warnings;
    quint64 revision = 0;
};

struct Progress {
    QString stage;
    quint64 completed = 0;
    quint64 total = 0;
};

using ProgressCallback = std::function<void(const Progress&)>;

class Cancelled final : public std::runtime_error {
public:
    Cancelled() : std::runtime_error("Operação interrompida.") {}
};

inline void check_cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw Cancelled{};
}

struct RenderedFunction {
    QString text;
    // Zero-based output lines with an exact original instruction; synthetic lines are omitted.
    QMap<int, Address> source_addresses;
    quint32 modeled_instructions = 0;
    quint32 total_instructions = 0;
    bool complete = false;
};

struct Note {
    Address address;
    QString text;
    bool bookmarked = false;
};

struct Sample {
    QString sha256;
    QString relative_path;
    QString original_path;
    QString original_name;
    quint64 size = 0;
};

struct AnalysisSummary {
    QString id;
    QString name;
    QString sha256;
    QString state;
    quint64 revision = 0;
};

} // namespace reworkbench
