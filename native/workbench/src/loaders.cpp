#include "reworkbench/binary_image.hpp"

#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <limits>
#include <utility>

namespace reworkbench {
namespace {
constexpr quint64 maximum_block = 16ull * 1024 * 1024;
constexpr quint64 maximum_name = 4096;
constexpr quint64 maximum_headers = 65536;
constexpr quint64 maximum_regions = 4096;
constexpr quint64 maximum_symbols = 200000;
constexpr quint64 maximum_imports = 65536;
constexpr quint64 maximum_slices = 256;

[[noreturn]] void invalid(const QString& message) {
    throw std::runtime_error((QStringLiteral("Não foi possível carregar a imagem: ") + message).toUtf8().toStdString());
}

quint64 sum(quint64 a, quint64 b) {
    if (b > std::numeric_limits<quint64>::max() - a) invalid(QStringLiteral("soma de endereços excede 64 bits."));
    return a + b;
}

quint64 product(quint64 a, quint64 b) {
    if (a != 0 && b > std::numeric_limits<quint64>::max() / a)
        invalid(QStringLiteral("tamanho de tabela excede 64 bits."));
    return a * b;
}

void range(quint64 offset, quint64 length, quint64 size) {
    if (offset > size || length > size - offset) invalid(QStringLiteral("estrutura ultrapassa os limites do arquivo ou da fatia."));
}

void limit(quint64 count, quint64 maximum, const QString& what) {
    if (count > maximum) invalid(what + QStringLiteral(" excede o limite de segurança do carregador."));
}

quint64 number(const QByteArray& bytes, quint64 offset, unsigned width, ByteOrder order) {
    if (width == 0 || width > 8) invalid(QStringLiteral("largura de campo inválida."));
    range(offset, width, static_cast<quint64>(bytes.size()));
    quint64 value = 0;
    if (order == ByteOrder::Little) {
        for (unsigned i = 0; i < width; ++i)
            value |= quint64(static_cast<unsigned char>(bytes[static_cast<qsizetype>(offset + i)])) << (i * 8);
    } else {
        for (unsigned i = 0; i < width; ++i)
            value = (value << 8) | static_cast<unsigned char>(bytes[static_cast<qsizetype>(offset + i)]);
    }
    return value;
}

QString fixed_name(const QByteArray& bytes, quint64 offset, quint64 length) {
    range(offset, length, static_cast<quint64>(bytes.size()));
    const auto value = bytes.mid(static_cast<qsizetype>(offset), static_cast<qsizetype>(length));
    const auto nul = value.indexOf('\0');
    return QString::fromLatin1(nul < 0 ? value : value.left(nul));
}

struct View {
    std::shared_ptr<BinaryFile> file;
    quint64 origin = 0;
    quint64 size = 0;
    ByteOrder order = ByteOrder::Little;

    QByteArray read(quint64 offset, quint64 length) const {
        range(offset, length, size);
        limit(length, maximum_block, QStringLiteral("Bloco de leitura"));
        return file->read(sum(origin, offset), length);
    }

    quint64 integer(quint64 offset, unsigned width) const {
        return number(read(offset, width), 0, width, order);
    }
};

// Strings are read in small chunks. Neither a table's declared size nor a
// missing terminator can turn into an unbounded allocation or scan.
QString string_at(const View& view, quint64 offset, quint64 available, std::stop_token stop) {
    range(offset, available, view.size);
    const quint64 bound = std::min(available, maximum_name);
    QByteArray result;
    for (quint64 consumed = 0; consumed < bound;) {
        check_cancelled(stop);
        const quint64 length = std::min<quint64>(256, bound - consumed);
        auto chunk = view.read(sum(offset, consumed), length);
        const auto nul = chunk.indexOf('\0');
        if (nul >= 0) {
            result.append(chunk.constData(), nul);
            return QString::fromUtf8(result);
        }
        result.append(chunk);
        consumed += length;
    }
    invalid(QStringLiteral("nome sem terminador NUL em até 4096 bytes."));
}

struct MetadataBudget {
    quint64 text_bytes = 0;
    void name(const QString& value) {
        text_bytes = sum(text_bytes, static_cast<quint64>(value.toUtf8().size()));
        limit(text_bytes, maximum_block, QStringLiteral("Texto dos metadados"));
    }
};

Architecture pe_architecture(quint64 machine) {
    switch (machine) {
    case 0x14c: return Architecture::X86;
    case 0x8664: return Architecture::X64;
    case 0x1c0: return Architecture::Arm;
    case 0x1c2:
    case 0x1c4: return Architecture::Thumb;
    case 0xaa64: return Architecture::Arm64;
    case 0x162:
    case 0x166:
    case 0x168:
    case 0x266:
    case 0x366:
    case 0x466: return Architecture::Mips;
    default: return Architecture::Unknown;
    }
}

Architecture elf_architecture(quint64 machine) {
    switch (machine) {
    case 3: return Architecture::X86;
    case 62: return Architecture::X64;
    case 40: return Architecture::Arm;
    case 183: return Architecture::Arm64;
    case 8: return Architecture::Mips;
    default: return Architecture::Unknown;
    }
}

Architecture mach_architecture(quint64 cpu) {
    switch (cpu) {
    case 7: return Architecture::X86;
    case 0x01000007: return Architecture::X64;
    case 12: return Architecture::Arm;
    case 0x0100000c: return Architecture::Arm64;
    default: return Architecture::Unknown;
    }
}

struct MachKind {
    bool valid = false;
    bool fat = false;
    bool wide = false;
    ByteOrder order = ByteOrder::Little;
};

MachKind mach_kind(const QByteArray& prefix) {
    if (prefix.size() < 4) return {};
    switch (number(prefix, 0, 4, ByteOrder::Big)) {
    case 0xfeedface: return {true, false, false, ByteOrder::Big};
    case 0xcefaedfe: return {true, false, false, ByteOrder::Little};
    case 0xfeedfacf: return {true, false, true, ByteOrder::Big};
    case 0xcffaedfe: return {true, false, true, ByteOrder::Little};
    case 0xcafebabe: return {true, true, false, ByteOrder::Big};
    case 0xbebafeca: return {true, true, false, ByteOrder::Little};
    case 0xcafebabf: return {true, true, true, ByteOrder::Big};
    case 0xbfbafeca: return {true, true, true, ByteOrder::Little};
    default: return {};
    }
}

void add_region(BinaryMetadata& metadata, Region region) {
    const auto end = sum(region.start.value, region.virtual_size);
    if (metadata.bits == 32 && end > 0x100000000ull)
        invalid(QStringLiteral("região ultrapassa o espaço de endereços de 32 bits."));
    if (region.file_offset) range(*region.file_offset, region.file_size, metadata.size);
    else if (region.file_size != 0) invalid(QStringLiteral("região sem offset possui tamanho em arquivo."));
    if (region.virtual_size == 0) return;
    limit(static_cast<quint64>(metadata.regions.size()) + 1, maximum_regions, QStringLiteral("Regiões mapeadas"));
    // Keep mappings sorted so overlap validation checks only the neighbors.
    // Empty regions never enter this index.
    const auto at = std::lower_bound(metadata.regions.begin(), metadata.regions.end(), region,
        [](const Region& a, const Region& b) {
            return a.start.space < b.start.space || (a.start.space == b.start.space && a.start.value < b.start.value);
        });
    if (at != metadata.regions.end() && at->start.space == region.start.space && at->start.value < end)
        invalid(QStringLiteral("regiões virtuais sobrepostas não são suportadas nesta versão."));
    if (at != metadata.regions.begin()) {
        const auto& before = *(at - 1);
        if (before.start.space == region.start.space && region.start.value < sum(before.start.value, before.virtual_size))
            invalid(QStringLiteral("regiões virtuais sobrepostas não são suportadas nesta versão."));
    }
    metadata.regions.insert(at, std::move(region));
}

void add_entry(BinaryMetadata& metadata, Address address) {
    if (std::find(metadata.entries.cbegin(), metadata.entries.cend(), address) == metadata.entries.cend())
        metadata.entries.push_back(std::move(address));
}

QString mapped_string(const View& view, const BinaryMetadata& metadata, quint64 address, std::stop_token stop,
                      quint64 upper_bound = std::numeric_limits<quint64>::max()) {
    for (const auto& region : metadata.regions) {
        if (region.start.space != QStringLiteral("ram") || !region.file_offset || address < region.start.value) continue;
        const quint64 delta = address - region.start.value;
        if (delta >= region.file_size || delta >= region.virtual_size) continue;
        const auto absolute = sum(*region.file_offset, delta);
        if (absolute < view.origin) invalid(QStringLiteral("nome aponta para fora da fatia selecionada."));
        const auto available = std::min({region.file_size - delta, region.virtual_size - delta, upper_bound});
        return string_at(view, absolute - view.origin, available, stop);
    }
    invalid(QStringLiteral("nome não possui bytes mapeados no arquivo."));
}

QByteArray mapped_read(const View& view, const BinaryMetadata& metadata, quint64 address, quint64 length) {
    const auto absolute = file_offset(metadata, {QStringLiteral("ram"), address}, length);
    if (!absolute || *absolute < view.origin) invalid(QStringLiteral("tabela não possui intervalo contíguo no arquivo."));
    return view.read(*absolute - view.origin, length);
}

struct Directory { quint64 rva = 0; quint64 size = 0; };

void pe_exports(const View& view, BinaryMetadata& metadata, Directory directory, MetadataBudget& budget,
                std::stop_token stop) {
    if (directory.rva == 0 && directory.size == 0) return;
    if (directory.rva == 0 || directory.size < 40) invalid(QStringLiteral("diretório de exports PE inválido."));
    const auto start = sum(metadata.image_base, directory.rva);
    const auto finish = sum(start, directory.size);
    const auto header = mapped_read(view, metadata, start, 40);
    const auto ordinal_base = number(header, 16, 4, view.order);
    const auto function_count = number(header, 20, 4, view.order);
    const auto name_count = number(header, 24, 4, view.order);
    limit(function_count, maximum_symbols, QStringLiteral("Exports PE"));
    limit(name_count, maximum_symbols, QStringLiteral("Nomes de exports PE"));
    if (function_count == 0) {
        if (name_count != 0) invalid(QStringLiteral("exports PE nomeados sem tabela de funções."));
        return;
    }
    const auto functions_rva = number(header, 28, 4, view.order);
    if (functions_rva == 0) invalid(QStringLiteral("diretório PE sem tabela de endereços de exports."));
    const auto functions = mapped_read(view, metadata, sum(metadata.image_base, functions_rva), product(function_count, 4));
    QByteArray names;
    QByteArray ordinals;
    if (name_count != 0) {
        if (number(header, 32, 4, view.order) == 0 || number(header, 36, 4, view.order) == 0)
            invalid(QStringLiteral("diretório PE sem tabelas de nomes/ordinais."));
        names = mapped_read(view, metadata, sum(metadata.image_base, number(header, 32, 4, view.order)), product(name_count, 4));
        ordinals = mapped_read(view, metadata, sum(metadata.image_base, number(header, 36, 4, view.order)), product(name_count, 2));
    }
    const auto append_export = [&](quint64 index, QString label) {
        check_cancelled(stop);
        const auto rva = number(functions, index * 4, 4, view.order);
        if (rva == 0) return;
        const auto ordinal = sum(ordinal_base, index);
        if (ordinal > std::numeric_limits<quint32>::max()) invalid(QStringLiteral("ordinal de export PE excede 32 bits."));
        if (label.isEmpty()) label = QStringLiteral("#") + QString::number(ordinal);
        budget.name(label);
        quint64 address = sum(metadata.image_base, rva);
        if (address >= start && address < finish) {
            const auto target = mapped_string(view, metadata, address, stop, finish - address);
            budget.name(target);
            // Forwarders name another module's export; they are not functions
            // at the address of the forwarder string.
            metadata.imports.push_back({label + QStringLiteral(" -> ") + target, QStringLiteral("PE forwarder"), std::nullopt});
            limit(static_cast<quint64>(metadata.imports.size()), maximum_imports, QStringLiteral("Imports PE"));
            return;
        }
        if (metadata.architecture == Architecture::Thumb) address &= ~quint64(1);
        limit(static_cast<quint64>(metadata.symbols.size()) + 1, maximum_symbols, QStringLiteral("Exports PE"));
        const Address symbol_address{QStringLiteral("ram"), address};
        const auto kind = executable_address(metadata, symbol_address) ? QStringLiteral("function") : QStringLiteral("export");
        metadata.symbols.push_back({symbol_address, std::move(label), kind});
    };
    QSet<quint64> named;
    for (quint64 i = 0; i < name_count; ++i) {
        check_cancelled(stop);
        const auto index = number(ordinals, i * 2, 2, view.order);
        if (index >= function_count) invalid(QStringLiteral("ordinal de export PE fora da tabela."));
        auto name = mapped_string(view, metadata, sum(metadata.image_base, number(names, i * 4, 4, view.order)), stop);
        if (name.isEmpty()) invalid(QStringLiteral("export PE nomeado possui nome vazio."));
        append_export(index, std::move(name));
        named.insert(index);
    }
    for (quint64 i = 0; i < function_count; ++i) {
        check_cancelled(stop);
        if (!named.contains(i)) append_export(i, {});
    }
}

void pe_imports(const View& view, BinaryMetadata& metadata, Directory directory, MetadataBudget& budget,
                std::stop_token stop) {
    if (directory.rva == 0 && directory.size == 0) return;
    if (directory.rva == 0 || directory.size < 20) invalid(QStringLiteral("diretório de imports PE inválido."));
    const auto descriptors = directory.size / 20;
    limit(descriptors, maximum_imports, QStringLiteral("Descritores de imports PE"));
    const unsigned pointer_size = metadata.bits == 64 ? 8 : 4;
    const quint64 ordinal_flag = quint64(1) << (metadata.bits - 1);
    bool terminated = false;
    for (quint64 i = 0; i < descriptors; ++i) {
        check_cancelled(stop);
        const auto address = sum(sum(metadata.image_base, directory.rva), product(i, 20));
        const auto descriptor = mapped_read(view, metadata, address, 20);
        bool empty = true;
        for (int field = 0; field < 5; ++field) empty &= number(descriptor, field * 4, 4, view.order) == 0;
        if (empty) { terminated = true; break; }
        const auto lookup = number(descriptor, 0, 4, view.order);
        const auto library_rva = number(descriptor, 12, 4, view.order);
        const auto iat_rva = number(descriptor, 16, 4, view.order);
        if (library_rva == 0 || iat_rva == 0) invalid(QStringLiteral("descritor de import PE sem nome ou IAT."));
        const auto library = mapped_string(view, metadata, sum(metadata.image_base, library_rva), stop);
        budget.name(library);
        const auto lookup_base = sum(metadata.image_base, lookup == 0 ? iat_rva : lookup);
        bool thunks_terminated = false;
        for (quint64 j = 0; j <= maximum_imports; ++j) {
            check_cancelled(stop);
            const auto value = number(mapped_read(view, metadata, sum(lookup_base, product(j, pointer_size)), pointer_size), 0, pointer_size, view.order);
            if (value == 0) { thunks_terminated = true; break; }
            limit(static_cast<quint64>(metadata.imports.size()) + 1, maximum_imports, QStringLiteral("Imports PE"));
            QString name;
            if ((value & ordinal_flag) != 0) {
                if ((value & ~(ordinal_flag | 0xffffull)) != 0) invalid(QStringLiteral("import PE por ordinal possui bits reservados."));
                name = QStringLiteral("#") + QString::number(value & 0xffff);
            } else {
                if (value > std::numeric_limits<quint32>::max()) invalid(QStringLiteral("RVA de import PE inválido; IAT vinculada sem tabela original não é suportada."));
                const auto hint_name = sum(metadata.image_base, value);
                mapped_read(view, metadata, hint_name, 2);
                name = mapped_string(view, metadata, sum(hint_name, 2), stop);
            }
            budget.name(name);
            const auto slot = sum(sum(metadata.image_base, iat_rva), product(j, pointer_size));
            if (!file_offset(metadata, {QStringLiteral("ram"), slot}, pointer_size))
                invalid(QStringLiteral("slot IAT PE ultrapassa os bytes mapeados."));
            metadata.imports.push_back({std::move(name), library, Address{QStringLiteral("ram"), slot}});
        }
        if (!thunks_terminated) invalid(QStringLiteral("tabela de imports PE sem terminador dentro do limite."));
    }
    if (!terminated) invalid(QStringLiteral("diretório de imports PE sem descritor terminador."));
}

void load_pe(View view, BinaryMetadata& metadata, std::stop_token stop) {
    view.order = ByteOrder::Little;
    const auto dos = view.read(0, 64);
    if (dos[0] != 'M' || dos[1] != 'Z') invalid(QStringLiteral("assinatura DOS ausente."));
    const auto nt_offset = number(dos, 60, 4, view.order);
    if (nt_offset < 64) invalid(QStringLiteral("offset do cabeçalho PE inválido."));
    const auto nt = view.read(nt_offset, 24);
    if (number(nt, 0, 4, view.order) != 0x4550) invalid(QStringLiteral("assinatura PE ausente."));
    metadata.architecture = pe_architecture(number(nt, 4, 2, view.order));
    const auto section_count = number(nt, 6, 2, view.order);
    if (section_count == 0) invalid(QStringLiteral("PE sem seções."));
    limit(section_count, 4096, QStringLiteral("Seções PE"));
    const auto optional_size = number(nt, 20, 2, view.order);
    const auto optional_offset = sum(nt_offset, 24);
    const auto optional = view.read(optional_offset, optional_size);
    const auto magic = number(optional, 0, 2, view.order);
    if (magic != 0x10b && magic != 0x20b) invalid(QStringLiteral("cabeçalho opcional PE não é PE32/PE32+."));
    metadata.bits = magic == 0x20b ? 64 : 32;
    if ((metadata.architecture == Architecture::X64 || metadata.architecture == Architecture::Arm64) && metadata.bits != 64)
        invalid(QStringLiteral("máquina PE de 64 bits com cabeçalho PE32."));
    if ((metadata.architecture == Architecture::X86 || metadata.architecture == Architecture::Arm || metadata.architecture == Architecture::Thumb) && metadata.bits != 32)
        invalid(QStringLiteral("máquina PE de 32 bits com cabeçalho PE32+."));
    metadata.byte_order = view.order;
    const quint64 minimum_optional = metadata.bits == 64 ? 112 : 96;
    if (optional_size < minimum_optional) invalid(QStringLiteral("cabeçalho opcional PE truncado."));
    const auto directories = number(optional, minimum_optional - 4, 4, view.order);
    if (directories > (optional_size - minimum_optional) / 8) invalid(QStringLiteral("diretórios PE ultrapassam o cabeçalho opcional."));
    metadata.image_base = number(optional, metadata.bits == 64 ? 24 : 28, metadata.bits == 64 ? 8 : 4, view.order);
    const auto image_size = number(optional, 56, 4, view.order);
    const auto headers_size = number(optional, 60, 4, view.order);
    const auto image_end = sum(metadata.image_base, image_size);
    if (metadata.bits == 32 && image_end > 0x100000000ull) invalid(QStringLiteral("imagem PE ultrapassa 32 bits."));
    const auto section_table = sum(optional_offset, optional_size);
    const auto sections = view.read(section_table, product(section_count, 40));
    if (headers_size == 0 || headers_size > image_size || headers_size < sum(section_table, product(section_count, 40)))
        invalid(QStringLiteral("tamanho dos cabeçalhos PE inconsistente."));
    range(0, headers_size, view.size);
    add_region(metadata, {QStringLiteral("headers"), {QStringLiteral("ram"), metadata.image_base}, headers_size,
                          view.origin, headers_size, true, false, false});
    for (quint64 i = 0; i < section_count; ++i) {
        check_cancelled(stop);
        const auto offset = i * 40;
        const auto raw_size = number(sections, offset + 16, 4, view.order);
        const auto raw_offset = number(sections, offset + 20, 4, view.order);
        auto virtual_size = number(sections, offset + 8, 4, view.order);
        if (virtual_size == 0) virtual_size = raw_size;
        const auto rva = number(sections, offset + 12, 4, view.order);
        if (rva > image_size || virtual_size > image_size - rva) invalid(QStringLiteral("seção PE ultrapassa SizeOfImage."));
        if (raw_size != 0) range(raw_offset, raw_size, view.size);
        const auto flags = number(sections, offset + 36, 4, view.order);
        Region region{fixed_name(sections, offset, 8), {QStringLiteral("ram"), sum(metadata.image_base, rva)}, virtual_size,
                      std::nullopt, raw_size, (flags & 0x40000000) != 0, (flags & 0x80000000) != 0, (flags & 0x20000000) != 0};
        if (raw_size != 0) region.file_offset = sum(view.origin, raw_offset);
        add_region(metadata, std::move(region));
    }
    auto entry = number(optional, 16, 4, view.order);
    if (entry != 0) {
        entry = sum(metadata.image_base, entry);
        if (metadata.architecture == Architecture::Thumb) entry &= ~quint64(1);
        add_entry(metadata, {QStringLiteral("ram"), entry});
    }
    MetadataBudget budget;
    const auto directory = [&](quint64 index) -> Directory {
        if (index >= directories) return {};
        return {number(optional, minimum_optional + index * 8, 4, view.order),
                number(optional, minimum_optional + index * 8 + 4, 4, view.order)};
    };
    pe_exports(view, metadata, directory(0), budget, stop);
    pe_imports(view, metadata, directory(1), budget, stop);
    if (section_count > 96) metadata.warnings.push_back(QStringLiteral("PE possui mais seções que o limite documentado do carregador Windows."));
    if (directory(13).rva != 0) metadata.warnings.push_back(QStringLiteral("Imports PE de carregamento adiado ainda não são interpretados."));
    metadata.warnings.push_back(QStringLiteral("PE usa endereços preferidos; relocação e resolução dinâmica de imports não são aplicadas."));
}

struct ElfSection {
    quint64 name = 0;
    quint64 type = 0;
    quint64 flags = 0;
    quint64 address = 0;
    quint64 offset = 0;
    quint64 size = 0;
    quint64 link = 0;
    quint64 info = 0;
    quint64 entry_size = 0;
};

ElfSection elf_section(const View& view, quint64 table, quint64 stride, quint64 index, bool wide) {
    const auto data = view.read(sum(table, product(stride, index)), wide ? 64 : 40);
    const auto field = [&](quint64 at, unsigned width) { return number(data, at, width, view.order); };
    return {field(0, 4), field(4, 4), field(8, wide ? 8 : 4), field(wide ? 16 : 12, wide ? 8 : 4),
            field(wide ? 24 : 16, wide ? 8 : 4), field(wide ? 32 : 20, wide ? 8 : 4),
            field(wide ? 40 : 24, 4), field(wide ? 44 : 28, 4), field(wide ? 56 : 36, wide ? 8 : 4)};
}

void load_elf(View view, BinaryMetadata& metadata, std::stop_token stop) {
    const auto ident = view.read(0, 16);
    if (ident.left(4) != QByteArray("\x7f" "ELF", 4)) invalid(QStringLiteral("assinatura ELF ausente."));
    const auto elf_class = static_cast<unsigned char>(ident[4]);
    const auto encoding = static_cast<unsigned char>(ident[5]);
    if ((elf_class != 1 && elf_class != 2) || (encoding != 1 && encoding != 2) || ident[6] != 1)
        invalid(QStringLiteral("classe, endian ou versão ELF inválida."));
    const bool wide = elf_class == 2;
    view.order = encoding == 1 ? ByteOrder::Little : ByteOrder::Big;
    metadata.bits = wide ? 64 : 32;
    metadata.byte_order = view.order;
    const auto header = view.read(0, wide ? 64 : 52);
    const auto field = [&](quint64 at, unsigned width) { return number(header, at, width, view.order); };
    const auto declared_header_size = field(wide ? 52 : 40, 2);
    if (field(20, 4) != 1 || declared_header_size < (wide ? 64 : 52)) invalid(QStringLiteral("cabeçalho ELF inválido."));
    range(0, declared_header_size, view.size);
    const auto file_type = field(16, 2);
    if (file_type != 2 && file_type != 3) invalid(QStringLiteral("esta versão carrega ELF executável/compartilhado; objetos relocáveis e core dumps requerem outro modelo de endereços."));
    metadata.architecture = elf_architecture(field(18, 2));
    auto entry = field(24, wide ? 8 : 4);
    const auto program_offset = field(wide ? 32 : 28, wide ? 8 : 4);
    const auto section_offset = field(wide ? 40 : 32, wide ? 8 : 4);
    const auto program_stride = field(wide ? 54 : 42, 2);
    auto program_count = field(wide ? 56 : 44, 2);
    const auto section_stride = field(wide ? 58 : 46, 2);
    auto section_count = field(wide ? 60 : 48, 2);
    auto name_index = field(wide ? 62 : 50, 2);
    if ((section_offset != 0 && section_count == 0) || program_count == 0xffff || name_index == 0xffff) {
        if (section_offset == 0 || section_stride < (wide ? 64 : 40)) invalid(QStringLiteral("numeração ELF estendida sem seção zero válida."));
        const auto zero = elf_section(view, section_offset, section_stride, 0, wide);
        if (zero.type != 0) invalid(QStringLiteral("seção zero ELF não é SHT_NULL."));
        if (section_count == 0) section_count = zero.size;
        if (program_count == 0xffff) program_count = zero.info;
        if (name_index == 0xffff) name_index = zero.link;
    }
    limit(program_count, maximum_headers, QStringLiteral("Segmentos ELF"));
    limit(section_count, maximum_headers, QStringLiteral("Seções ELF"));
    if (program_count != 0) {
        if (program_offset == 0 || program_stride < (wide ? 56 : 32)) invalid(QStringLiteral("tabela de segmentos ELF inválida."));
        const auto table_size = product(program_count, program_stride);
        limit(table_size, maximum_block, QStringLiteral("Tabela de segmentos ELF"));
        range(program_offset, table_size, view.size);
    }
    if (section_count != 0) {
        if (section_offset == 0 || section_stride < (wide ? 64 : 40)) invalid(QStringLiteral("tabela de seções ELF inválida."));
        const auto table_size = product(section_count, section_stride);
        limit(table_size, maximum_block, QStringLiteral("Tabela de seções ELF"));
        range(section_offset, table_size, view.size);
        if (name_index >= section_count) invalid(QStringLiteral("índice de nomes de seções ELF inválido."));
    } else if (name_index != 0) invalid(QStringLiteral("ELF declara nomes sem tabela de seções."));
    bool have_base = false;
    for (quint64 i = 0; i < program_count; ++i) {
        check_cancelled(stop);
        const auto data = view.read(sum(program_offset, product(i, program_stride)), wide ? 56 : 32);
        const auto get = [&](quint64 at, unsigned width) { return number(data, at, width, view.order); };
        const auto type = get(0, 4);
        const auto offset = get(wide ? 8 : 4, wide ? 8 : 4);
        const auto address = get(wide ? 16 : 8, wide ? 8 : 4);
        const auto file_size = get(wide ? 32 : 16, wide ? 8 : 4);
        const auto memory_size = get(wide ? 40 : 20, wide ? 8 : 4);
        const auto flags = get(wide ? 4 : 24, 4);
        const auto alignment = get(wide ? 48 : 28, wide ? 8 : 4);
        if (file_size != 0) range(offset, file_size, view.size);
        if (type != 1) continue;
        if (file_size > memory_size) invalid(QStringLiteral("segmento ELF possui p_filesz maior que p_memsz."));
        if (alignment > 1 && ((alignment & (alignment - 1)) != 0 || offset % alignment != address % alignment))
            invalid(QStringLiteral("alinhamento de segmento ELF inválido."));
        Region region{QStringLiteral("LOAD%1").arg(i), {QStringLiteral("ram"), address}, memory_size, std::nullopt,
                      file_size, (flags & 4) != 0, (flags & 2) != 0, (flags & 1) != 0};
        if (file_size != 0) region.file_offset = sum(view.origin, offset);
        add_region(metadata, std::move(region));
        if (!have_base || address < metadata.image_base) { metadata.image_base = address; have_base = true; }
    }
    if (!have_base) invalid(QStringLiteral("ELF sem segmento PT_LOAD."));
    if (entry != 0) {
        if (metadata.architecture == Architecture::Arm && (entry & 1) != 0) {
            metadata.architecture = Architecture::Thumb;
            entry &= ~quint64(1);
            metadata.warnings.push_back(QStringLiteral("ELF ARM inicia em Thumb; alternância ARM/Thumb por região ainda não é representada."));
        }
        add_entry(metadata, {QStringLiteral("ram"), entry});
    }
    QVector<ElfSection> sections;
    sections.reserve(static_cast<qsizetype>(section_count));
    for (quint64 i = 0; i < section_count; ++i) {
        check_cancelled(stop);
        auto section = elf_section(view, section_offset, section_stride, i, wide);
        if (section.type != 0 && section.type != 8) range(section.offset, section.size, view.size);
        if ((section.flags & 2) != 0) sum(section.address, section.size);
        sections.push_back(section);
    }
    if (name_index != 0 && sections[static_cast<qsizetype>(name_index)].type != 3)
        invalid(QStringLiteral("tabela de nomes de seções ELF não é SHT_STRTAB."));
    MetadataBudget budget;
    quint64 symbols_seen = 0;
    bool extended_symbol_warning = false;
    bool incompatible_symbol_mode = false;
    for (const auto& section : sections) {
        check_cancelled(stop);
        if (section.type != 2 && section.type != 11) continue;
        const quint64 minimum_symbol = wide ? 24 : 16;
        if (section.entry_size < minimum_symbol || section.size % section.entry_size != 0 || section.link >= section_count)
            invalid(QStringLiteral("tabela de símbolos ELF inválida."));
        const auto count = section.size / section.entry_size;
        limit(section.size, maximum_block, QStringLiteral("Tabela de símbolos ELF"));
        symbols_seen = sum(symbols_seen, count);
        limit(symbols_seen, maximum_symbols, QStringLiteral("Símbolos ELF"));
        const auto& strings = sections[static_cast<qsizetype>(section.link)];
        if (strings.type != 3) invalid(QStringLiteral("símbolos ELF não apontam para SHT_STRTAB."));
        if ((section.flags & 0x800) != 0 || (strings.flags & 0x800) != 0)
            invalid(QStringLiteral("tabela ELF de símbolos/nomes comprimida ainda não é suportada."));
        for (quint64 i = 0; i < count; ++i) {
            check_cancelled(stop);
            const auto data = view.read(sum(section.offset, product(i, section.entry_size)), minimum_symbol);
            const auto name_offset = number(data, 0, 4, view.order);
            if (name_offset == 0) continue;
            if (name_offset >= strings.size) invalid(QStringLiteral("nome de símbolo ELF fora da tabela."));
            const auto info = number(data, wide ? 4 : 12, 1, view.order);
            const auto index = number(data, wide ? 6 : 14, 2, view.order);
            auto value = number(data, wide ? 8 : 4, wide ? 8 : 4, view.order);
            const auto name = string_at(view, sum(strings.offset, name_offset), strings.size - name_offset, stop);
            budget.name(name);
            if (name.isEmpty()) continue;
            if (index == 0) {
                limit(static_cast<quint64>(metadata.imports.size()) + 1, maximum_imports, QStringLiteral("Símbolos ELF indefinidos"));
                metadata.imports.push_back({name, {}, std::nullopt});
                continue;
            }
            if (index == 0xffff) { extended_symbol_warning = true; continue; }
            if (index >= 0xff00 && index != 0xfff1) continue;
            if (index < 0xff00 && index >= section_count) invalid(QStringLiteral("seção de símbolo ELF fora da tabela."));
            const auto type = info & 15;
            if (type == 3 || type == 4 || type == 6) continue; // section, file, TLS (not a process VA)
            auto kind = type == 2 ? QStringLiteral("function") : (index == 0xfff1 ? QStringLiteral("absolute") : QStringLiteral("symbol"));
            if ((metadata.architecture == Architecture::Arm || metadata.architecture == Architecture::Thumb) && type == 2) {
                const bool thumb_symbol = (value & 1) != 0;
                value &= ~quint64(1);
                if (thumb_symbol != (metadata.architecture == Architecture::Thumb)) {
                    kind = thumb_symbol ? QStringLiteral("function-thumb-unselected") : QStringLiteral("function-arm-unselected");
                    incompatible_symbol_mode = true;
                }
            }
            metadata.symbols.push_back({{QStringLiteral("ram"), value}, name, kind});
        }
    }
    if (extended_symbol_warning) metadata.warnings.push_back(QStringLiteral("Símbolos ELF com SHN_XINDEX são omitidos; SHT_SYMTAB_SHNDX ainda não é interpretado."));
    if (incompatible_symbol_mode) metadata.warnings.push_back(QStringLiteral("Símbolos de função ARM/Thumb em modo diferente do modo da imagem são preservados, mas não decodificados. Sem entrada Thumb declarada, o modo ELF ARM permanece ARM; modo por função ainda não é suportado."));
    if (section_count == 0) metadata.warnings.push_back(QStringLiteral("ELF sem tabela de seções: símbolos do segmento dinâmico ainda não são recuperados."));
    metadata.warnings.push_back(QStringLiteral("ELF usa endereços de link; relocação, GOT/PLT e atribuição de biblioteca aos símbolos indefinidos ainda não são aplicadas."));
}

struct MachSymbols { quint64 offset = 0; quint64 count = 0; quint64 strings = 0; quint64 string_size = 0; };

void load_mach(View view, BinaryMetadata& metadata, std::stop_token stop) {
    const auto kind = mach_kind(view.read(0, 4));
    if (!kind.valid || kind.fat) invalid(QStringLiteral("fatia não contém Mach-O fino."));
    view.order = kind.order;
    metadata.byte_order = kind.order;
    metadata.bits = kind.wide ? 64 : 32;
    const quint64 header_size = kind.wide ? 32 : 28;
    const auto header = view.read(0, header_size);
    metadata.architecture = mach_architecture(number(header, 4, 4, view.order));
    const auto file_type = number(header, 12, 4, view.order);
    if (file_type == 1 || file_type == 4) invalid(QStringLiteral("Mach-O relocável/core requer modelo de endereços ainda não disponível."));
    if (file_type < 2 || file_type > 0xc) invalid(QStringLiteral("tipo de arquivo Mach-O desconhecido."));
    const bool two_level = (number(header, 24, 4, view.order) & 0x80) != 0;
    const auto command_count = number(header, 16, 4, view.order);
    const auto command_bytes = number(header, 20, 4, view.order);
    limit(command_count, maximum_headers, QStringLiteral("Comandos Mach-O"));
    limit(command_bytes, maximum_block, QStringLiteral("Tabela de comandos Mach-O"));
    if (product(command_count, 8) > command_bytes) invalid(QStringLiteral("contagem de comandos Mach-O inconsistente."));
    const auto commands = view.read(header_size, command_bytes);
    quint64 position = 0;
    quint64 total_sections = 0;
    std::optional<quint64> main_offset;
    std::optional<MachSymbols> symbols;
    QStringList libraries;
    MetadataBudget budget;
    bool have_base = false;
    bool thread_entry = false;
    bool dynamic_metadata = false;
    for (quint64 i = 0; i < command_count; ++i) {
        check_cancelled(stop);
        const auto command = number(commands, position, 4, view.order);
        const auto length = number(commands, sum(position, 4), 4, view.order);
        if (length < 8 || length % (kind.wide ? 8 : 4) != 0) invalid(QStringLiteral("tamanho de comando Mach-O inválido."));
        range(position, length, command_bytes);
        const auto data = commands.mid(static_cast<qsizetype>(position), static_cast<qsizetype>(length));
        const auto get = [&](quint64 at, unsigned width) { return number(data, at, width, view.order); };
        if (command == 1 || command == 0x19) {
            const bool segment_wide = command == 0x19;
            if (segment_wide != kind.wide) invalid(QStringLiteral("classe de segmento Mach-O diverge do cabeçalho."));
            const quint64 minimum_segment = segment_wide ? 72 : 56;
            if (length < minimum_segment) invalid(QStringLiteral("segmento Mach-O truncado."));
            const auto name = fixed_name(data, 8, 16);
            const auto address = get(24, segment_wide ? 8 : 4);
            const auto memory_size = get(segment_wide ? 32 : 28, segment_wide ? 8 : 4);
            const auto offset = get(segment_wide ? 40 : 32, segment_wide ? 8 : 4);
            const auto file_size = get(segment_wide ? 48 : 36, segment_wide ? 8 : 4);
            const auto permissions = get(segment_wide ? 60 : 44, 4);
            const auto section_count = get(segment_wide ? 64 : 48, 4);
            const auto flags = get(segment_wide ? 68 : 52, 4);
            if ((flags & 1) != 0) invalid(QStringLiteral("segmento Mach-O SG_HIGHVM ainda não é suportado."));
            if (file_size > memory_size) invalid(QStringLiteral("segmento Mach-O possui filesize maior que vmsize."));
            if (file_size != 0) range(offset, file_size, view.size);
            total_sections = sum(total_sections, section_count);
            limit(total_sections, maximum_headers, QStringLiteral("Seções Mach-O"));
            const quint64 section_stride = segment_wide ? 80 : 68;
            range(minimum_segment, product(section_count, section_stride), length);
            for (quint64 j = 0; j < section_count; ++j) {
                check_cancelled(stop);
                const auto at = sum(minimum_segment, product(j, section_stride));
                const auto section_address = get(at + 32, segment_wide ? 8 : 4);
                const auto section_size = get(at + (segment_wide ? 40 : 36), segment_wide ? 8 : 4);
                const auto section_offset = get(at + (segment_wide ? 48 : 40), 4);
                const auto section_type = get(at + (segment_wide ? 64 : 56), 4) & 0xff;
                if (section_address < address || section_address - address > memory_size ||
                    section_size > memory_size - (section_address - address))
                    invalid(QStringLiteral("seção Mach-O ultrapassa o segmento virtual."));
                if (section_type != 1 && section_type != 0xc && section_type != 0x12) {
                    range(section_offset, section_size, view.size);
                    if (section_offset < offset || section_offset - offset > file_size ||
                        section_size > file_size - (section_offset - offset))
                        invalid(QStringLiteral("seção Mach-O ultrapassa os bytes do segmento."));
                    if (section_size != 0 && section_address - address != section_offset - offset)
                        invalid(QStringLiteral("mapeamento de seção Mach-O diverge do segmento."));
                }
            }
            Region region{name, {QStringLiteral("ram"), address}, memory_size, std::nullopt, file_size,
                          (permissions & 1) != 0, (permissions & 2) != 0, (permissions & 4) != 0};
            if (file_size != 0) region.file_offset = sum(view.origin, offset);
            add_region(metadata, std::move(region));
            if (file_size != 0 && (!have_base || address < metadata.image_base)) { metadata.image_base = address; have_base = true; }
        } else if (command == 0x80000028) {
            if (length < 24 || main_offset) invalid(QStringLiteral("LC_MAIN Mach-O truncado ou duplicado."));
            main_offset = get(8, 8);
        } else if (command == 2) {
            if (length < 24 || symbols) invalid(QStringLiteral("LC_SYMTAB Mach-O truncado ou duplicado."));
            symbols = MachSymbols{get(8, 4), get(12, 4), get(16, 4), get(20, 4)};
        } else if (command == 0xc || command == 0x80000018 || command == 0x8000001f || command == 0x20 || command == 0x80000023) {
            if (length < 24) invalid(QStringLiteral("comando dylib Mach-O truncado."));
            const auto name_offset = get(8, 4);
            if (name_offset < 24 || name_offset >= length) invalid(QStringLiteral("nome de dylib fora do comando Mach-O."));
            const View command_view{view.file, sum(view.origin, sum(header_size, position)), length, view.order};
            const auto library = string_at(command_view, name_offset, length - name_offset, stop);
            budget.name(library);
            libraries.push_back(library);
        } else if (command == 4 || command == 5) {
            if (length < 16) invalid(QStringLiteral("comando de estado de thread Mach-O vazio ou truncado."));
            quint64 state_position = 8;
            while (state_position < length) {
                check_cancelled(stop);
                range(state_position, 8, length); // flavor and count of 32-bit words
                const auto state_size = product(get(state_position + 4, 4), 4);
                range(sum(state_position, 8), state_size, length);
                state_position = sum(sum(state_position, 8), state_size);
            }
            thread_entry = true;
        } else if (command == 0x21 || command == 0x2c) {
            if (length < (command == 0x2c ? 24 : 20)) invalid(QStringLiteral("comando de criptografia Mach-O truncado."));
            const auto encrypted_offset = get(8, 4);
            const auto encrypted_size = get(12, 4);
            range(encrypted_offset, encrypted_size, view.size);
            if (get(16, 4) != 0) metadata.warnings.push_back(QStringLiteral("Mach-O contém bytes criptografados; a ferramenta não os descriptografa."));
        } else if (command == 0x22 || command == 0x80000022 || command == 0x80000033 || command == 0x80000034) {
            if (command == 0x22 || command == 0x80000022) {
                if (length < 48) invalid(QStringLiteral("comando dyld_info Mach-O truncado."));
                for (quint64 at = 8; at < 48; at += 8) range(get(at, 4), get(at + 4, 4), view.size);
            } else {
                if (length < 16) invalid(QStringLiteral("comando linkedit Mach-O truncado."));
                range(get(8, 4), get(12, 4), view.size);
            }
            dynamic_metadata = true;
        }
        position = sum(position, length);
    }
    if (position != command_bytes) invalid(QStringLiteral("tamanho acumulado dos comandos Mach-O inconsistente."));
    if (!have_base) invalid(QStringLiteral("Mach-O sem segmento com bytes em arquivo."));
    if (main_offset) {
        bool found = false;
        const auto absolute = sum(view.origin, *main_offset);
        for (const auto& region : metadata.regions) {
            if (!region.file_offset || absolute < *region.file_offset) continue;
            const auto delta = absolute - *region.file_offset;
            if (delta < region.file_size && delta < region.virtual_size) {
                auto address = sum(region.start.value, delta);
                if (metadata.architecture == Architecture::Arm && (address & 1) != 0) {
                    metadata.architecture = Architecture::Thumb;
                    address &= ~quint64(1);
                }
                add_entry(metadata, {QStringLiteral("ram"), address});
                found = true;
                break;
            }
        }
        if (!found) invalid(QStringLiteral("entrada LC_MAIN não possui bytes mapeados na fatia."));
    } else if (thread_entry) {
        metadata.warnings.push_back(QStringLiteral("Entrada Mach-O em LC_THREAD/LC_UNIXTHREAD ainda não é extraída; use símbolos ou uma entrada manual."));
    }
    if (symbols) {
        bool incompatible_symbol_mode = false;
        const quint64 stride = kind.wide ? 16 : 12;
        limit(symbols->count, maximum_symbols, QStringLiteral("Símbolos Mach-O"));
        range(symbols->offset, product(symbols->count, stride), view.size);
        range(symbols->strings, symbols->string_size, view.size);
        for (quint64 i = 0; i < symbols->count; ++i) {
            check_cancelled(stop);
            const auto data = view.read(sum(symbols->offset, product(i, stride)), stride);
            const auto name_index = number(data, 0, 4, view.order);
            const auto type = number(data, 4, 1, view.order);
            const auto section = number(data, 5, 1, view.order);
            const auto description = number(data, 6, 2, view.order);
            auto value = number(data, 8, kind.wide ? 8 : 4, view.order);
            if ((type & 0xe0) != 0 || name_index == 0) continue; // STAB debugging entries
            if (name_index >= symbols->string_size) invalid(QStringLiteral("nome de símbolo Mach-O fora da tabela."));
            const auto name = string_at(view, sum(symbols->strings, name_index), symbols->string_size - name_index, stop);
            budget.name(name);
            if (name.isEmpty()) continue;
            const auto basic_type = type & 0x0e;
            if (basic_type == 0 && value == 0) {
                const auto ordinal = (description >> 8) & 0xff;
                QString library;
                if (two_level && ordinal > 0 && ordinal < 0xfe) {
                    if (ordinal > static_cast<quint64>(libraries.size())) invalid(QStringLiteral("ordinal de biblioteca Mach-O fora da lista."));
                    library = libraries[static_cast<qsizetype>(ordinal - 1)];
                }
                limit(static_cast<quint64>(metadata.imports.size()) + 1, maximum_imports, QStringLiteral("Imports Mach-O"));
                metadata.imports.push_back({name, library, std::nullopt});
            } else if (basic_type == 0xe || basic_type == 2) {
                if (basic_type == 0xe && (section == 0 || section > total_sections)) invalid(QStringLiteral("seção de símbolo Mach-O inválida."));
                const Address address{QStringLiteral("ram"), value};
                auto symbol_kind = basic_type == 2 ? QStringLiteral("absolute") :
                    (executable_address(metadata, address) ? QStringLiteral("function") : QStringLiteral("symbol"));
                if (symbol_kind == QStringLiteral("function") && (metadata.architecture == Architecture::Arm || metadata.architecture == Architecture::Thumb)) {
                    // Mach-O uses n_desc N_ARM_THUMB_DEF, not the low address bit.
                    const bool thumb_symbol = (description & 0x0008) != 0;
                    if (thumb_symbol != (metadata.architecture == Architecture::Thumb)) {
                        symbol_kind = thumb_symbol ? QStringLiteral("function-thumb-unselected") : QStringLiteral("function-arm-unselected");
                        incompatible_symbol_mode = true;
                    }
                }
                metadata.symbols.push_back({address, name, symbol_kind});
            }
        }
        if (incompatible_symbol_mode) metadata.warnings.push_back(QStringLiteral("Símbolos Mach-O ARM/Thumb de modo diferente, identificado por N_ARM_THUMB_DEF, não são decodificados no modo global da imagem."));
    }
    if (dynamic_metadata) metadata.warnings.push_back(QStringLiteral("Mach-O: bind opcodes, export trie e chained fixups ainda não são interpretados; imports/símbolos vêm de LC_SYMTAB."));
    metadata.warnings.push_back(QStringLiteral("Mach-O usa endereços preferidos; slide e relocação dinâmica não são aplicados."));
}

void load_raw(const View& view, BinaryMetadata& metadata, const OpenOptions& options) {
    if (options.raw_architecture == Architecture::Unknown) invalid(QStringLiteral("imagem bruta exige arquitetura explícita."));
    metadata.format = BinaryFormat::Raw;
    metadata.architecture = options.raw_architecture;
    metadata.byte_order = options.raw_byte_order;
    metadata.bits = options.raw_architecture == Architecture::X64 || options.raw_architecture == Architecture::Arm64 ? 64 : 32;
    metadata.image_base = options.raw_base;
    sum(options.raw_base, view.size);
    add_region(metadata, {QStringLiteral("raw"), {QStringLiteral("ram"), options.raw_base}, view.size,
                          view.size == 0 ? std::nullopt : std::optional<quint64>(view.origin), view.size, true, true, true});
    if (options.entries.isEmpty() && view.size != 0) add_entry(metadata, {QStringLiteral("ram"), options.raw_base});
    metadata.warnings.push_back(QStringLiteral("Imagem bruta: arquitetura, endian, base e entradas são escolhas do usuário; permissões são hipóteses de análise."));
}

} // namespace

Probe probe_binary(const std::shared_ptr<BinaryFile>& file) {
    if (!file) invalid(QStringLiteral("arquivo ausente."));
    const View view{file, 0, file->size(), ByteOrder::Little};
    const auto prefix = view.read(0, std::min<quint64>(64, view.size));
    if (prefix.size() >= 2 && prefix[0] == 'M' && prefix[1] == 'Z') return {BinaryFormat::Pe, {}, false};
    if (prefix.size() >= 4 && prefix.left(4) == QByteArray("\x7f" "ELF", 4)) return {BinaryFormat::Elf, {}, false};
    const auto kind = mach_kind(prefix);
    if (!kind.valid) return {BinaryFormat::Unknown, {}, true};
    if (!kind.fat) return {BinaryFormat::MachO, {}, false};
    View fat = view;
    fat.order = kind.order;
    const auto count = fat.integer(4, 4);
    if (count == 0) invalid(QStringLiteral("Mach-O universal sem fatias."));
    limit(count, maximum_slices, QStringLiteral("Fatias Mach-O"));
    const quint64 stride = kind.wide ? 32 : 20;
    const auto table_end = sum(8, product(count, stride));
    const auto table = fat.read(8, product(count, stride));
    Probe result{BinaryFormat::FatMachO, {}, false};
    for (quint64 i = 0; i < count; ++i) {
        const auto at = i * stride;
        const auto cpu = number(table, at, 4, fat.order);
        const auto subtype = number(table, at + 4, 4, fat.order);
        const auto offset = number(table, at + 8, kind.wide ? 8 : 4, fat.order);
        const auto size = number(table, at + (kind.wide ? 16 : 12), kind.wide ? 8 : 4, fat.order);
        const auto alignment = number(table, at + (kind.wide ? 24 : 16), 4, fat.order);
        if (alignment > 63 || offset % (quint64(1) << alignment) != 0 || offset < table_end || size < 4)
            invalid(QStringLiteral("intervalo ou alinhamento de fatia Mach-O inválido."));
        if (kind.wide && number(table, at + 28, 4, fat.order) != 0) invalid(QStringLiteral("campo reservado FAT64 não é zero."));
        range(offset, size, fat.size);
        for (const auto& previous : result.slices) {
            if (offset < sum(previous.file_offset, previous.size) && previous.file_offset < sum(offset, size))
                invalid(QStringLiteral("fatias Mach-O sobrepostas."));
        }
        const auto inner = fat.read(offset, std::min<quint64>(size, 12));
        const auto inner_kind = mach_kind(inner);
        if (!inner_kind.valid || inner_kind.fat || inner.size() < 12 || number(inner, 4, 4, inner_kind.order) != cpu ||
            (number(inner, 8, 4, inner_kind.order) & 0x00ffffff) != (subtype & 0x00ffffff))
            invalid(QStringLiteral("cabeçalho de fatia diverge da tabela universal Mach-O."));
        const auto architecture = mach_architecture(cpu);
        const auto label = QStringLiteral("%1 — %2 bits — %3 — %4 bytes")
            .arg(architecture_name(architecture)).arg(inner_kind.wide ? 64 : 32)
            .arg(inner_kind.order == ByteOrder::Little ? QStringLiteral("little endian") : QStringLiteral("big endian"))
            .arg(size);
        result.slices.push_back({static_cast<quint32>(i), label, offset, size, architecture});
    }
    return result;
}

BinaryMetadata load_binary(const std::shared_ptr<BinaryFile>& file, const OpenOptions& options, std::stop_token stop) {
    check_cancelled(stop);
    if (!file) invalid(QStringLiteral("arquivo ausente."));
    BinaryMetadata metadata;
    metadata.source_path = file->path();
    metadata.original_name = QFileInfo(file->path()).fileName();
    metadata.size = file->size();
    View view{file, 0, file->size(), ByteOrder::Little};
    if (options.raw) {
        if (options.slice_index) invalid(QStringLiteral("seleção de fatia não se aplica à abertura bruta."));
        load_raw(view, metadata, options);
    } else {
        const auto probe = probe_binary(file);
        check_cancelled(stop);
        metadata.format = probe.format;
        if (probe.raw_required) invalid(QStringLiteral("formato desconhecido; abra como imagem bruta e configure a arquitetura."));
        if (probe.format != BinaryFormat::FatMachO && options.slice_index)
            invalid(QStringLiteral("o arquivo não contém uma tabela de fatias Mach-O."));
        if (probe.format == BinaryFormat::FatMachO) {
            if (!options.slice_index) invalid(QStringLiteral("Mach-O universal exige seleção explícita de uma fatia."));
            const auto selected = std::find_if(probe.slices.cbegin(), probe.slices.cend(), [&](const Slice& slice) { return slice.index == *options.slice_index; });
            if (selected == probe.slices.cend()) invalid(QStringLiteral("índice de fatia Mach-O fora da tabela."));
            view.origin = selected->file_offset;
            view.size = selected->size;
            metadata.warnings.push_back(QStringLiteral("Mach-O universal: fatia %1; offsets permanecem relativos ao arquivo original.").arg(selected->index));
            load_mach(view, metadata, stop);
        } else if (probe.format == BinaryFormat::Pe) load_pe(view, metadata, stop);
        else if (probe.format == BinaryFormat::Elf) load_elf(view, metadata, stop);
        else if (probe.format == BinaryFormat::MachO) load_mach(view, metadata, stop);
        else invalid(QStringLiteral("formato não suportado."));
    }
    limit(static_cast<quint64>(options.entries.size()), maximum_regions, QStringLiteral("Entradas manuais"));
    for (auto entry : options.entries) {
        check_cancelled(stop);
        if (metadata.architecture == Architecture::Thumb) entry.value &= ~quint64(1);
        if (!file_offset(metadata, entry) || !executable_address(metadata, entry))
            invalid(QStringLiteral("entrada manual fora de uma região executável com bytes no arquivo: ") + entry.text());
        add_entry(metadata, std::move(entry));
    }
    for (const auto& entry : metadata.entries) {
        check_cancelled(stop);
        if (!file_offset(metadata, entry) || !executable_address(metadata, entry))
            metadata.warnings.push_back(QStringLiteral("Entrada declarada sem bytes executáveis mapeados: ") + entry.text());
    }
    if (metadata.architecture == Architecture::Unknown)
        metadata.warnings.push_back(QStringLiteral("CPU não representada pelo conjunto de arquiteturas disponível; metadados e bytes permanecem acessíveis."));
    check_cancelled(stop);
    return metadata;
}

} // namespace reworkbench
