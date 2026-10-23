#pragma once

#include "reworkbench/enrichment.hpp"

#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <limits>

namespace reworkbench::enrichment_internal {
inline constexpr quint64 block_limit = 16ull * 1024 * 1024;
inline constexpr quint64 record_limit = 200000;
inline constexpr quint64 detail_limit = 20000;
inline constexpr quint64 name_limit = 4096;

[[noreturn]] inline void invalid(const QString& reason) {
    throw std::runtime_error((QStringLiteral("Metadados adicionais: ") + reason).toUtf8().toStdString());
}

inline quint64 add(quint64 left, quint64 right) {
    if (right > std::numeric_limits<quint64>::max() - left) invalid(QStringLiteral("soma excede 64 bits."));
    return left + right;
}

inline quint64 multiply(quint64 left, quint64 right) {
    if (left != 0 && right > std::numeric_limits<quint64>::max() / left)
        invalid(QStringLiteral("produto excede 64 bits."));
    return left * right;
}

inline void range(quint64 offset, quint64 size, quint64 bound) {
    if (offset > bound || size > bound - offset) invalid(QStringLiteral("estrutura truncada."));
}

inline void bounded(quint64 count, quint64 maximum, const QString& what) {
    if (count > maximum) invalid(what + QStringLiteral(" excede o limite de leitura."));
}

inline quint64 number(const QByteArray& data, quint64 at, unsigned width, ByteOrder order) {
    if (width == 0 || width > 8) invalid(QStringLiteral("largura numérica inválida."));
    range(at, width, static_cast<quint64>(data.size()));
    quint64 value = 0;
    for (unsigned i = 0; i < width; ++i) {
        const auto byte = static_cast<unsigned char>(data[static_cast<qsizetype>(at + i)]);
        if (order == ByteOrder::Little) value |= quint64(byte) << (8 * i);
        else value = (value << 8) | byte;
    }
    return value;
}

inline QByteArray read(const std::shared_ptr<BinaryFile>& file, quint64 at, quint64 size) {
    bounded(size, block_limit, QStringLiteral("Bloco"));
    range(at, size, file->size());
    return file->read(at, size);
}

inline QString cstring(const QByteArray& data, quint64 at, quint64 limit = name_limit) {
    range(at, 1, static_cast<quint64>(data.size()));
    const quint64 available = std::min(limit, static_cast<quint64>(data.size()) - at);
    const auto window = data.mid(static_cast<qsizetype>(at), static_cast<qsizetype>(available));
    const auto nul = window.indexOf('\0');
    if (nul < 0)
        invalid(QStringLiteral("texto sem terminador NUL dentro do limite."));
    return QString::fromUtf8(window.constData(), nul);
}

inline Address ram(quint64 value) { return {QStringLiteral("ram"), value}; }

inline bool aligned_code(const BinaryMetadata& metadata, quint64 value) {
    switch (metadata.architecture) {
    case Architecture::Thumb: return (value & 1) == 0;
    case Architecture::Arm:
    case Architecture::Arm64:
    case Architecture::Mips: return (value & 3) == 0;
    default: return true;
    }
}

inline bool backed_code(const BinaryMetadata& metadata, Address address) {
    return aligned_code(metadata, address.value) && executable_address(metadata, address) &&
           file_offset(metadata, address).has_value();
}

// Consume an ABI function pointer conservatively. An ARM/Thumb mode bit must
// agree with the globally selected image mode rather than change it silently.
inline std::optional<Address> code_pointer(const BinaryMetadata& metadata, quint64 value) {
    if (metadata.architecture == Architecture::Thumb) {
        if ((value & 1) == 0) return std::nullopt;
        value &= ~quint64(1);
    } else if (metadata.architecture == Architecture::Arm && (value & 1) != 0) return std::nullopt;
    const auto address = ram(value);
    return backed_code(metadata, address) ? std::optional<Address>(address) : std::nullopt;
}

inline QByteArray mapped_read(const std::shared_ptr<BinaryFile>& file, const BinaryMetadata& metadata,
                              quint64 value, quint64 size) {
    const auto offset = file_offset(metadata, ram(value), size);
    if (!offset) invalid(QStringLiteral("estrutura não possui bytes mapeados na imagem."));
    return read(file, *offset, size);
}

struct ElfSection {
    quint64 name = 0, type = 0, flags = 0, address = 0, offset = 0, size = 0;
    quint64 link = 0, info = 0, stride = 0;
    QString label;
};

struct ElfImage {
    bool wide = false;
    ByteOrder order = ByteOrder::Little;
    quint64 machine = 0;
    bool dynamic_inventory_complete = true;
    QVector<ElfSection> sections;
};

inline ElfImage elf_sections(const std::shared_ptr<BinaryFile>& file, std::stop_token stop) {
    const auto ident = read(file, 0, 16);
    if (ident.left(4) != QByteArray("\x7f" "ELF", 4) || (ident[4] != 1 && ident[4] != 2) ||
        (ident[5] != 1 && ident[5] != 2)) invalid(QStringLiteral("cabeçalho ELF inválido."));
    ElfImage image;
    image.wide = ident[4] == 2;
    image.order = ident[5] == 1 ? ByteOrder::Little : ByteOrder::Big;
    const auto header = read(file, 0, image.wide ? 64 : 52);
    const auto field = [&](quint64 at, unsigned size) { return number(header, at, size, image.order); };
    const auto kind = field(16, 2);
    if (kind != 2 && kind != 3) invalid(QStringLiteral("ELF requer endereços de executável ou biblioteca."));
    image.machine = field(18, 2);
    const auto offset = field(image.wide ? 40 : 32, image.wide ? 8 : 4);
    const auto stride = field(image.wide ? 58 : 46, 2);
    auto count = field(image.wide ? 60 : 48, 2);
    auto names = field(image.wide ? 62 : 50, 2);
    const quint64 minimum = image.wide ? 64 : 40;
    if (offset == 0) {
        if (count != 0 || names != 0) invalid(QStringLiteral("seções ELF ausentes com índices declarados."));
        return image;
    }
    if (stride < minimum) invalid(QStringLiteral("stride de seção ELF inválido."));
    const auto section_at = [&](quint64 index) {
        const auto data = read(file, add(offset, multiply(stride, index)), minimum);
        const auto get = [&](quint64 at, unsigned size) { return number(data, at, size, image.order); };
        return ElfSection{get(0, 4), get(4, 4), get(8, image.wide ? 8 : 4),
            get(image.wide ? 16 : 12, image.wide ? 8 : 4), get(image.wide ? 24 : 16, image.wide ? 8 : 4),
            get(image.wide ? 32 : 20, image.wide ? 8 : 4), get(image.wide ? 40 : 24, 4),
            get(image.wide ? 44 : 28, 4), get(image.wide ? 56 : 36, image.wide ? 8 : 4), {}};
    };
    if (count == 0 || names == 0xffff) {
        const auto zero = section_at(0);
        if (zero.type != 0) invalid(QStringLiteral("seção ELF zero inválida."));
        if (count == 0) count = zero.size;
        if (names == 0xffff) names = zero.link;
    }
    bounded(count, 65536, QStringLiteral("Seções ELF"));
    bounded(multiply(count, stride), block_limit, QStringLiteral("Tabela ELF"));
    range(offset, multiply(count, stride), file->size());
    if (count == 0 || names >= count) invalid(QStringLiteral("índice de nomes ELF inválido."));
    image.sections.reserve(static_cast<qsizetype>(count));
    for (quint64 index = 0; index < count; ++index) {
        check_cancelled(stop);
        auto section = section_at(index);
        if (section.type != 0 && section.type != 8) range(section.offset, section.size, file->size());
        image.sections.push_back(section);
    }
    if (names != 0) {
        const auto& strings = image.sections[static_cast<qsizetype>(names)];
        if (strings.type != 3 || (strings.flags & 0x800) != 0)
            invalid(QStringLiteral("nomes ELF comprimidos ou com tipo inválido."));
        const auto data = read(file, strings.offset, strings.size);
        quint64 text_budget = 0;
        for (auto& section : image.sections) {
            check_cancelled(stop);
            if (section.name >= strings.size) invalid(QStringLiteral("nome de seção ELF fora da tabela."));
            section.label = cstring(data, section.name);
            text_budget = add(text_budget, static_cast<quint64>(section.label.toUtf8().size()));
            bounded(text_budget, block_limit, QStringLiteral("Nomes ELF"));
        }
    }
    const auto program_offset = field(image.wide ? 32 : 28, image.wide ? 8 : 4);
    const auto program_stride = field(image.wide ? 54 : 42, 2);
    auto program_count = field(image.wide ? 56 : 44, 2);
    if (program_count == 0xffff) program_count = image.sections.front().info;
    bounded(program_count, 65536, QStringLiteral("Segmentos ELF adicionais"));
    if (program_count != 0) {
        if (program_offset == 0 || program_stride < (image.wide ? 56u : 32u))
            invalid(QStringLiteral("tabela de segmentos ELF inválida."));
        bounded(multiply(program_count, program_stride), block_limit, QStringLiteral("Tabela de segmentos ELF"));
        range(program_offset, multiply(program_count, program_stride), file->size());
        for (quint64 index = 0; index < program_count; ++index) {
            check_cancelled(stop);
            const auto data = read(file, add(program_offset, multiply(index, program_stride)), image.wide ? 56 : 32);
            if (number(data, 0, 4, image.order) != 2) continue; // PT_DYNAMIC
            const auto file_start = number(data, image.wide ? 8 : 4, image.wide ? 8 : 4, image.order);
            const auto address = number(data, image.wide ? 16 : 8, image.wide ? 8 : 4, image.order);
            const auto size = number(data, image.wide ? 32 : 16, image.wide ? 8 : 4, image.order);
            range(file_start, size, file->size());
            const bool covered = std::any_of(image.sections.cbegin(), image.sections.cend(), [&](const auto& section) {
                return section.type == 6 && (section.flags & 0x800) == 0 && file_start == section.offset &&
                    address == section.address && size == section.size;
            });
            if (!covered) image.dynamic_inventory_complete = false;
        }
    }
    return image;
}

inline void warning(QStringList& list, const QString& text) {
    if (!list.contains(text) && list.size() < 256) list.push_back(text);
}

} // namespace reworkbench::enrichment_internal
