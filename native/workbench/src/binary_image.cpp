#include "reworkbench/binary_image.hpp"
#include <QFileInfo>

#include <filesystem>
#include <limits>
#include <utility>

namespace reworkbench {
namespace {
[[noreturn]] void file_error(const QString& message) {
    throw std::runtime_error(message.toUtf8().toStdString());
}
}

BinaryFile::BinaryFile(QString path) : path_(std::move(path)) {
    if (path_.isEmpty() || path_.contains(QChar::Null) || !QFileInfo(path_).isFile())
        file_error(QStringLiteral("A origem deve ser um arquivo regular."));
#ifdef _WIN32
    const std::filesystem::path native_path(path_.toStdWString());
#else
    const auto encoded = path_.toUtf8();
    const auto native_path = std::filesystem::u8path(encoded.constBegin(), encoded.constEnd());
#endif
    stream_.open(native_path, std::ios::binary | std::ios::ate);
    if (!stream_.is_open()) file_error(QStringLiteral("Não foi possível abrir o arquivo: ") + path_);
    const auto end = stream_.tellg();
    if (end == std::streampos(-1)) file_error(QStringLiteral("Não foi possível medir o arquivo."));
    const auto length = static_cast<std::streamoff>(end);
    if (length < 0) file_error(QStringLiteral("Tamanho de arquivo inválido."));
    size_ = static_cast<quint64>(length);
    stream_.seekg(0, std::ios::beg);
    if (!stream_) file_error(QStringLiteral("Não foi possível posicionar o arquivo."));
}

QByteArray BinaryFile::read(quint64 offset, quint64 length) const {
    constexpr quint64 maximum_read = 16ull * 1024 * 1024;
    if (length > maximum_read) file_error(QStringLiteral("Leitura excede o limite de 16 MiB."));
    if (offset > size_ || length > size_ - offset)
        file_error(QStringLiteral("Leitura ultrapassa o fim do arquivo."));
    if (offset > static_cast<quint64>(std::numeric_limits<std::streamoff>::max()))
        file_error(QStringLiteral("Offset excede a capacidade de leitura da plataforma."));
    if (length == 0) return {};
    std::lock_guard lock(mutex_);
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream_) file_error(QStringLiteral("Falha ao posicionar leitura do arquivo."));
    QByteArray result(static_cast<qsizetype>(length), '\0');
    stream_.read(result.data(), static_cast<std::streamsize>(length));
    if (stream_.gcount() != static_cast<std::streamsize>(length) || !stream_)
        file_error(QStringLiteral("Arquivo truncado ou alterado durante a leitura."));
    return result;
}

std::optional<quint64> file_offset(const BinaryMetadata& metadata, Address address, quint64 length) {
    for (const auto& region : metadata.regions) {
        if (region.start.space != address.space || !region.file_offset || address.value < region.start.value)
            continue;
        const quint64 delta = address.value - region.start.value;
        if (delta > region.virtual_size || length > region.virtual_size - delta ||
            delta > region.file_size || length > region.file_size - delta)
            continue;
        // Offsets already include a universal Mach-O slice's origin.
        const quint64 base = *region.file_offset;
        if (base > metadata.size || delta > metadata.size - base) continue;
        const quint64 offset = base + delta;
        if (length > metadata.size - offset) continue;
        return offset;
    }
    return std::nullopt;
}

bool executable_address(const BinaryMetadata& metadata, Address address) {
    for (const auto& region : metadata.regions) {
        if (region.executable && region.start.space == address.space && address.value >= region.start.value &&
            address.value - region.start.value < region.virtual_size)
            return true;
    }
    return false;
}

} // namespace reworkbench
