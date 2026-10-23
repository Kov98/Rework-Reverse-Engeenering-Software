#include "reworkbench/investigation.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <filesystem>
#include <limits>

namespace reworkbench {
namespace {
constexpr quint64 chunk_size = 1024 * 1024;
constexpr quint64 comparison_limit = 1024ULL * 1024 * 1024;
constexpr quint64 patch_source_limit = 8ULL * 1024 * 1024 * 1024;
[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toStdString()); }
quint64 number(const QJsonValue& value, const QString& field) {
    if (!value.isString()) fail(field + QStringLiteral(" must be a decimal or hexadecimal string."));
    const QString text = value.toString();
    const bool hex_value = text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive);
    const QString digits = hex_value ? text.mid(2) : text;
    if (digits.isEmpty() || digits.size() > 20) fail(field + QStringLiteral(" is invalid."));
    for (const QChar c : digits) {
        const auto u = c.unicode();
        if (!(u >= '0' && u <= '9') && !(hex_value && ((u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F'))))
            fail(field + QStringLiteral(" is invalid."));
    }
    bool ok = false;
    const auto parsed = digits.toULongLong(&ok, hex_value ? 16 : 10);
    if (!ok) fail(field + QStringLiteral(" exceeds 64 bits."));
    return parsed;
}
QByteArray bytes(const QJsonValue& value, const QString& field, qsizetype maximum) {
    if (!value.isString()) fail(field + QStringLiteral(" must be hexadecimal bytes."));
    const QString text = value.toString();
    if (text.isEmpty() || (text.size() % 2) || text.size() / 2 > maximum)
        fail(field + QStringLiteral(" has an invalid byte count."));
    for (const QChar c : text) {
        const auto u = c.unicode();
        if (!((u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F')))
            fail(field + QStringLiteral(" contains a non-hexadecimal character."));
    }
    return QByteArray::fromHex(text.toLatin1());
}
QString regular_file(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.canonicalFilePath().isEmpty()) fail(QStringLiteral("Expected an existing regular file: ") + path);
    return info.canonicalFilePath();
}
QByteArray read_exact(QFile& file, qint64 length) {
    const auto data = file.read(length);
    if (data.size() != length) fail(QStringLiteral("Short read or concurrent file change: ") + file.fileName());
    return data;
}
QString hash_file(QFile& file, std::stop_token stop) {
    if (!file.seek(0)) fail(QStringLiteral("Cannot seek source file."));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto size = file.size();
    if (size < 0) fail(QStringLiteral("Cannot obtain source size."));
    for (qint64 offset = 0; offset < size;) {
        check_cancelled(stop);
        const auto count = std::min<qint64>(static_cast<qint64>(chunk_size), size - offset);
        hash.addData(read_exact(file, count));
        offset += count;
    }
    if (file.size() != size) fail(QStringLiteral("Source size changed while reading."));
    return QString::fromLatin1(hash.result().toHex());
}
std::filesystem::path native_path(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    const auto utf8 = path.toUtf8();
    return std::filesystem::u8path(utf8.constData(), utf8.constData() + utf8.size());
#endif
}
bool equal_path(const QString& left, const QString& right) {
#ifdef _WIN32
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}
bool below(const QString& path, const QString& root) {
    const auto prefix = QDir::cleanPath(root) + QLatin1Char('/');
#ifdef _WIN32
    return path.startsWith(prefix, Qt::CaseInsensitive);
#else
    return path.startsWith(prefix);
#endif
}
QString destination_path(const QString& destination) {
    const QFileInfo info(destination);
    if (destination.contains(QChar::Null) || info.fileName().isEmpty() || info.fileName().contains(QLatin1Char(':')) || info.isSymLink() || (info.exists() && !info.isFile()))
        fail(QStringLiteral("Destination must be a regular file, not a symbolic link."));
    const QFileInfo parent(info.absolutePath());
    if (!parent.exists() || !parent.isDir() || parent.canonicalFilePath().isEmpty()) fail(QStringLiteral("Destination parent must already exist."));
    return QDir(parent.canonicalFilePath()).filePath(info.fileName());
}
void protect_destination(const QString& destination, const QString& source, const QString& root,
                         const QStringList& protected_paths, std::stop_token stop) {
    const auto path = destination_path(destination);
    QStringList forbidden = protected_paths;
    forbidden.push_back(source);
    for (const auto& protected_path : forbidden) {
        check_cancelled(stop);
        const QFileInfo info(protected_path);
        if (equal_path(path, QDir::cleanPath(info.absoluteFilePath()))) fail(QStringLiteral("Export would replace a registered source path."));
        if (!info.exists()) continue;
        const auto canonical = info.canonicalFilePath();
        if (equal_path(path, canonical) || (info.isDir() && below(path, canonical)))
            fail(QStringLiteral("Export would overwrite a protected path."));
        if (QFileInfo::exists(path) && info.isFile()) {
            std::error_code error;
            const bool equivalent = std::filesystem::equivalent(native_path(path), native_path(canonical), error);
            if (error) fail(QStringLiteral("Cannot check destination identity."));
            if (equivalent) fail(QStringLiteral("Destination is a hard link to a protected file."));
        }
    }
    if (!root.isEmpty()) {
        const QFileInfo info(root);
        const auto canonical = info.canonicalFilePath();
        if (canonical.isEmpty() || !info.isDir()) fail(QStringLiteral("Protected project root is invalid."));
        const auto exports = QDir(canonical).filePath(QStringLiteral("exports"));
        // Existing export junctions/symlinks must not lead into samples or other internals.
        const auto export_info = QFileInfo(exports);
        const auto export_root = export_info.exists() ? export_info.canonicalFilePath() : exports;
        if (equal_path(path, canonical) || (below(path, canonical) && !(below(path, exports) && below(path, export_root))))
            fail(QStringLiteral("Only the exports directory may receive files inside a project."));
        if (QFileInfo::exists(path)) {
            QDirIterator files(canonical, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            quint32 count = 0;
            while (files.hasNext()) {
                check_cancelled(stop);
                const auto internal = files.next();
                if (++count > 100000) fail(QStringLiteral("Project file inventory exceeds the export-protection limit."));
                if (below(internal, exports)) continue;
                std::error_code error;
                const bool equivalent = std::filesystem::equivalent(native_path(path), native_path(internal), error);
                if (error || equivalent) fail(QStringLiteral("Destination aliases a protected project file, or identity cannot be verified."));
            }
        }
    }
}
QString shape(const Instruction& instruction) {
    QString out = instruction.mnemonic.toLower();
    if (instruction.flow == FlowKind::Call || instruction.flow == FlowKind::Jump || instruction.flow == FlowKind::ConditionalJump)
        out += instruction.direct_target ? QStringLiteral(" <direct-target>") : QStringLiteral(" <indirect-target>");
    else {
        for (const auto& operand : instruction.operands) {
            out += QLatin1Char(' ');
            switch (operand.kind) {
            case OperandKind::Register: out += operand.reg.toLower(); break;
            case OperandKind::Immediate: out += hex(operand.immediate); break;
            case OperandKind::Memory:
                out += QStringLiteral("mem%1[").arg(operand.width_bits) + operand.memory_base.toLower()
                    + QLatin1Char('+') + operand.memory_index.toLower() + QLatin1Char('*')
                    + QString::number(operand.memory_scale) + QLatin1Char('+')
                    + ((operand.memory_base == QStringLiteral("rip") || operand.memory_base == QStringLiteral("eip"))
                       ? QStringLiteral("<pc-relative>") : QString::number(operand.displacement)) + QLatin1Char(']');
                break;
            case OperandKind::Other: out += instruction.operands_text; break;
            }
        }
    }
    return out;
}
} // namespace

QJsonObject compare_binaries(const QString& left, const QString& right, std::stop_token stop) {
    const auto left_path = regular_file(left), right_path = regular_file(right);
    QFile a(left_path), b(right_path);
    if (!a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly)) fail(QStringLiteral("Cannot open comparison inputs."));
    const auto left_size = static_cast<quint64>(a.size()), right_size = static_cast<quint64>(b.size());
    if (left_size > comparison_limit || right_size > comparison_limit) fail(QStringLiteral("Binary comparison is limited to 1 GiB per input."));
    const auto total = std::max(left_size, right_size);
    QCryptographicHash left_hash(QCryptographicHash::Sha256), right_hash(QCryptographicHash::Sha256);
    QJsonArray ranges;
    quint64 changed = 0, range_start = 0, range_length = 0, range_count = 0;
    auto finish_range = [&] {
        if (!range_length) return;
        ++range_count;
        if (ranges.size() < 4096) ranges.push_back(QJsonObject{{QStringLiteral("offset"), hex(range_start)}, {QStringLiteral("length"), hex(range_length)}});
        range_length = 0;
    };
    for (quint64 offset = 0; offset < total;) {
        check_cancelled(stop);
        const auto count = std::min(chunk_size, total - offset);
        const auto an = offset < left_size ? std::min(count, left_size - offset) : 0;
        const auto bn = offset < right_size ? std::min(count, right_size - offset) : 0;
        const auto av = read_exact(a, static_cast<qint64>(an)), bv = read_exact(b, static_cast<qint64>(bn));
        left_hash.addData(av); right_hash.addData(bv);
        for (quint64 i = 0; i < count; ++i) {
            if (i >= an || i >= bn || av[static_cast<qsizetype>(i)] != bv[static_cast<qsizetype>(i)]) {
                if (!range_length) range_start = offset + i;
                ++range_length; ++changed;
            } else finish_range();
        }
        offset += count;
    }
    finish_range();
    if (a.size() != static_cast<qint64>(left_size) || b.size() != static_cast<qint64>(right_size)) fail(QStringLiteral("An input size changed during comparison."));
    const auto left_sha = QString::fromLatin1(left_hash.result().toHex());
    const auto right_sha = QString::fromLatin1(right_hash.result().toHex());
    // A second read prevents accepting changes to already-compared portions.
    if (hash_file(a, stop) != left_sha || hash_file(b, stop) != right_sha) fail(QStringLiteral("An input changed during comparison."));
    QFile current_left(left_path), current_right(right_path);
    if (!current_left.open(QIODevice::ReadOnly) || !current_right.open(QIODevice::ReadOnly) ||
        hash_file(current_left, stop) != left_sha || hash_file(current_right, stop) != right_sha)
        fail(QStringLiteral("An input path was replaced during comparison."));
    return {{QStringLiteral("kind"), QStringLiteral("byte-comparison")}, {QStringLiteral("complete"), true},
            {QStringLiteral("left_sha256"), left_sha}, {QStringLiteral("right_sha256"), right_sha},
            {QStringLiteral("left_size"), hex(left_size)}, {QStringLiteral("right_size"), hex(right_size)},
            {QStringLiteral("equal"), changed == 0}, {QStringLiteral("changed_bytes"), hex(changed)},
            {QStringLiteral("range_count"), hex(range_count)}, {QStringLiteral("ranges"), ranges},
            {QStringLiteral("ranges_truncated"), range_count > static_cast<quint64>(ranges.size())}};
}

QJsonObject compare_functions(const AnalysisSnapshot& left, qsizetype left_index,
                              const AnalysisSnapshot& right, qsizetype right_index, std::stop_token stop) {
    if (left_index < 0 || left_index >= left.functions.size() || right_index < 0 || right_index >= right.functions.size())
        fail(QStringLiteral("Function comparison index is out of range."));
    const auto& a = left.functions[left_index]; const auto& b = right.functions[right_index];
    if (a.instructions.size() > 4096 || b.instructions.size() > 4096) fail(QStringLiteral("Function comparison is limited to 4096 instructions per input."));
    QVector<QString> av, bv;
    QCryptographicHash ah(QCryptographicHash::Sha256), bh(QCryptographicHash::Sha256);
    for (const auto& item : a.instructions) { check_cancelled(stop); av.push_back(shape(item)); ah.addData(item.bytes); }
    for (const auto& item : b.instructions) { check_cancelled(stop); bv.push_back(shape(item)); bh.addData(item.bytes); }
    QVector<int> previous(bv.size() + 1, 0), current(bv.size() + 1, 0);
    for (qsizetype i = 0; i < av.size(); ++i) {
        check_cancelled(stop);
        current[0] = 0;
        for (qsizetype j = 0; j < bv.size(); ++j)
            current[j + 1] = av[i] == bv[j] ? previous[j] + 1 : std::max(previous[j + 1], current[j]);
        previous.swap(current);
    }
    const int matches = previous.back();
    const auto denominator = std::max(av.size(), bv.size());
    return {{QStringLiteral("kind"), QStringLiteral("instruction-shape-comparison")},
            {QStringLiteral("left"), a.address.text()}, {QStringLiteral("right"), b.address.text()},
            {QStringLiteral("left_instructions"), static_cast<int>(av.size())}, {QStringLiteral("right_instructions"), static_cast<int>(bv.size())},
            {QStringLiteral("longest_common_subsequence"), matches},
            {QStringLiteral("similarity"), denominator ? static_cast<double>(matches) / static_cast<double>(denominator) : 1.0},
            {QStringLiteral("identical_instruction_bytes"), ah.result() == bh.result()},
            {QStringLiteral("same_architecture"), left.metadata.architecture == right.metadata.architecture},
            {QStringLiteral("partial_input"), a.partial || b.partial},
            {QStringLiteral("semantic_equivalence_proven"), false},
            {QStringLiteral("normalization"), QStringLiteral("Direct flow targets and PC-relative displacements are normalized; register names and other constants are retained. Similarity is not semantic equivalence.")}};
}

QJsonObject export_patched_copy(const QString& source, const QString& destination, const QJsonArray& patches,
                               const QString& protected_project_root, const QStringList& protected_paths,
                               std::stop_token stop) {
    const auto source_path = regular_file(source);
    if (protected_paths.size() > 100000) fail(QStringLiteral("At most 100000 protected paths may be supplied."));
    protect_destination(destination, source_path, protected_project_root, protected_paths, stop);
    QFile input(source_path);
    if (!input.open(QIODevice::ReadOnly)) fail(QStringLiteral("Cannot open patch source."));
    const auto size = static_cast<quint64>(input.size());
    if (size > patch_source_limit) fail(QStringLiteral("Patch export is limited to source files up to 8 GiB."));
    if (patches.isEmpty() || patches.size() > 4096) fail(QStringLiteral("Supply between 1 and 4096 patches."));
    struct Patch { quint64 offset; QByteArray expected; QByteArray replacement; };
    QVector<Patch> parsed;
    quint64 patched_bytes = 0;
    for (const auto& value : patches) {
        check_cancelled(stop);
        if (!value.isObject()) fail(QStringLiteral("Every patch must be an object."));
        const auto object = value.toObject();
        Patch patch{number(object.value(QStringLiteral("offset")), QStringLiteral("offset")),
                    bytes(object.value(QStringLiteral("expected")), QStringLiteral("expected"), 1024 * 1024),
                    bytes(object.value(QStringLiteral("replacement")), QStringLiteral("replacement"), 1024 * 1024)};
        if (patch.expected.size() != patch.replacement.size()) fail(QStringLiteral("Patching cannot insert or remove bytes."));
        if (patch.offset > size || static_cast<quint64>(patch.expected.size()) > size - patch.offset) fail(QStringLiteral("Patch extends beyond source."));
        patched_bytes += static_cast<quint64>(patch.expected.size());
        if (patched_bytes > 16 * chunk_size) fail(QStringLiteral("Patches exceed the 16 MiB aggregate limit."));
        parsed.push_back(std::move(patch));
    }
    std::sort(parsed.begin(), parsed.end(), [](const Patch& a, const Patch& b) { return a.offset < b.offset; });
    quint64 end = 0;
    for (const auto& patch : parsed) {
        if (patch.offset < end) fail(QStringLiteral("Overlapping patches are rejected."));
        end = patch.offset + static_cast<quint64>(patch.expected.size());
        if (!input.seek(static_cast<qint64>(patch.offset)) || read_exact(input, patch.expected.size()) != patch.expected)
            fail(QStringLiteral("Expected source bytes do not match at ") + hex(patch.offset));
    }
    const auto source_sha = hash_file(input, stop);
    if (!input.seek(0)) fail(QStringLiteral("Cannot rewind source."));
    QSaveFile output(destination_path(destination));
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) fail(QStringLiteral("Cannot open atomic patch export: ") + output.errorString());
    if (!output.setPermissions(input.permissions())) fail(QStringLiteral("Cannot preserve source file permissions."));
    QCryptographicHash observed(QCryptographicHash::Sha256), result(QCryptographicHash::Sha256);
    qsizetype patch_index = 0;
    for (quint64 offset = 0; offset < size;) {
        check_cancelled(stop);
        const auto count = std::min(chunk_size, size - offset);
        auto data = read_exact(input, static_cast<qint64>(count));
        observed.addData(data);
        while (patch_index < parsed.size() && parsed[patch_index].offset + static_cast<quint64>(parsed[patch_index].expected.size()) <= offset) ++patch_index;
        for (auto j = patch_index; j < parsed.size() && parsed[j].offset < offset + count; ++j) {
            const auto& patch = parsed[j];
            const auto begin = std::max(offset, patch.offset);
            const auto finish = std::min(offset + count, patch.offset + static_cast<quint64>(patch.expected.size()));
            const auto source_index = static_cast<qsizetype>(begin - patch.offset);
            const auto data_index = static_cast<qsizetype>(begin - offset);
            const auto length = static_cast<qsizetype>(finish - begin);
            if (data.mid(data_index, length) != patch.expected.mid(source_index, length)) fail(QStringLiteral("Source changed at a patch location."));
            data.replace(data_index, length, patch.replacement.mid(source_index, length));
        }
        result.addData(data);
        if (output.write(data) != data.size()) fail(QStringLiteral("Patch export write failed."));
        offset += count;
    }
    if (input.size() != static_cast<qint64>(size) || QString::fromLatin1(observed.result().toHex()) != source_sha
        || hash_file(input, stop) != source_sha) fail(QStringLiteral("Source changed while exporting; output was discarded."));
    QFile current_source(source_path);
    if (!current_source.open(QIODevice::ReadOnly) || hash_file(current_source, stop) != source_sha)
        fail(QStringLiteral("Source path was replaced during export; output was discarded."));
    check_cancelled(stop);
    protect_destination(destination, source_path, protected_project_root, protected_paths, stop);
    if (!output.commit()) fail(QStringLiteral("Cannot commit atomic patch export: ") + output.errorString());
    return {{QStringLiteral("destination"), destination_path(destination)}, {QStringLiteral("source_sha256"), source_sha},
            {QStringLiteral("output_sha256"), QString::fromLatin1(result.result().toHex())}, {QStringLiteral("size"), hex(size)},
            {QStringLiteral("patch_count"), static_cast<int>(parsed.size())}, {QStringLiteral("patched_bytes"), hex(patched_bytes)},
            {QStringLiteral("original_preserved"), true},
            {QStringLiteral("warning"), QStringLiteral("Signatures and file checksums are not regenerated; the exported file may no longer pass integrity checks.")}};
}
} // namespace reworkbench
