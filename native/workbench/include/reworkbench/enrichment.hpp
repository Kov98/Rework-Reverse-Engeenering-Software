#pragma once

#include "reworkbench/binary_image.hpp"
#include <QJsonObject>

namespace reworkbench {

// Supplementary, independently parsed facts. No relocation is applied to the
// sample and an import slot is never promoted to an executable function.
struct BinaryEnrichment {
    QVector<Symbol> symbols;
    QVector<Address> entries;
    QVector<Import> imports;
    QVector<Reference> references;
    QStringList warnings;
    QJsonObject details;
};

[[nodiscard]] BinaryEnrichment enrich_binary(const std::shared_ptr<BinaryFile>& file,
                                            const BinaryMetadata& metadata,
                                            std::stop_token stop = {});

struct DebugSourceLine {
    Address address;
    QString path;
    quint64 line = 0;
    quint64 column = 0;
    quint64 discriminator = 0;
    bool is_statement = false;
};

struct DebugSymbols {
    QVector<DebugSourceLine> lines;
    QStringList warnings;
    QJsonObject details;
};

// ELF .debug_line versions 2, 3 and 4, with exact row addresses. This is not a
// PDB reader, a complete DWARF reader, or an address-range interpolation API.
[[nodiscard]] DebugSymbols decode_debug_symbols(const std::shared_ptr<BinaryFile>& file,
                                                const BinaryMetadata& metadata,
                                                std::stop_token stop = {});

enum class SymbolMapFormat { Auto, Json, Text };
struct SymbolMapImport {
    QVector<Symbol> symbols;
    QVector<Address> entries;
    QStringList warnings;
    QJsonObject details;
};

// Explicit offline import; never searches a symbol server or follows a path
// mentioned in binary debug records. Throws for malformed/unmapped entries.
[[nodiscard]] SymbolMapImport import_symbol_map(const QString& path,
                                               const BinaryMetadata& metadata,
                                               SymbolMapFormat format = SymbolMapFormat::Auto,
                                               std::stop_token stop = {});

} // namespace reworkbench
