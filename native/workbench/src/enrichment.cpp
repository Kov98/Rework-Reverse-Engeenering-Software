#include "reworkbench/enrichment.hpp"
#include "enrichment_internal.hpp"

#include <QJsonArray>
#include <QMap>
#include <map>
#include <set>
#include <utility>

namespace reworkbench {
namespace {
using namespace enrichment_internal;

struct Collector {
    BinaryEnrichment result;
    const BinaryMetadata& metadata;
    QSet<quint64> entries;
    QSet<QString> references;
    QSet<QString> imports;
    QSet<quint64> relocated;
    QJsonArray relocation_slots;
    bool relocation_complete = false;
    quint64 text_bytes = 0;

    explicit Collector(const BinaryMetadata& value) : metadata(value) {
        relocation_complete = value.format == BinaryFormat::Raw;
        for (const auto& entry : value.entries) entries.insert(entry.value);
        for (const auto& imported : value.imports) {
            if (imported.slot) imports.insert(imported.name + QLatin1Char('|') + imported.slot->text());
        }
    }

    void relocation_slot(Address address, unsigned width = 1) {
        if (!file_offset(metadata, address, width) || relocated.contains(address.value)) return;
        bounded(static_cast<quint64>(relocation_slots.size()) + 1, record_limit, QStringLiteral("Locais de relocação"));
        relocated.insert(address.value);
        relocation_slots.push_back(address.text());
    }

    void entry(Address address, const QString& name) {
        if (!backed_code(metadata, address) || entries.contains(address.value)) return;
        bounded(static_cast<quint64>(result.entries.size()) + 1, record_limit, QStringLiteral("Entradas adicionais"));
        entries.insert(address.value);
        result.entries.push_back(address);
        result.symbols.push_back({address, name, QStringLiteral("function")});
    }

    void reference(Address source, Address target, const QString& kind) {
        if (!file_offset(metadata, source) || !file_offset(metadata, target)) return;
        const auto key = source.text() + QLatin1Char('|') + target.text() + QLatin1Char('|') + kind;
        if (references.contains(key)) return;
        bounded(static_cast<quint64>(result.references.size()) + 1, record_limit, QStringLiteral("Referências adicionais"));
        references.insert(key);
        result.references.push_back({source, target, kind, std::nullopt});
    }

    void imported(QString name, Address slot) {
        if (!file_offset(metadata, slot)) return;
        const auto key = name + QLatin1Char('|') + slot.text();
        if (imports.contains(key)) return;
        bounded(static_cast<quint64>(result.imports.size()) + 1, 65536, QStringLiteral("Slots de importação"));
        text_bytes = add(text_bytes, static_cast<quint64>(name.toUtf8().size()));
        bounded(text_bytes, block_limit, QStringLiteral("Nomes de importação"));
        imports.insert(key);
        result.imports.push_back({std::move(name), {}, slot});
    }
};

struct PeDirectory { quint64 rva = 0, size = 0; };
struct PeImage {
    bool wide = false;
    QVector<PeDirectory> directories;
};

PeImage pe_directories(const std::shared_ptr<BinaryFile>& file) {
    const auto dos = read(file, 0, 64);
    if (dos.left(2) != QByteArray("MZ", 2)) invalid(QStringLiteral("assinatura DOS inválida."));
    const auto nt_offset = number(dos, 60, 4, ByteOrder::Little);
    const auto nt = read(file, nt_offset, 24);
    if (nt.left(4) != QByteArray("PE\0\0", 4)) invalid(QStringLiteral("assinatura PE inválida."));
    const auto optional = read(file, add(nt_offset, 24), number(nt, 20, 2, ByteOrder::Little));
    const auto magic = number(optional, 0, 2, ByteOrder::Little);
    if (magic != 0x10b && magic != 0x20b) invalid(QStringLiteral("cabeçalho PE32/PE32+ ausente."));
    PeImage image;
    image.wide = magic == 0x20b;
    const quint64 base = image.wide ? 112 : 96;
    range(0, base, static_cast<quint64>(optional.size()));
    const auto count = number(optional, base - 4, 4, ByteOrder::Little);
    if (count > (static_cast<quint64>(optional.size()) - base) / 8)
        invalid(QStringLiteral("diretórios PE fora do cabeçalho opcional."));
    bounded(count, 8192, QStringLiteral("Diretórios PE"));
    for (quint64 i = 0; i < count; ++i) image.directories.push_back({
        number(optional, base + i * 8, 4, ByteOrder::Little),
        number(optional, base + i * 8 + 4, 4, ByteOrder::Little)});
    return image;
}

PeDirectory directory(const PeImage& image, qsizetype index) {
    return index < image.directories.size() ? image.directories[index] : PeDirectory{};
}

quint64 pe_va(const BinaryMetadata& metadata, quint64 rva) { return add(metadata.image_base, rva); }

void pe_functions(const std::shared_ptr<BinaryFile>& file, const BinaryMetadata& metadata,
                  const PeImage& image, Collector& output, std::stop_token stop) {
    const auto table = directory(image, 3);
    if (table.rva == 0 || table.size == 0) return;
    if (metadata.architecture != Architecture::X64 || !image.wide) {
        warning(output.result.warnings, QStringLiteral("A tabela de exceções PE está presente; somente registros AMD64 RUNTIME_FUNCTION são interpretados."));
        return;
    }
    if (table.size % 12 != 0) invalid(QStringLiteral("tabela AMD64 RUNTIME_FUNCTION não é múltipla de 12 bytes."));
    bounded(table.size / 12, record_limit, QStringLiteral("Funções de exceção PE"));
    const auto data = mapped_read(file, metadata, pe_va(metadata, table.rva), table.size);
    QJsonArray ranges;
    quint64 accepted = 0, previous = 0;
    bool have_previous = false;
    for (quint64 at = 0; at < table.size; at += 12) {
        check_cancelled(stop);
        const auto begin = number(data, at, 4, ByteOrder::Little);
        const auto end = number(data, at + 4, 4, ByteOrder::Little);
        const auto unwind = number(data, at + 8, 4, ByteOrder::Little);
        if (begin == 0 && end == 0 && unwind == 0) continue;
        if (have_previous && begin < previous)
            warning(output.result.warnings, QStringLiteral("A tabela de exceções AMD64 não está ordenada por endereço inicial."));
        previous = begin;
        have_previous = true;
        const auto start = ram(pe_va(metadata, begin));
        if (end <= begin || !backed_code(metadata, start) ||
            !file_offset(metadata, start, end - begin) || unwind == 0 || (unwind & 3) != 0) {
            warning(output.result.warnings, QStringLiteral("Registros AMD64 com intervalo, alinhamento ou bytes inválidos foram omitidos."));
            continue;
        }
        const auto header = mapped_read(file, metadata, pe_va(metadata, unwind), 4);
        const auto version = number(header, 0, 1, ByteOrder::Little) & 7;
        const auto flags = number(header, 0, 1, ByteOrder::Little) >> 3;
        const auto unwind_slots = number(header, 2, 1, ByteOrder::Little);
        if ((version != 1 && version != 2) || (flags & ~quint64(7)) != 0 ||
            ((flags & 4) != 0 && (flags & 3) != 0)) {
            warning(output.result.warnings, QStringLiteral("UNWIND_INFO AMD64 com versão ou flags inválidas foi omitido."));
            continue;
        }
        // Validate the variable-length unwind-code storage without pretending
        // to recover a stack layout or execute the unwind instructions.
        const auto tail = add(4, multiply((unwind_slots + 1) & ~quint64(1), 2));
        const auto extra = (flags & 4) != 0 ? quint64(12) : ((flags & 3) != 0 ? quint64(4) : quint64(0));
        const auto record = mapped_read(file, metadata, pe_va(metadata, unwind), add(tail, extra));
        output.entry(start, QStringLiteral("unwind_%1").arg(begin, 0, 16));
        ++accepted;
        QJsonObject item{{QStringLiteral("start"), start.text()},
                         {QStringLiteral("end"), ram(pe_va(metadata, end)).text()},
                         {QStringLiteral("unwind"), ram(pe_va(metadata, unwind)).text()},
                         {QStringLiteral("version"), static_cast<int>(version)},
                         {QStringLiteral("flags"), static_cast<int>(flags)}};
        if ((flags & 3) != 0) {
            const auto handler = ram(pe_va(metadata, number(record, tail, 4, ByteOrder::Little)));
            if (backed_code(metadata, handler)) {
                output.entry(handler, QStringLiteral("exception_handler_%1").arg(handler.value, 0, 16));
                output.reference(start, handler, QStringLiteral("exception-handler"));
                item.insert(QStringLiteral("handler"), handler.text());
            } else warning(output.result.warnings, QStringLiteral("Handler de exceção AMD64 sem código mapeado foi omitido."));
        }
        if ((flags & 4) != 0) {
            const auto chained_begin = number(record, tail, 4, ByteOrder::Little);
            const auto chained_end = number(record, tail + 4, 4, ByteOrder::Little);
            const auto chained_unwind = number(record, tail + 8, 4, ByteOrder::Little);
            const auto target = ram(pe_va(metadata, chained_begin));
            if (chained_end > chained_begin && backed_code(metadata, target) &&
                file_offset(metadata, target, chained_end - chained_begin) && chained_unwind != 0 &&
                (chained_unwind & 3) == 0 && file_offset(metadata, ram(pe_va(metadata, chained_unwind)), 4)) {
                output.reference(start, target, QStringLiteral("unwind-chain"));
                item.insert(QStringLiteral("chained_start"), target.text());
            } else warning(output.result.warnings, QStringLiteral("UNWIND_INFO encadeado com destino inválido foi omitido."));
        }
        if (static_cast<quint64>(ranges.size()) < detail_limit) ranges.push_back(item);
    }
    output.result.details.insert(QStringLiteral("pe_runtime_functions"), ranges);
    output.result.details.insert(QStringLiteral("pe_runtime_function_count"), QString::number(accepted));
    if (accepted > detail_limit) warning(output.result.warnings, QStringLiteral("A apresentação dos intervalos PE foi limitada a 20.000 registros; as entradas adicionais mantêm o limite global."));
    warning(output.result.warnings, QStringLiteral("RUNTIME_FUNCTION fornece entradas e intervalos de unwind; fragmentos encadeados não são prova de funções independentes. Instruções de unwind e layout da pilha não foram reconstruídos."));
}

void pe_tls(const std::shared_ptr<BinaryFile>& file, const BinaryMetadata& metadata,
            const PeImage& image, Collector& output, std::stop_token stop) {
    const auto table = directory(image, 9);
    if (table.rva == 0 || table.size == 0) return;
    const unsigned width = image.wide ? 8 : 4;
    if (table.size < (image.wide ? 40u : 24u)) invalid(QStringLiteral("diretório TLS PE truncado."));
    const auto data = mapped_read(file, metadata, pe_va(metadata, table.rva), image.wide ? 40 : 24);
    const auto callbacks = number(data, width * 3, width, ByteOrder::Little);
    if (callbacks == 0) return;
    QJsonArray values;
    bool terminated = false;
    for (quint64 index = 0; index < 1024; ++index) {
        check_cancelled(stop);
        const auto slot = add(callbacks, multiply(index, width));
        const auto pointer = number(mapped_read(file, metadata, slot, width), 0, width, ByteOrder::Little);
        if (pointer == 0) { terminated = true; break; }
        const auto target = code_pointer(metadata, pointer);
        if (!target) {
            warning(output.result.warnings, QStringLiteral("Callback TLS sem código mapeado ou em modo ARM/Thumb diferente foi omitido."));
            continue;
        }
        output.entry(*target, QStringLiteral("tls_callback_%1").arg(index));
        output.reference(ram(slot), *target, QStringLiteral("tls-callback"));
        values.push_back(QJsonObject{{QStringLiteral("slot"), ram(slot).text()}, {QStringLiteral("target"), target->text()}});
    }
    if (!terminated) warning(output.result.warnings, QStringLiteral("A tabela TLS não terminou dentro do limite de 1.024 callbacks."));
    output.result.details.insert(QStringLiteral("pe_tls_callbacks"), values);
}

QString guid_text(const QByteArray& data, quint64 at) {
    QString result = QStringLiteral("%1-%2-%3-")
        .arg(number(data, at, 4, ByteOrder::Little), 8, 16, QLatin1Char('0'))
        .arg(number(data, at + 4, 2, ByteOrder::Little), 4, 16, QLatin1Char('0'))
        .arg(number(data, at + 6, 2, ByteOrder::Little), 4, 16, QLatin1Char('0'));
    result += QString::fromLatin1(data.mid(static_cast<qsizetype>(at + 8), 2).toHex());
    result += QLatin1Char('-');
    result += QString::fromLatin1(data.mid(static_cast<qsizetype>(at + 10), 6).toHex());
    return result;
}

void pe_debug(const std::shared_ptr<BinaryFile>& file, const BinaryMetadata& metadata,
              const PeImage& image, Collector& output, std::stop_token stop) {
    const auto table = directory(image, 6);
    if (table.rva == 0 || table.size == 0) return;
    if (table.size % 28 != 0) invalid(QStringLiteral("diretório de debug PE não é múltiplo de 28 bytes."));
    bounded(table.size / 28, 4096, QStringLiteral("Diretórios de debug PE"));
    const auto data = mapped_read(file, metadata, pe_va(metadata, table.rva), table.size);
    QJsonArray records;
    for (quint64 at = 0; at < table.size; at += 28) {
        check_cancelled(stop);
        if (number(data, at + 12, 4, ByteOrder::Little) != 2) continue; // IMAGE_DEBUG_TYPE_CODEVIEW
        const auto size = number(data, at + 16, 4, ByteOrder::Little);
        const auto rva = number(data, at + 20, 4, ByteOrder::Little);
        const auto pointer = number(data, at + 24, 4, ByteOrder::Little);
        if (size < 25 || size > 65536 || pointer == 0) {
            warning(output.result.warnings, QStringLiteral("Registro CodeView sem conteúdo offline RSDS válido foi omitido."));
            continue;
        }
        const auto raw = read(file, pointer, size);
        if (raw.left(4) != QByteArray("RSDS", 4)) {
            warning(output.result.warnings, QStringLiteral("CodeView presente em formato diferente de RSDS; não interpretado."));
            continue;
        }
        if (rva != 0) {
            const auto mapped = file_offset(metadata, ram(pe_va(metadata, rva)), size);
            if (!mapped || *mapped != pointer) {
                warning(output.result.warnings, QStringLiteral("RVA e offset de CodeView discordam; identidade RSDS foi omitida."));
                continue;
            }
        }
        bounded(static_cast<quint64>(records.size()) + 1, 256, QStringLiteral("Identidades RSDS"));
        const auto guid = guid_text(raw, 4);
        const auto age = number(raw, 20, 4, ByteOrder::Little);
        const auto path = cstring(raw, 24);
        records.push_back(QJsonObject{{QStringLiteral("guid"), guid}, {QStringLiteral("age"), QString::number(age)},
            {QStringLiteral("recorded_path"), path},
            {QStringLiteral("symbol_server_key"), QString(guid).remove(QLatin1Char('-')).toUpper() + QString::number(age, 16).toUpper()}});
    }
    if (!records.isEmpty()) {
        output.result.details.insert(QStringLiteral("pe_codeview_rsds"), records);
        warning(output.result.warnings, QStringLiteral("Somente a identidade RSDS/PDB foi extraída. Caminhos são metadados; nenhum PDB foi aberto ou baixado e tipos/símbolos PDB não foram decodificados."));
    }
}

void pe_relocations(const std::shared_ptr<BinaryFile>& file, const BinaryMetadata& metadata,
                    const PeImage& image, Collector& output, std::stop_token stop) {
    const auto table = directory(image, 5);
    if (table.rva == 0 && table.size == 0) { output.relocation_complete = true; return; }
    if (table.rva == 0 || table.size == 0) invalid(QStringLiteral("diretório de relocação PE incompleto."));
    const auto data = mapped_read(file, metadata, pe_va(metadata, table.rva), table.size);
    quint64 at = 0, records = 0;
    QMap<quint64, quint64> kinds;
    bool all_types_known = true;
    while (at < table.size) {
        check_cancelled(stop);
        range(at, 8, table.size);
        const auto page = number(data, at, 4, ByteOrder::Little);
        const auto size = number(data, at + 4, 4, ByteOrder::Little);
        if (size < 8 || size % 4 != 0 || (page & 0xfff) != 0) invalid(QStringLiteral("bloco de relocação PE inválido."));
        range(at, size, table.size);
        for (quint64 offset = 8; offset < size; offset += 2) {
            check_cancelled(stop);
            bounded(++records, record_limit, QStringLiteral("Relocações PE"));
            const auto encoded = number(data, at + offset, 2, ByteOrder::Little);
            const auto type = encoded >> 12;
            ++kinds[type];
            if (type == 0) continue;
            const auto location = ram(pe_va(metadata, add(page, encoded & 0xfff)));
            output.relocation_slot(location);
            if (type == 4) {
                // HIGHADJ consumes a second 16-bit word containing an addend,
                // not another type/offset entry.
                range(at + offset, 4, at + size);
                offset += 2;
            }
            const unsigned width = type == 10 && metadata.architecture == Architecture::X64 ? 8 :
                (type == 3 && metadata.architecture == Architecture::X86 ? 4 : 0);
            if (width == 0) { all_types_known = false; continue; }
            const auto stored = number(mapped_read(file, metadata, location.value, width), 0, width, ByteOrder::Little);
            auto target = ram(stored);
            if (metadata.architecture == Architecture::Thumb) target.value &= ~quint64(1);
            output.reference(location, target, QStringLiteral("pe-preferred-relocation-pointer"));
        }
        at += size;
    }
    QJsonObject counts;
    for (auto it = kinds.cbegin(); it != kinds.cend(); ++it) counts.insert(QString::number(it.key()), QString::number(it.value()));
    output.result.details.insert(QStringLiteral("pe_base_relocation_types"), counts);
    output.result.details.insert(QStringLiteral("pe_base_relocation_count"), QString::number(records));
    output.relocation_complete = all_types_known;
    warning(output.result.warnings, QStringLiteral("Relocações PE são inventariadas no endereço preferido. Somente HIGHLOW x86 e DIR64 x64 produzem referências; nenhum patch ou rebasing é aplicado."));
}

struct ElfSymbol {
    QString name;
    quint64 value = 0, section = 0, type = 0;
};

ElfSymbol elf_symbol(const std::shared_ptr<BinaryFile>& file, const ElfImage& image,
                     quint64 table_index, quint64 index) {
    if (table_index >= static_cast<quint64>(image.sections.size())) invalid(QStringLiteral("índice de símbolos ELF inválido."));
    const auto& table = image.sections[static_cast<qsizetype>(table_index)];
    const auto minimum = image.wide ? quint64(24) : quint64(16);
    if ((table.type != 2 && table.type != 11) || table.stride < minimum || table.size % table.stride != 0 ||
        (table.flags & 0x800) != 0 || table.link >= static_cast<quint64>(image.sections.size()))
        invalid(QStringLiteral("tabela de símbolos usada em relocação ELF inválida."));
    bounded(table.size / table.stride, record_limit, QStringLiteral("Símbolos ELF de relocação"));
    if (index >= table.size / table.stride) invalid(QStringLiteral("relocação ELF referencia símbolo inexistente."));
    const auto& strings = image.sections[static_cast<qsizetype>(table.link)];
    if (strings.type != 3 || (strings.flags & 0x800) != 0) invalid(QStringLiteral("nomes ELF comprimidos ou inválidos."));
    const auto data = read(file, add(table.offset, multiply(index, table.stride)), minimum);
    const auto name = number(data, 0, 4, image.order);
    ElfSymbol result;
    result.value = number(data, image.wide ? 8 : 4, image.wide ? 8 : 4, image.order);
    result.section = number(data, image.wide ? 6 : 14, 2, image.order);
    result.type = number(data, image.wide ? 4 : 12, 1, image.order) & 15;
    if (name != 0) {
        if (name >= strings.size) invalid(QStringLiteral("nome de símbolo ELF fora da tabela."));
        result.name = cstring(read(file, add(strings.offset, name), std::min(name_limit, strings.size - name)), 0);
    }
    return result;
}

quint64 plus_signed(quint64 value, quint64 encoded, unsigned width) {
    const unsigned bits = width * 8;
    if ((encoded & (quint64(1) << (bits - 1))) == 0) return add(value, encoded);
    const auto magnitude = width == 8 ? (~encoded + 1) : ((~encoded & ((quint64(1) << bits) - 1)) + 1);
    if (magnitude > value) invalid(QStringLiteral("addend de relocação produz endereço negativo."));
    return value - magnitude;
}

void elf_relocations(const std::shared_ptr<BinaryFile>& file, const BinaryMetadata& metadata,
                     const ElfImage& image, Collector& output, std::stop_token stop) {
    if (image.sections.isEmpty()) {
        warning(output.result.warnings, QStringLiteral("Sem seções ELF, os locais de relocação dinâmica não podem ser inventariados."));
        return;
    }
    if (image.machine == 8 && image.wide) {
        warning(output.result.warnings, QStringLiteral("Relocações ELF MIPS64 possuem encoding específico e não são interpretadas."));
        return;
    }
    quint64 total = 0;
    QMap<quint64, quint64> kinds;
    std::map<std::pair<quint64, quint64>, ElfSymbol> symbols;
    quint64 symbol_text = 0;
    bool complete = image.dynamic_inventory_complete;
    for (const auto& section : image.sections) {
        check_cancelled(stop);
        if (section.type == 19 || section.type == 0x60000001 || section.type == 0x60000002) {
            complete = false;
            warning(output.result.warnings, QStringLiteral("Relocações compactadas RELR/Android não foram inventariadas; resolução de ponteiros estáticos deve permanecer desativada."));
        }
        if (section.type != 4 && section.type != 9) continue;
        // Only allocated relocation tables describe runtime VAs here. Static
        // --emit-relocs/debug-section records can use a different address model
        // and do not describe loader-written pointer slots.
        if ((section.flags & 2) == 0) continue;
        if ((section.flags & 0x800) != 0) {
            complete = false;
            warning(output.result.warnings, QStringLiteral("Relocações ELF comprimidas foram omitidas."));
            continue;
        }
        const unsigned width = image.wide ? 8 : 4;
        const auto minimum = quint64(width) * (section.type == 4 ? 3 : 2);
        if (section.stride < minimum || section.size % section.stride != 0 ||
            section.link >= static_cast<quint64>(image.sections.size()))
            invalid(QStringLiteral("stride, tamanho ou link de relocação ELF inválido."));
        total = add(total, section.size / section.stride);
        bounded(total, record_limit, QStringLiteral("Relocações ELF"));
        for (quint64 index = 0; index < section.size / section.stride; ++index) {
            check_cancelled(stop);
            const auto data = read(file, add(section.offset, multiply(index, section.stride)), minimum);
            const auto location = ram(number(data, 0, width, image.order));
            const auto info = number(data, width, width, image.order);
            const auto symbol_index = image.wide ? info >> 32 : info >> 8;
            const auto type = image.wide ? info & 0xffffffff : info & 0xff;
            ++kinds[type];
            if (type == 0) continue;
            output.relocation_slot(location);
            // ET_EXEC/ET_DYN r_offset is a VA. An unmapped slot (including BSS)
            // supplies no bytes to this offline, file-backed analysis.
            if (!file_offset(metadata, location, width)) continue;
            const bool import_slot = (image.machine == 62 || image.machine == 3) ? (type == 6 || type == 7) :
                (image.machine == 40 ? (type == 21 || type == 22) :
                 (image.machine == 183 ? (type == 1025 || type == 1026) : false));
            const bool absolute = image.machine == 62 ? type == 1 : (image.machine == 3 ? type == 1 :
                (image.machine == 40 ? type == 2 : (image.machine == 183 ? type == 257 : false)));
            const bool relative = (image.machine == 62 || image.machine == 3) ? type == 8 :
                (image.machine == 40 ? type == 23 : (image.machine == 183 ? type == 1027 : false));
            if (!import_slot && !absolute && !relative) continue;
            ElfSymbol symbol;
            if (symbol_index != 0) {
                const auto key = std::make_pair(section.link, symbol_index);
                auto found = symbols.find(key);
                if (found == symbols.end()) {
                    symbol = elf_symbol(file, image, section.link, symbol_index);
                    symbol_text = add(symbol_text, static_cast<quint64>(symbol.name.toUtf8().size()));
                    bounded(symbol_text, block_limit, QStringLiteral("Nomes de símbolos ELF"));
                    found = symbols.emplace(key, std::move(symbol)).first;
                }
                symbol = found->second;
            }
            if (import_slot && symbol.section == 0 && symbol_index != 0 && symbol.type != 6 && !symbol.name.isEmpty()) {
                output.imported(symbol.name, location);
                continue;
            }
            // Explicit RELA addends and relative REL slots have a meaningful
            // preferred/link-address result. Other REL implicit addends cannot
            // be distinguished from linker-processed content here.
            std::optional<quint64> target;
            if (relative && symbol_index == 0) {
                const auto encoded = section.type == 4 ? number(data, width * 2, width, image.order) :
                    number(mapped_read(file, metadata, location.value, width), 0, width, image.order);
                // No runtime load bias is introduced: linked VAs are used.
                if ((encoded & (quint64(1) << (width * 8 - 1))) == 0) target = encoded;
            } else if ((absolute || import_slot) && section.type == 4 && symbol_index != 0 &&
                       symbol.section != 0 && symbol.section < 0xff00 && symbol.type != 6 && symbol.type != 10) {
                target = plus_signed(symbol.value, number(data, width * 2, width, image.order), width);
            }
            if (target) output.reference(location, ram(*target), QStringLiteral("elf-link-relocation"));
        }
    }
    // Match dynamic table ranges against the section tables actually visited.
    // This catches stripped/omitted relocation sections without reading any
    // runtime pointer or assuming that a section name proves completeness.
    quint64 dynamic_records = 0;
    for (const auto& section : image.sections) {
        if (section.type != 6) continue;
        check_cancelled(stop);
        const unsigned width = image.wide ? 8 : 4;
        const auto minimum = quint64(width) * 2;
        if ((section.flags & 0x800) != 0 || section.stride < minimum || section.size % section.stride != 0) {
            complete = false;
            continue;
        }
        dynamic_records = add(dynamic_records, section.size / section.stride);
        bounded(dynamic_records, record_limit, QStringLiteral("Entradas dinâmicas ELF"));
        QMap<quint64, quint64> values;
        bool terminated = false;
        for (quint64 index = 0; index < section.size / section.stride; ++index) {
            check_cancelled(stop);
            const auto data = read(file, add(section.offset, multiply(index, section.stride)), minimum);
            const auto tag = number(data, 0, width, image.order);
            if (tag == 0) { terminated = true; break; }
            if (tag == 7 || tag == 8 || tag == 17 || tag == 18 || tag == 23 || tag == 2 ||
                tag == 35 || tag == 36 || tag == 0x6000000f || tag == 0x60000010 ||
                tag == 0x60000011 || tag == 0x60000012) {
                if (values.contains(tag)) complete = false;
                values.insert(tag, number(data, width, width, image.order));
            }
        }
        if (!terminated) complete = false;
        if (values.contains(35) || values.contains(36) || values.contains(0x6000000f) ||
            values.contains(0x60000011)) complete = false;
        for (const auto pair : {std::make_pair(quint64(7), quint64(8)), std::make_pair(quint64(17), quint64(18)),
                                std::make_pair(quint64(23), quint64(2))}) {
            if (!values.contains(pair.first) && !values.contains(pair.second)) continue;
            if (!values.contains(pair.first) || !values.contains(pair.second)) { complete = false; continue; }
            const auto address = values.value(pair.first), size = values.value(pair.second);
            if (size == 0) continue;
            const bool covered = std::any_of(image.sections.cbegin(), image.sections.cend(), [&](const auto& candidate) {
                return (candidate.type == 4 || candidate.type == 9) && (candidate.flags & 0x800) == 0 && (candidate.flags & 2) != 0 &&
                    address >= candidate.address && address - candidate.address <= candidate.size &&
                    size <= candidate.size - (address - candidate.address);
            });
            if (!covered) complete = false;
        }
    }
    QJsonObject counts;
    for (auto it = kinds.cbegin(); it != kinds.cend(); ++it) counts.insert(QString::number(it.key()), QString::number(it.value()));
    output.result.details.insert(QStringLiteral("elf_relocation_types"), counts);
    output.result.details.insert(QStringLiteral("elf_relocation_count"), QString::number(total));
    output.relocation_complete = complete;
    if (!complete) warning(output.result.warnings, QStringLiteral("O inventário de relocação ELF está incompleto; dados brutos de ponteiros não comprovam destinos de fluxo indireto."));
    if (total != 0) warning(output.result.warnings, QStringLiteral("Relocações ELF são fatos estáticos nos endereços de link. Slots GLOB_DAT/JUMP_SLOT continuam imports sem destino externo; bibliotecas/versões e load bias não são resolvidos. RELR, PLT stubs e relocações não reconhecidas não são simulados."));
}

void elf_debug_metadata(const std::shared_ptr<BinaryFile>& file, const ElfImage& image,
                         Collector& output, std::stop_token stop) {
    QJsonArray ids;
    QJsonArray sections;
    quint64 note_bytes = 0, notes = 0;
    for (const auto& section : image.sections) {
        check_cancelled(stop);
        if (section.label.startsWith(QStringLiteral(".debug_")) || section.label.startsWith(QStringLiteral(".zdebug_"))) {
            if (sections.size() < 4096) sections.push_back(QJsonObject{{QStringLiteral("name"), section.label},
                {QStringLiteral("offset"), hex(section.offset)}, {QStringLiteral("size"), QString::number(section.size)},
                {QStringLiteral("compressed"), (section.flags & 0x800) != 0 || section.label.startsWith(QStringLiteral(".zdebug_"))}});
        }
        if (section.label == QStringLiteral(".gnu_debuglink")) {
            if ((section.flags & 0x800) != 0) continue;
            const auto data = read(file, section.offset, section.size);
            const auto name = cstring(data, 0);
            const auto nul = data.indexOf('\0');
            const auto crc_offset = (static_cast<quint64>(nul) + 4) & ~quint64(3);
            output.result.details.insert(QStringLiteral("elf_gnu_debuglink"), QJsonObject{
                {QStringLiteral("recorded_filename"), name},
                {QStringLiteral("crc32"), hex(number(data, crc_offset, 4, image.order))}});
        }
        if (section.type != 7 || (section.flags & 0x800) != 0) continue;
        note_bytes = add(note_bytes, section.size);
        bounded(note_bytes, block_limit, QStringLiteral("Notas ELF"));
        const auto data = read(file, section.offset, section.size);
        quint64 at = 0;
        while (at < section.size) {
            check_cancelled(stop);
            bounded(++notes, 65536, QStringLiteral("Registros de notas ELF"));
            range(at, 12, section.size);
            const auto names = number(data, at, 4, image.order);
            const auto size = number(data, at + 4, 4, image.order);
            const auto type = number(data, at + 8, 4, image.order);
            const auto name_start = add(at, 12);
            const auto descriptor = add(name_start, add(names, 3) & ~quint64(3));
            range(name_start, names, section.size);
            range(descriptor, size, section.size);
            if (names == 4 && data.mid(static_cast<qsizetype>(name_start), 4) == QByteArray("GNU\0", 4) && type == 3) {
                if (size > 0 && size <= 64 && ids.size() < 256)
                    ids.push_back(QString::fromLatin1(data.mid(static_cast<qsizetype>(descriptor), static_cast<qsizetype>(size)).toHex()));
                else warning(output.result.warnings, QStringLiteral("Build-id GNU fora do limite de 1 a 64 bytes foi omitido."));
            }
            at = add(descriptor, add(size, 3) & ~quint64(3));
            if (at > section.size) invalid(QStringLiteral("padding de nota ELF truncado."));
        }
    }
    if (!ids.isEmpty()) output.result.details.insert(QStringLiteral("elf_gnu_build_ids"), ids);
    if (!sections.isEmpty()) output.result.details.insert(QStringLiteral("elf_debug_sections"), sections);
    if (output.result.details.contains(QStringLiteral("elf_gnu_debuglink")))
        warning(output.result.warnings, QStringLiteral(".gnu_debuglink é somente metadado offline; o arquivo externo não é aberto e seu CRC não foi verificado."));
}

} // namespace

BinaryEnrichment enrich_binary(const std::shared_ptr<BinaryFile>& file,
                               const BinaryMetadata& metadata, std::stop_token stop) {
    if (!file) throw std::invalid_argument("enrich_binary requires a BinaryFile");
    if (file->size() != metadata.size) throw std::invalid_argument("BinaryFile size differs from loaded metadata");
    Collector output(metadata);
    output.result.details.insert(QStringLiteral("engine"), QStringLiteral("reworkbench-own-enrichment-1"));
    const auto component = [&](const QString& name, const auto& operation) {
        check_cancelled(stop);
        try { operation(); }
        catch (const Cancelled&) { throw; }
        catch (const std::exception& error) {
            warning(output.result.warnings, name + QStringLiteral(": leitura parcial: ") + QString::fromUtf8(error.what()));
        }
    };
    if (metadata.format == BinaryFormat::Pe) {
        component(QStringLiteral("Cabeçalho PE adicional"), [&] {
            const auto image = pe_directories(file);
            component(QStringLiteral("Funções de exceção PE"), [&] { pe_functions(file, metadata, image, output, stop); });
            component(QStringLiteral("Callbacks TLS PE"), [&] { pe_tls(file, metadata, image, output, stop); });
            component(QStringLiteral("Identidade CodeView"), [&] { pe_debug(file, metadata, image, output, stop); });
            component(QStringLiteral("Relocações PE"), [&] { pe_relocations(file, metadata, image, output, stop); });
        });
    } else if (metadata.format == BinaryFormat::Elf) {
        component(QStringLiteral("Seções ELF adicionais"), [&] {
            const auto image = elf_sections(file, stop);
            component(QStringLiteral("Relocações ELF"), [&] { elf_relocations(file, metadata, image, output, stop); });
            component(QStringLiteral("Identidades de depuração ELF"), [&] { elf_debug_metadata(file, image, output, stop); });
        });
    }
    component(QStringLiteral("Linhas de código-fonte"), [&] {
        const auto debug = decode_debug_symbols(file, metadata, stop);
        for (const auto& message : debug.warnings) warning(output.result.warnings, message);
        if (!debug.details.isEmpty()) output.result.details.insert(QStringLiteral("debug"), debug.details);
        if (!debug.lines.isEmpty()) {
            QJsonArray files, rows;
            QHash<QString, int> indexes;
            for (const auto& line : debug.lines) {
                check_cancelled(stop);
                auto found = indexes.constFind(line.path);
                int index = 0;
                if (found == indexes.cend()) {
                    index = static_cast<int>(files.size());
                    indexes.insert(line.path, index);
                    files.push_back(line.path);
                } else index = found.value();
                rows.push_back(QJsonObject{{QStringLiteral("address"), line.address.text()},
                    {QStringLiteral("file"), index}, {QStringLiteral("line"), QString::number(line.line)},
                    {QStringLiteral("column"), QString::number(line.column)},
                    {QStringLiteral("discriminator"), QString::number(line.discriminator)},
                    {QStringLiteral("is_statement"), line.is_statement}});
            }
            output.result.details.insert(QStringLiteral("source_files"), files);
            output.result.details.insert(QStringLiteral("source_lines"), rows);
        }
    });
    output.result.details.insert(QStringLiteral("relocation_slots"), output.relocation_slots);
    output.result.details.insert(QStringLiteral("relocation_slots_complete"), output.relocation_complete);
    return std::move(output.result);
}

} // namespace reworkbench
