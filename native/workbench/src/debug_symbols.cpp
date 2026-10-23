#include "reworkbench/enrichment.hpp"
#include "enrichment_internal.hpp"

#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStringConverter>

#include <bit>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

namespace reworkbench {
namespace {
using namespace enrichment_internal;
constexpr quint64 maximum_source_rows = 20000;
constexpr quint64 maximum_source_text = 1024 * 1024;
constexpr quint64 maximum_line_steps = 1000000;
constexpr quint64 maximum_map_symbols = 100000;

struct Cursor {
    const QByteArray& data;
    quint64 position = 0;
    quint64 end = 0;
    ByteOrder order = ByteOrder::Little;

    quint64 integer(unsigned width) {
        range(position, width, end);
        const auto result = number(data, position, width, order);
        position += width;
        return result;
    }
    quint64 uleb() {
        quint64 result = 0;
        for (unsigned index = 0; index < 10; ++index) {
            const auto byte = integer(1);
            const auto payload = byte & 0x7f;
            if (index == 9 && payload > 1) invalid(QStringLiteral("ULEB128 excede 64 bits."));
            result |= payload << (index * 7);
            if ((byte & 0x80) == 0) return result;
        }
        invalid(QStringLiteral("ULEB128 sem terminador."));
    }
    qint64 sleb() {
        quint64 result = 0;
        for (unsigned index = 0; index < 10; ++index) {
            const auto byte = integer(1);
            const auto payload = byte & 0x7f;
            if (index == 9 && payload != 0 && payload != 0x7f)
                invalid(QStringLiteral("SLEB128 excede 64 bits."));
            if (index == 9) result |= (payload & 1) << 63;
            else result |= payload << (index * 7);
            if ((byte & 0x80) == 0) {
                const auto shift = (index + 1) * 7;
                if (shift < 64 && (byte & 0x40) != 0) result |= ~quint64(0) << shift;
                return std::bit_cast<qint64>(result);
            }
        }
        invalid(QStringLiteral("SLEB128 sem terminador."));
    }
    QString text() {
        range(position, 1, end);
        const auto available = std::min(name_limit, end - position);
        const auto window = data.mid(static_cast<qsizetype>(position), static_cast<qsizetype>(available));
        const auto nul = window.indexOf('\0');
        if (nul < 0)
            invalid(QStringLiteral("texto DWARF sem terminador dentro do limite."));
        const auto value = QString::fromUtf8(window.constData(), nul);
        position += static_cast<quint64>(nul) + 1;
        return value;
    }
};

struct DebugBudget {
    quint64 steps = 0;
    quint64 text = 0;
    quint64 units = 0;
    QSet<QString> paths;
    std::set<std::tuple<quint64, QString, quint64, quint64, quint64, bool>> rows;

    void record_text(const QString& value) {
        text = add(text, static_cast<quint64>(value.toUtf8().size()));
        bounded(text, maximum_source_text, QStringLiteral("Texto de linhas DWARF"));
    }
};

struct LineFile { QString name; quint64 directory = 0; };

LineFile line_file(Cursor& cursor, DebugBudget& budget) {
    LineFile file;
    file.name = cursor.text();
    if (file.name.isEmpty()) return file;
    budget.record_text(file.name);
    file.directory = cursor.uleb();
    cursor.uleb(); // timestamp
    cursor.uleb(); // recorded file size
    return file;
}

qint64 add_line(qint64 value, qint64 delta) {
    if ((delta > 0 && value > std::numeric_limits<qint64>::max() - delta) ||
        (delta < 0 && value < std::numeric_limits<qint64>::min() - delta))
        invalid(QStringLiteral("linha DWARF excede 64 bits."));
    return value + delta;
}

void line_unit(Cursor& cursor, quint64 version, unsigned offset_width,
               const BinaryMetadata& metadata, DebugSymbols& output, DebugBudget& budget,
               std::stop_token stop) {
    const auto header_length = cursor.integer(offset_width);
    range(cursor.position, header_length, cursor.end);
    const auto header_end = cursor.position + header_length;
    Cursor header{cursor.data, cursor.position, header_end, cursor.order};
    const auto minimum_instruction = header.integer(1);
    const auto maximum_operations = version >= 4 ? header.integer(1) : quint64(1);
    const auto default_statement = header.integer(1);
    const auto encoded_base = header.integer(1);
    const qint64 line_base = encoded_base >= 128 ? static_cast<qint64>(encoded_base) - 256 : static_cast<qint64>(encoded_base);
    const auto line_range = header.integer(1);
    const auto opcode_base = header.integer(1);
    if (minimum_instruction == 0 || minimum_instruction > 16 || maximum_operations != 1 ||
        default_statement > 1 || line_range == 0 || opcode_base == 0)
        invalid(QStringLiteral("prologue DWARF inválido ou VLIW com múltiplas operações ainda não suportado."));
    QVector<quint64> operand_lengths;
    operand_lengths.push_back(0);
    for (quint64 index = 1; index < opcode_base; ++index) operand_lengths.push_back(header.integer(1));
    QVector<QString> directories;
    QVector<LineFile> files;
    while (header.position < header.end) {
        check_cancelled(stop);
        auto directory = header.text();
        if (directory.isEmpty()) break;
        bounded(static_cast<quint64>(directories.size()) + 1, 65536, QStringLiteral("Diretórios DWARF"));
        budget.record_text(directory);
        directories.push_back(std::move(directory));
    }
    bool files_terminated = false;
    while (header.position < header.end) {
        check_cancelled(stop);
        auto file = line_file(header, budget);
        if (file.name.isEmpty()) { files_terminated = true; break; }
        bounded(static_cast<quint64>(files.size()) + 1, 65536, QStringLiteral("Arquivos DWARF"));
        if (file.directory > static_cast<quint64>(directories.size())) invalid(QStringLiteral("arquivo DWARF referencia diretório inexistente."));
        files.push_back(std::move(file));
    }
    if (!files_terminated || header.position != header.end)
        invalid(QStringLiteral("tabelas de nomes DWARF incompletas ou com bytes extras."));
    cursor.position = header_end;
    quint64 address = 0, file_index = 1, column = 0, discriminator = 0;
    qint64 line = 1;
    bool statement = default_statement != 0, address_known = false, in_sequence = false;
    const auto reset = [&] {
        address = 0; file_index = 1; column = 0; discriminator = 0; line = 1;
        statement = default_statement != 0; address_known = false; in_sequence = false;
    };
    const auto advance = [&](quint64 operations) { address = add(address, multiply(minimum_instruction, operations)); };
    const auto emit_row = [&] {
        in_sequence = true;
        if (!address_known || line <= 0 || file_index == 0 || file_index > static_cast<quint64>(files.size())) {
            warning(output.warnings, QStringLiteral("Linhas DWARF sem endereço declarado, linha positiva ou arquivo válido foram omitidas."));
            discriminator = 0;
            return;
        }
        const auto location = ram(address);
        if (!backed_code(metadata, location)) { discriminator = 0; return; }
        const auto& file = files[static_cast<qsizetype>(file_index - 1)];
        QString path = file.name;
        if (!QDir::isAbsolutePath(path) && file.directory != 0)
            path = directories[static_cast<qsizetype>(file.directory - 1)] + QLatin1Char('/') + path;
        path = QDir::cleanPath(path);
        if (static_cast<quint64>(path.toUtf8().size()) >= name_limit)
            invalid(QStringLiteral("caminho DWARF composto excede 4095 bytes."));
        if (!budget.paths.contains(path)) { budget.record_text(path); budget.paths.insert(path); }
        const auto key = std::make_tuple(address, path, static_cast<quint64>(line), column, discriminator, statement);
        if (!budget.rows.contains(key)) {
            bounded(static_cast<quint64>(output.lines.size()) + 1, maximum_source_rows, QStringLiteral("Linhas DWARF"));
            budget.rows.insert(key);
            output.lines.push_back({location, std::move(path), static_cast<quint64>(line), column, discriminator, statement});
        }
        discriminator = 0;
    };
    while (cursor.position < cursor.end) {
        check_cancelled(stop);
        bounded(++budget.steps, maximum_line_steps, QStringLiteral("Operações da máquina de linhas DWARF"));
        const auto opcode = cursor.integer(1);
        if (opcode == 0) {
            const auto length = cursor.uleb();
            if (length == 0) invalid(QStringLiteral("opcode DWARF estendido sem conteúdo."));
            range(cursor.position, length, cursor.end);
            Cursor extended{cursor.data, cursor.position, cursor.position + length, cursor.order};
            const auto code = extended.integer(1);
            if (code == 1) {
                if (extended.position != extended.end) invalid(QStringLiteral("end_sequence DWARF com conteúdo extra."));
                reset();
            } else if (code == 2) {
                const auto width = metadata.bits == 64 ? 8u : 4u;
                if (extended.end - extended.position != width) invalid(QStringLiteral("set_address DWARF possui largura diferente da imagem."));
                address = extended.integer(width);
                address_known = true;
                in_sequence = true;
            } else if (code == 3) {
                auto file = line_file(extended, budget);
                if (file.name.isEmpty() || file.directory > static_cast<quint64>(directories.size()))
                    invalid(QStringLiteral("define_file DWARF inválido."));
                bounded(static_cast<quint64>(files.size()) + 1, 65536, QStringLiteral("Arquivos DWARF"));
                files.push_back(std::move(file));
            } else if (code == 4 && version >= 4) discriminator = extended.uleb();
            else invalid(QStringLiteral("opcode estendido DWARF não modelado; unidade interrompida para preservar a precisão dos endereços."));
            if (extended.position != extended.end) invalid(QStringLiteral("opcode estendido DWARF com conteúdo extra."));
            cursor.position = extended.end;
            continue;
        }
        if (opcode >= opcode_base) {
            const auto adjusted = opcode - opcode_base;
            advance(adjusted / line_range);
            line = add_line(line, line_base + static_cast<qint64>(adjusted % line_range));
            emit_row();
            continue;
        }
        // Known standard opcodes have defined operand counts in the prologue.
        const quint64 expected = (opcode == 2 || opcode == 3 || opcode == 4 || opcode == 5 || opcode == 9 || opcode == 12) ? 1 : 0;
        if (operand_lengths[static_cast<qsizetype>(opcode)] != expected)
            invalid(QStringLiteral("contagem de operandos DWARF padrão inválida."));
        switch (opcode) {
        case 1: emit_row(); break;
        case 2: advance(cursor.uleb()); break;
        case 3: line = add_line(line, cursor.sleb()); break;
        case 4: file_index = cursor.uleb(); break;
        case 5: column = cursor.uleb(); break;
        case 6: statement = !statement; break;
        case 7: break; // basic_block affects presentation flags, not a row's location
        case 8: advance((255 - opcode_base) / line_range); break;
        case 9: address = add(address, cursor.integer(2)); break;
        case 10:
        case 11:
            if (version < 3) invalid(QStringLiteral("opcode de prologue/epilogue fora da versão DWARF."));
            break;
        case 12:
            if (version < 3 || cursor.uleb() != 0)
                invalid(QStringLiteral("DWARF seleciona ISA não representada pelo modo global da imagem."));
            break;
        default: invalid(QStringLiteral("opcode DWARF padrão não modelado; unidade interrompida."));
        }
    }
    if (in_sequence) warning(output.warnings, QStringLiteral("Uma sequência DWARF terminou sem end_sequence; somente endereços exatos das linhas anteriores são apresentados."));
}

bool allowed_name(const QString& name) {
    if (name.isEmpty() || name.trimmed() != name || static_cast<quint64>(name.toUtf8().size()) >= name_limit) return false;
    for (qsizetype index = 0; index < name.size(); ++index) {
        const auto character = name[index];
        if (character.isHighSurrogate()) {
            if (++index >= name.size() || !name[index].isLowSurrogate()) return false;
            continue;
        }
        if (character.isLowSurrogate()) return false;
        if (character.category() == QChar::Other_Control || character.category() == QChar::Other_Format ||
            character.category() == QChar::Separator_Line || character.category() == QChar::Separator_Paragraph)
            return false;
    }
    return true;
}

Address map_address(const QString& text) {
    static const QRegularExpression pattern(QStringLiteral("^(?:ram:)?0x([0-9A-Fa-f]{1,16})$"));
    const auto match = pattern.match(text);
    if (!match.hasMatch()) invalid(QStringLiteral("endereço de mapa deve ser ram:0x... ou 0x..., em até 64 bits."));
    bool ok = false;
    const auto value = match.captured(1).toULongLong(&ok, 16);
    if (!ok) invalid(QStringLiteral("endereço de mapa inválido."));
    return ram(value);
}

void json_shape(const QByteArray& data) {
    // Bound nesting before handing data to Qt's JSON parser. A symbol map needs
    // only root object -> symbols array -> symbol object, never nested payloads.
    int depth = 0;
    bool quoted = false, escaped = false;
    qsizetype string_start = 0;
    QVector<QSet<QByteArray>> object_keys;
    for (qsizetype index = 0; index < data.size(); ++index) {
        const auto byte = data[index];
        if (quoted) {
            if (escaped) escaped = false;
            else if (byte == '\\') escaped = true;
            else if (byte == '"') {
                quoted = false;
                auto next = index + 1;
                while (next < data.size() && (data[next] == ' ' || data[next] == '\r' || data[next] == '\n' || data[next] == '\t')) ++next;
                if (next < data.size() && data[next] == ':') {
                    if (object_keys.isEmpty()) invalid(QStringLiteral("chave JSON fora de objeto."));
                    const auto key = data.mid(string_start, index - string_start);
                    if (key.isEmpty() || key.size() > 32 || key.contains('\\'))
                        invalid(QStringLiteral("chaves do mapa JSON devem ser nomes ASCII literais sem escapes."));
                    for (const auto character : key)
                        if ((character < 'a' || character > 'z') && (character < '0' || character > '9') && character != '_')
                            invalid(QStringLiteral("chave do mapa JSON não é ASCII canônico."));
                    if (object_keys.back().contains(key)) invalid(QStringLiteral("chave duplicada no mapa JSON."));
                    object_keys.back().insert(key);
                }
            }
        } else if (byte == '"') { quoted = true; string_start = index + 1; }
        else if (byte == '{' || byte == '[') {
            if (++depth > 3) invalid(QStringLiteral("mapa JSON possui estruturas aninhadas além do esquema."));
            if (byte == '{') object_keys.push_back({});
        } else if (byte == '}' || byte == ']') {
            if (--depth < 0) invalid(QStringLiteral("mapa JSON não está balanceado."));
            if (byte == '}') {
                if (object_keys.isEmpty()) invalid(QStringLiteral("fechamento de objeto JSON inválido."));
                object_keys.pop_back();
            }
        }
    }
    if (depth != 0 || quoted) invalid(QStringLiteral("mapa JSON truncado."));
}

void keys(const QJsonObject& object, const QSet<QString>& accepted) {
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator)
        if (!accepted.contains(iterator.key())) invalid(QStringLiteral("campo desconhecido no mapa JSON: ") + iterator.key());
}

struct MapCollector {
    SymbolMapImport result;
    const BinaryMetadata& metadata;
    quint64 text = 0;
    std::set<std::tuple<quint64, QString, QString>> seen;
    QSet<quint64> entries;

    explicit MapCollector(const BinaryMetadata& value) : metadata(value) {}
    void symbol(const QString& address_text, QString name, const QString& kind) {
        if (kind != QStringLiteral("function") && kind != QStringLiteral("symbol"))
            invalid(QStringLiteral("kind do mapa deve ser function ou symbol."));
        if (!allowed_name(name)) invalid(QStringLiteral("nome do mapa está vazio, possui controles ou excede 4095 bytes."));
        const auto address = map_address(address_text);
        if (!file_offset(metadata, address)) invalid(QStringLiteral("símbolo fora dos bytes mapeados: ") + address.text());
        if (kind == QStringLiteral("function") && !backed_code(metadata, address))
            invalid(QStringLiteral("função do mapa fora do código mapeado ou desalinhada: ") + address.text());
        const auto key = std::make_tuple(address.value, name, kind);
        if (seen.contains(key)) return;
        bounded(static_cast<quint64>(result.symbols.size()) + 1, maximum_map_symbols, QStringLiteral("Símbolos do mapa"));
        text = add(text, static_cast<quint64>(name.toUtf8().size()));
        bounded(text, block_limit, QStringLiteral("Nomes do mapa"));
        seen.insert(key);
        result.symbols.push_back({address, std::move(name), kind});
        if (kind == QStringLiteral("function") && !entries.contains(address.value)) {
            entries.insert(address.value);
            result.entries.push_back(address);
        }
    }
};

} // namespace

DebugSymbols decode_debug_symbols(const std::shared_ptr<BinaryFile>& file,
                                  const BinaryMetadata& metadata, std::stop_token stop) {
    if (!file) throw std::invalid_argument("decode_debug_symbols requires a BinaryFile");
    if (file->size() != metadata.size) throw std::invalid_argument("BinaryFile size differs from loaded metadata");
    DebugSymbols output;
    if (metadata.format != BinaryFormat::Elf) return output;
    const auto image = elf_sections(file, stop);
    DebugBudget budget;
    quint64 section_bytes = 0;
    QJsonArray versions;
    bool found = false;
    for (const auto& section : image.sections) {
        check_cancelled(stop);
        if (section.label == QStringLiteral(".zdebug_line") ||
            (section.label == QStringLiteral(".debug_line") && (section.flags & 0x800) != 0)) {
            warning(output.warnings, QStringLiteral("Linhas DWARF comprimidas não são decodificadas."));
            continue;
        }
        if (section.label != QStringLiteral(".debug_line")) continue;
        found = true;
        if (section.type != 1) { warning(output.warnings, QStringLiteral(".debug_line sem SHT_PROGBITS foi omitida.")); continue; }
        section_bytes = add(section_bytes, section.size);
        bounded(section_bytes, block_limit, QStringLiteral("Seções de linhas DWARF"));
        const auto data = read(file, section.offset, section.size);
        Cursor section_cursor{data, 0, section.size, image.order};
        while (section_cursor.position < section_cursor.end) {
            check_cancelled(stop);
            bounded(++budget.units, 4096, QStringLiteral("Unidades DWARF"));
            auto length = section_cursor.integer(4);
            unsigned offset_width = 4;
            if (length == 0xffffffff) { length = section_cursor.integer(8); offset_width = 8; }
            else if (length >= 0xfffffff0) invalid(QStringLiteral("comprimento DWARF reservado."));
            if (length == 0) {
                warning(output.warnings, QStringLiteral("Unidade DWARF de tamanho zero foi omitida."));
                continue;
            }
            range(section_cursor.position, length, section_cursor.end);
            const auto unit_end = section_cursor.position + length;
            Cursor unit{data, section_cursor.position, unit_end, image.order};
            try {
                const auto version = unit.integer(2);
                if (!versions.contains(static_cast<int>(version))) versions.push_back(static_cast<int>(version));
                if (version >= 2 && version <= 4) line_unit(unit, version, offset_width, metadata, output, budget, stop);
                else warning(output.warnings, QStringLiteral("Versão de linhas DWARF não suportada: ") + QString::number(version) + QStringLiteral(". Esta versão lê somente DWARF 2, 3 e 4."));
            } catch (const Cancelled&) { throw; }
            catch (const std::exception& error) {
                warning(output.warnings, QStringLiteral("Unidade de linhas DWARF parcial: ") + QString::fromUtf8(error.what()));
            }
            section_cursor.position = unit_end;
            if (budget.steps >= maximum_line_steps || output.lines.size() >= static_cast<qsizetype>(maximum_source_rows) ||
                budget.text >= maximum_source_text) {
                warning(output.warnings, QStringLiteral("Leitura DWARF interrompida pelos limites globais de linhas, operações ou texto."));
                break;
            }
        }
        if (budget.steps >= maximum_line_steps || output.lines.size() >= static_cast<qsizetype>(maximum_source_rows) ||
            budget.text >= maximum_source_text) break;
    }
    if (found) {
        output.details = QJsonObject{{QStringLiteral("format"), QStringLiteral("ELF DWARF exact source rows")},
            {QStringLiteral("versions"), versions}, {QStringLiteral("units"), QString::number(budget.units)},
            {QStringLiteral("rows"), QString::number(output.lines.size())},
            {QStringLiteral("source_files_opened"), false}, {QStringLiteral("type_recovery"), false}};
        warning(output.warnings, QStringLiteral("Linhas são registros exatos de .debug_line, sem inferência de intervalos. DW_AT_comp_dir, DIEs, tipos, inlining, DWARF 5, split DWARF e arquivos externos não são interpretados; caminhos gravados são apenas metadados."));
    }
    std::stable_sort(output.lines.begin(), output.lines.end(), [](const auto& left, const auto& right) {
        return left.address.value < right.address.value;
    });
    return output;
}

SymbolMapImport import_symbol_map(const QString& path, const BinaryMetadata& metadata,
                                  SymbolMapFormat format, std::stop_token stop) {
    check_cancelled(stop);
    const auto file = std::make_shared<BinaryFile>(path);
    bounded(file->size(), block_limit, QStringLiteral("Arquivo de mapa"));
    auto data = read(file, 0, file->size());
    if (data.startsWith(QByteArray("\xef\xbb\xbf", 3))) data.remove(0, 3);
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString text = decoder(data);
    if (decoder.hasError()) invalid(QStringLiteral("mapa deve usar UTF-8 válido."));
    if (text.contains(QChar::Null)) invalid(QStringLiteral("mapa possui byte NUL."));
    if (format == SymbolMapFormat::Auto)
        format = text.trimmed().startsWith(QLatin1Char('{')) ? SymbolMapFormat::Json : SymbolMapFormat::Text;
    MapCollector output(metadata);
    if (format == SymbolMapFormat::Json) {
        json_shape(data);
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) invalid(QStringLiteral("JSON de mapa inválido: ") + error.errorString());
        const auto root = document.object();
        keys(root, {QStringLiteral("format"), QStringLiteral("version"), QStringLiteral("sha256"), QStringLiteral("symbols")});
        if (root.value(QStringLiteral("format")).toString() != QStringLiteral("reworkbench-symbols") ||
            !root.value(QStringLiteral("version")).isDouble() || root.value(QStringLiteral("version")).toDouble() != 1 ||
            !root.value(QStringLiteral("symbols")).isArray()) invalid(QStringLiteral("esquema de mapa JSON não suportado."));
        if (root.contains(QStringLiteral("sha256"))) {
            static const QRegularExpression digest_pattern(QStringLiteral("^[0-9A-Fa-f]{64}$"));
            const auto digest = root.value(QStringLiteral("sha256"));
            if (!digest.isString() || !digest_pattern.match(digest.toString()).hasMatch() || metadata.sha256.isEmpty() ||
                digest.toString().compare(metadata.sha256, Qt::CaseInsensitive) != 0)
                invalid(QStringLiteral("SHA-256 do mapa não corresponde à amostra carregada."));
            output.result.details.insert(QStringLiteral("sha256_matched"), true);
        } else warning(output.result.warnings, QStringLiteral("Mapa JSON sem SHA-256: identidade da amostra não foi comprovada pelo mapa."));
        const auto symbols = root.value(QStringLiteral("symbols")).toArray();
        bounded(static_cast<quint64>(symbols.size()), maximum_map_symbols, QStringLiteral("Registros do mapa JSON"));
        for (const auto value : symbols) {
            check_cancelled(stop);
            if (!value.isObject()) invalid(QStringLiteral("símbolo JSON deve ser objeto."));
            const auto item = value.toObject();
            keys(item, {QStringLiteral("address"), QStringLiteral("name"), QStringLiteral("kind")});
            if (!item.value(QStringLiteral("address")).isString() || !item.value(QStringLiteral("name")).isString() ||
                !item.value(QStringLiteral("kind")).isString()) invalid(QStringLiteral("campos de símbolo JSON devem ser strings."));
            output.symbol(item.value(QStringLiteral("address")).toString(), item.value(QStringLiteral("name")).toString(),
                          item.value(QStringLiteral("kind")).toString());
        }
    } else if (format == SymbolMapFormat::Text) {
        static const QRegularExpression line_pattern(QStringLiteral("^((?:ram:)?0x[0-9A-Fa-f]{1,16})\\s+(function|symbol)\\s+(.+)$"));
        qsizetype position = 0;
        quint64 lines = 0;
        while (position < text.size()) {
            check_cancelled(stop);
            bounded(++lines, 200000, QStringLiteral("Linhas do mapa de texto"));
            auto end = text.indexOf(QLatin1Char('\n'), position);
            if (end < 0) end = text.size();
            const auto line = text.mid(position, end - position).trimmed();
            position = end + 1;
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
            const auto match = line_pattern.match(line);
            if (!match.hasMatch()) invalid(QStringLiteral("linha de mapa inválida: ") + QString::number(lines));
            output.symbol(match.captured(1), match.captured(3), match.captured(2));
        }
        warning(output.result.warnings, QStringLiteral("Mapa de texto não vincula SHA-256; os nomes são fornecidos pelo analista e não comprovam fronteiras ou tipos de função."));
    } else throw std::invalid_argument("unknown SymbolMapFormat");
    output.result.details.insert(QStringLiteral("format"), format == SymbolMapFormat::Json ? QStringLiteral("reworkbench-symbols/1") : QStringLiteral("reworkbench-symbol-text/1"));
    output.result.details.insert(QStringLiteral("imported_symbols"), QString::number(output.result.symbols.size()));
    output.result.details.insert(QStringLiteral("source"), file->path());
    output.result.details.insert(QStringLiteral("applied_to_binary"), false);
    return std::move(output.result);
}

} // namespace reworkbench
