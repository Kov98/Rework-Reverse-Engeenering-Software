#include "serialization.hpp"

#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>
#include <utility>

namespace reworkbench::persistence {
namespace {

[[noreturn]] void invalid(const QString& detail) {
    throw std::runtime_error((QStringLiteral("Snapshot nativo inválido: ") + detail).toStdString());
}

QJsonValue field(const QJsonObject& object, const char* key) {
    const auto value = object.value(QLatin1String(key));
    if (value.isUndefined()) invalid(QStringLiteral("campo ausente: ") + QLatin1String(key));
    return value;
}

QString string(const QJsonValue& value, qsizetype maximum = 1024 * 1024) {
    if (!value.isString() || value.toString().size() > maximum) invalid(QStringLiteral("texto inválido ou excessivo"));
    return value.toString();
}

QString string(const QJsonObject& object, const char* key, qsizetype maximum = 1024 * 1024) {
    return string(field(object, key), maximum);
}

QJsonObject object(const QJsonValue& value) {
    if (!value.isObject()) invalid(QStringLiteral("objeto esperado"));
    return value.toObject();
}

QJsonArray array(const QJsonValue& value, qsizetype maximum) {
    if (!value.isArray() || value.toArray().size() > maximum) invalid(QStringLiteral("lista inválida ou excessiva"));
    return value.toArray();
}

bool boolean(const QJsonObject& value, const char* key) {
    const auto item = field(value, key);
    if (!item.isBool()) invalid(QStringLiteral("booleano esperado"));
    return item.toBool();
}

int integer(const QJsonValue& value, int minimum, int maximum) {
    if (!value.isDouble()) invalid(QStringLiteral("inteiro esperado"));
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < minimum || number > maximum)
        invalid(QStringLiteral("inteiro fora do intervalo"));
    return static_cast<int>(number);
}

int integer(const QJsonObject& value, const char* key, int minimum, int maximum) {
    return integer(field(value, key), minimum, maximum);
}

quint64 number(const QJsonObject& value, const char* key) { return unsigned_hex(string(value, key, 18)); }

QString signed_hex(qint64 value) {
    // -(INT64_MIN) is undefined; perform the magnitude calculation unsigned.
    return value < 0 ? QStringLiteral("-0x") + QString::number(quint64(0) - quint64(value), 16) : hex(quint64(value));
}

qint64 signed_number(const QString& text) {
    const bool negative = text.startsWith(QLatin1Char('-'));
    const quint64 magnitude = unsigned_hex(negative ? text.mid(1) : text);
    const quint64 limit = quint64(std::numeric_limits<qint64>::max());
    if (magnitude > limit + (negative ? 1u : 0u)) invalid(QStringLiteral("deslocamento fora do intervalo"));
    if (negative && magnitude == limit + 1) return std::numeric_limits<qint64>::min();
    return negative ? -static_cast<qint64>(magnitude) : static_cast<qint64>(magnitude);
}

QJsonArray addresses(const QVector<Address>& values) {
    QJsonArray result;
    for (const auto& value : values) result.append(address_json(value));
    return result;
}

QVector<Address> read_addresses(const QJsonValue& values, qsizetype maximum = 200000) {
    QVector<Address> result;
    for (const auto& value : array(values, maximum)) result.append(address_from_json(object(value)));
    return result;
}

QJsonValue optional_address(const std::optional<Address>& value) {
    return value ? QJsonValue(address_json(*value)) : QJsonValue(QJsonValue::Null);
}

std::optional<Address> read_optional_address(const QJsonValue& value) {
    if (value.isNull()) return std::nullopt;
    return address_from_json(object(value));
}

QJsonArray strings(const QStringList& values) {
    QJsonArray result;
    for (const auto& value : values) result.append(value);
    return result;
}

QStringList read_strings(const QJsonValue& values, qsizetype maximum = 100000) {
    QStringList result;
    for (const auto& value : array(values, maximum)) result.append(string(value));
    return result;
}

QJsonObject options_json(const OpenOptions& options) {
    return {{"raw", options.raw}, {"raw_architecture", int(options.raw_architecture)},
            {"raw_byte_order", int(options.raw_byte_order)}, {"raw_base", hex(options.raw_base)},
            {"entries", addresses(options.entries)},
            {"slice_index", options.slice_index ? QJsonValue(hex(*options.slice_index)) : QJsonValue(QJsonValue::Null)}};
}

OpenOptions read_options(const QJsonObject& object) {
    OpenOptions result;
    result.raw = boolean(object, "raw");
    result.raw_architecture = static_cast<Architecture>(integer(object, "raw_architecture", 0, int(Architecture::Mips)));
    result.raw_byte_order = static_cast<ByteOrder>(integer(object, "raw_byte_order", 0, 1));
    result.raw_base = number(object, "raw_base");
    result.entries = read_addresses(field(object, "entries"));
    const auto slice = field(object, "slice_index");
    if (!slice.isNull()) {
        const auto index = unsigned_hex(string(slice, 18));
        if (index > std::numeric_limits<quint32>::max()) invalid(QStringLiteral("índice de imagem excessivo"));
        result.slice_index = static_cast<quint32>(index);
    }
    return result;
}

QJsonObject metadata_json(const BinaryMetadata& metadata, std::stop_token stop) {
    QJsonArray regions, symbols, imports;
    for (const auto& region : metadata.regions) {
        check_cancelled(stop);
        regions.append(QJsonObject{{"name", region.name}, {"start", address_json(region.start)},
            {"virtual_size", hex(region.virtual_size)}, {"file_offset", region.file_offset ? QJsonValue(hex(*region.file_offset)) : QJsonValue(QJsonValue::Null)},
            {"file_size", hex(region.file_size)}, {"readable", region.readable}, {"writable", region.writable}, {"executable", region.executable}});
    }
    for (const auto& symbol : metadata.symbols) {
        check_cancelled(stop);
        symbols.append(QJsonObject{{"address", address_json(symbol.address)}, {"name", symbol.name}, {"kind", symbol.kind}});
    }
    for (const auto& entry : metadata.imports) {
        check_cancelled(stop);
        imports.append(QJsonObject{{"name", entry.name}, {"library", entry.library}, {"slot", optional_address(entry.slot)}});
    }
    return {{"source_path", metadata.source_path}, {"original_name", metadata.original_name}, {"sha256", metadata.sha256},
        {"size", hex(metadata.size)}, {"format", int(metadata.format)}, {"format_name", format_name(metadata.format)},
        {"architecture", int(metadata.architecture)}, {"architecture_name", architecture_name(metadata.architecture)},
        {"byte_order", int(metadata.byte_order)}, {"bits", metadata.bits}, {"image_base", hex(metadata.image_base)},
        {"entries", addresses(metadata.entries)}, {"regions", regions}, {"symbols", symbols}, {"imports", imports}, {"warnings", strings(metadata.warnings)}};
}

BinaryMetadata read_metadata(const QJsonObject& object, std::stop_token stop) {
    BinaryMetadata result;
    result.source_path = string(object, "source_path");
    result.original_name = string(object, "original_name");
    result.sha256 = string(object, "sha256", 64);
    result.size = number(object, "size");
    result.format = static_cast<BinaryFormat>(integer(object, "format", 0, int(BinaryFormat::FatMachO)));
    result.architecture = static_cast<Architecture>(integer(object, "architecture", 0, int(Architecture::Mips)));
    result.byte_order = static_cast<ByteOrder>(integer(object, "byte_order", 0, 1));
    result.bits = integer(object, "bits", 0, 128);
    result.image_base = number(object, "image_base");
    result.entries = read_addresses(field(object, "entries"));
    for (const auto& value : array(field(object, "regions"), 100000)) {
        check_cancelled(stop);
        const auto item = persistence::object(value);
        Region region;
        region.name = string(item, "name");
        region.start = address_from_json(persistence::object(field(item, "start")));
        region.virtual_size = number(item, "virtual_size");
        const auto offset = field(item, "file_offset");
        if (!offset.isNull()) region.file_offset = unsigned_hex(string(offset, 18));
        region.file_size = number(item, "file_size");
        region.readable = boolean(item, "readable");
        region.writable = boolean(item, "writable");
        region.executable = boolean(item, "executable");
        result.regions.append(std::move(region));
    }
    for (const auto& value : array(field(object, "symbols"), 1000000)) {
        check_cancelled(stop);
        const auto item = persistence::object(value);
        result.symbols.append(Symbol{address_from_json(persistence::object(field(item, "address"))), string(item, "name"), string(item, "kind")});
    }
    for (const auto& value : array(field(object, "imports"), 1000000)) {
        check_cancelled(stop);
        const auto item = persistence::object(value);
        result.imports.append(Import{string(item, "name"), string(item, "library"), read_optional_address(field(item, "slot"))});
    }
    result.warnings = read_strings(field(object, "warnings"));
    return result;
}

QJsonObject instruction_json(const Instruction& instruction) {
    QJsonArray operands;
    for (const auto& operand : instruction.operands)
        operands.append(QJsonObject{{"kind", int(operand.kind)}, {"reg", operand.reg}, {"immediate", hex(operand.immediate)},
            {"memory_base", operand.memory_base}, {"memory_index", operand.memory_index}, {"memory_scale", operand.memory_scale},
            {"displacement", signed_hex(operand.displacement)}, {"width_bits", operand.width_bits}});
    return {{"address", address_json(instruction.address)}, {"bytes", QString::fromLatin1(instruction.bytes.toHex())},
        {"mnemonic", instruction.mnemonic}, {"operands_text", instruction.operands_text}, {"operands", operands},
        {"flow", int(instruction.flow)}, {"direct_target", optional_address(instruction.direct_target)},
        {"memory_target", optional_address(instruction.memory_target)}, {"falls_through", instruction.falls_through},
        {"native_id", hex(instruction.native_id)}};
}

Instruction read_instruction(const QJsonObject& object) {
    Instruction result;
    result.address = address_from_json(persistence::object(field(object, "address")));
    const auto encoded = string(object, "bytes", 1024);
    if ((encoded.size() & 1) != 0) invalid(QStringLiteral("bytes hexadecimais inválidos"));
    result.bytes = QByteArray::fromHex(encoded.toLatin1());
    if (QString::fromLatin1(result.bytes.toHex()) != encoded) invalid(QStringLiteral("bytes hexadecimais não canônicos"));
    result.mnemonic = string(object, "mnemonic", 4096);
    result.operands_text = string(object, "operands_text", 65536);
    for (const auto& value : array(field(object, "operands"), 64)) {
        const auto item = persistence::object(value);
        Operand operand;
        operand.kind = static_cast<OperandKind>(integer(item, "kind", 0, int(OperandKind::Other)));
        operand.reg = string(item, "reg", 4096);
        operand.immediate = number(item, "immediate");
        operand.memory_base = string(item, "memory_base", 4096);
        operand.memory_index = string(item, "memory_index", 4096);
        operand.memory_scale = integer(item, "memory_scale", std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        operand.displacement = signed_number(string(item, "displacement", 19));
        operand.width_bits = integer(item, "width_bits", 0, 65536);
        result.operands.append(std::move(operand));
    }
    result.flow = static_cast<FlowKind>(integer(object, "flow", 0, int(FlowKind::Trap)));
    result.direct_target = read_optional_address(field(object, "direct_target"));
    result.memory_target = read_optional_address(field(object, "memory_target"));
    result.falls_through = boolean(object, "falls_through");
    const auto native_id = number(object, "native_id");
    if (native_id > std::numeric_limits<quint32>::max()) invalid(QStringLiteral("identificador de instrução excessivo"));
    result.native_id = static_cast<quint32>(native_id);
    return result;
}

} // namespace

quint64 unsigned_hex(const QString& text) {
    if (text.size() < 3 || text.size() > 18 || !text.startsWith(QStringLiteral("0x"))) invalid(QStringLiteral("inteiro hexadecimal textual esperado"));
    for (const auto character : text.mid(2))
        if (!(character >= QLatin1Char('0') && character <= QLatin1Char('9')) &&
            !(character >= QLatin1Char('a') && character <= QLatin1Char('f')))
            invalid(QStringLiteral("dígito hexadecimal inválido"));
    bool ok = false;
    const auto result = text.mid(2).toULongLong(&ok, 16);
    if (!ok) invalid(QStringLiteral("inteiro hexadecimal fora do intervalo"));
    return result;
}

QJsonObject address_json(const Address& address) { return {{"space", address.space}, {"value", hex(address.value)}}; }

Address address_from_json(const QJsonObject& object) {
    const auto space = string(object, "space", 64);
    if (space.isEmpty() || space.contains(QChar::Null)) invalid(QStringLiteral("espaço de endereço inválido"));
    return {space, number(object, "value")};
}

QJsonObject note_json(const Note& note) {
    return {{"address", address_json(note.address)}, {"text", note.text}, {"bookmarked", note.bookmarked}};
}

QJsonObject snapshot_json(const AnalysisSnapshot& snapshot, std::stop_token stop) {
    check_cancelled(stop);
    if (snapshot.functions.size() > 10000 || snapshot.references.size() > 2000000 || snapshot.strings.size() > 10000 ||
        snapshot.metadata.regions.size() > 100000 || snapshot.metadata.symbols.size() > 1000000 || snapshot.metadata.imports.size() > 1000000)
        invalid(QStringLiteral("limites do snapshot excedidos"));
    QJsonArray functions, references, byte_strings;
    quint64 instruction_count = 0;
    for (const auto& function : snapshot.functions) {
        check_cancelled(stop);
        instruction_count += quint64(function.instructions.size());
        if (instruction_count > 200000 || function.blocks.size() > 200000) invalid(QStringLiteral("limite de instruções/blocos excedido"));
        QJsonArray instructions, blocks;
        for (const auto& instruction : function.instructions) {
            check_cancelled(stop);
            instructions.append(instruction_json(instruction));
        }
        for (const auto& block : function.blocks) {
            QJsonArray indices;
            for (const auto index : block.instruction_indices) {
                if (index >= quint64(function.instructions.size())) invalid(QStringLiteral("índice de bloco fora da função"));
                indices.append(hex(index));
            }
            blocks.append(QJsonObject{{"address", address_json(block.address)}, {"instruction_indices", indices}, {"successors", addresses(block.successors)}});
        }
        functions.append(QJsonObject{{"address", address_json(function.address)}, {"name", function.name}, {"instructions", instructions},
            {"blocks", blocks}, {"partial", function.partial}, {"warnings", strings(function.warnings)}});
    }
    for (const auto& reference : snapshot.references) {
        check_cancelled(stop);
        references.append(QJsonObject{{"source", address_json(reference.source)}, {"target", address_json(reference.target)},
            {"type", reference.type}, {"source_function", optional_address(reference.source_function)}});
    }
    for (const auto& value : snapshot.strings) {
        check_cancelled(stop);
        byte_strings.append(QJsonObject{{"file_offset", hex(value.file_offset)}, {"value", value.value}});
    }
    if (!std::isfinite(snapshot.entropy) || snapshot.entropy < 0 || snapshot.entropy > 8) invalid(QStringLiteral("entropia inválida"));
    return {{"schema", 1}, {"metadata", metadata_json(snapshot.metadata, stop)}, {"options", options_json(snapshot.options)},
        {"functions", functions}, {"references", references}, {"strings", byte_strings}, {"entropy", snapshot.entropy},
        {"strings_truncated", snapshot.strings_truncated}, {"state", snapshot.state}, {"engine_version", snapshot.engine_version},
        {"capabilities", strings(snapshot.capabilities)}, {"warnings", strings(snapshot.warnings)}, {"revision", hex(snapshot.revision)}};
}

AnalysisSnapshot snapshot_from_json(const QJsonObject& object, std::stop_token stop) {
    check_cancelled(stop);
    if (integer(object, "schema", 1, 1) != 1) invalid(QStringLiteral("versão não suportada"));
    AnalysisSnapshot result;
    result.metadata = read_metadata(persistence::object(field(object, "metadata")), stop);
    result.options = read_options(persistence::object(field(object, "options")));
    quint64 instruction_count = 0;
    QSet<QString> function_addresses;
    for (const auto& value : array(field(object, "functions"), 10000)) {
        check_cancelled(stop);
        const auto item = persistence::object(value);
        Function function;
        function.address = address_from_json(persistence::object(field(item, "address")));
        const auto key = function.address.space + QChar::Null + hex(function.address.value);
        if (function_addresses.contains(key)) invalid(QStringLiteral("função duplicada"));
        function_addresses.insert(key);
        function.name = string(item, "name");
        const auto instructions = array(field(item, "instructions"), 200000);
        instruction_count += quint64(instructions.size());
        if (instruction_count > 200000) invalid(QStringLiteral("limite total de instruções excedido"));
        for (const auto& instruction : instructions) {
            check_cancelled(stop);
            function.instructions.append(read_instruction(persistence::object(instruction)));
        }
        for (const auto& value_block : array(field(item, "blocks"), 200000)) {
            check_cancelled(stop);
            const auto block_object = persistence::object(value_block);
            BasicBlock block;
            block.address = address_from_json(persistence::object(field(block_object, "address")));
            for (const auto& index_value : array(field(block_object, "instruction_indices"), 200000)) {
                const auto index = unsigned_hex(string(index_value, 18));
                if (index >= quint64(function.instructions.size())) invalid(QStringLiteral("índice de bloco fora da função"));
                block.instruction_indices.append(static_cast<quint32>(index));
            }
            block.successors = read_addresses(field(block_object, "successors"));
            function.blocks.append(std::move(block));
        }
        function.partial = boolean(item, "partial");
        function.warnings = read_strings(field(item, "warnings"));
        result.functions.append(std::move(function));
    }
    for (const auto& value : array(field(object, "references"), 2000000)) {
        check_cancelled(stop);
        const auto item = persistence::object(value);
        result.references.append(Reference{address_from_json(persistence::object(field(item, "source"))),
            address_from_json(persistence::object(field(item, "target"))), string(item, "type"), read_optional_address(field(item, "source_function"))});
    }
    for (const auto& value : array(field(object, "strings"), 10000)) {
        check_cancelled(stop);
        const auto item = persistence::object(value);
        result.strings.append(ByteString{number(item, "file_offset"), string(item, "value")});
    }
    const auto entropy = field(object, "entropy");
    if (!entropy.isDouble() || !std::isfinite(entropy.toDouble()) || entropy.toDouble() < 0 || entropy.toDouble() > 8)
        invalid(QStringLiteral("entropia inválida"));
    result.entropy = entropy.toDouble();
    result.strings_truncated = boolean(object, "strings_truncated");
    result.state = string(object, "state", 128);
    result.engine_version = string(object, "engine_version", 4096);
    result.capabilities = read_strings(field(object, "capabilities"));
    result.warnings = read_strings(field(object, "warnings"));
    result.revision = number(object, "revision");
    return result;
}

} // namespace reworkbench::persistence
