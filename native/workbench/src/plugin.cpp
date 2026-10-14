#include "reworkbench/plugin.hpp"
#include "reworkbench/types.hpp"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLibrary>
#include <QPluginLoader>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace reworkbench {
namespace {
[[noreturn]] void fail(const QString& reason) { throw std::runtime_error(reason.toStdString()); }
QString extension_path(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.isSymLink() || info.canonicalFilePath().isEmpty() || !QLibrary::isLibrary(info.fileName()))
        fail(QStringLiteral("Extension must be an existing native library, not a symbolic link."));
    if (info.size() < 1 || info.size() > 64 * 1024 * 1024) fail(QStringLiteral("Extension size must be between 1 byte and 64 MiB."));
    return info.canonicalFilePath();
}
QString hash_extension(const QString& path, std::stop_token stop) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) fail(QStringLiteral("Cannot read extension."));
    const auto size = file.size();
    if (size < 1 || size > 64 * 1024 * 1024) fail(QStringLiteral("Extension exceeds the size limit."));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (qint64 offset = 0; offset < size;) {
        check_cancelled(stop);
        const auto count = std::min<qint64>(1024 * 1024, size - offset);
        const auto bytes = file.read(count);
        if (bytes.size() != count) fail(QStringLiteral("Extension changed or could not be read."));
        hash.addData(bytes); offset += count;
    }
    if (file.size() != size) fail(QStringLiteral("Extension size changed while reading."));
    return QString::fromLatin1(hash.result().toHex());
}
QJsonObject manifest(QPluginLoader& loader) {
    const auto metadata = loader.metaData();
    if (metadata.isEmpty() || metadata.value(QStringLiteral("IID")).toString() != QLatin1String(REWORKBENCH_EXTENSION_IID))
        fail(QStringLiteral("Not a compatible ReWorkbench native extension."));
    const auto value = metadata.value(QStringLiteral("MetaData"));
    if (!value.isObject()) fail(QStringLiteral("Extension manifest is missing."));
    const auto data = value.toObject();
    if (QJsonDocument(data).toJson(QJsonDocument::Compact).size() > 64 * 1024) fail(QStringLiteral("Extension manifest exceeds 64 KiB."));
    if (!data.value(QStringLiteral("api_version")).isDouble() || data.value(QStringLiteral("api_version")).toDouble() != extension_api_version)
        fail(QStringLiteral("Unsupported extension API version."));
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$"));
    if (!data.value(QStringLiteral("id")).isString() || !identifier.match(data.value(QStringLiteral("id")).toString()).hasMatch())
        fail(QStringLiteral("Extension identifier is invalid."));
    const auto commands_value = data.value(QStringLiteral("commands"));
    if (!commands_value.isArray()) fail(QStringLiteral("Extension must declare its commands."));
    const auto commands = commands_value.toArray();
    if (commands.isEmpty() || commands.size() > 128) fail(QStringLiteral("Extension must declare between 1 and 128 commands."));
    QSet<QString> seen;
    for (const auto& command : commands) {
        if (!command.isString() || !identifier.match(command.toString()).hasMatch() || seen.contains(command.toString()))
            fail(QStringLiteral("Extension command is invalid or duplicated."));
        seen.insert(command.toString());
    }
    return data;
}
} // namespace

QJsonArray discover_extensions(const QString& directory, std::stop_token stop) {
    const QFileInfo directory_info(directory);
    if (!directory_info.exists() || !directory_info.isDir() || !directory_info.isReadable()) fail(QStringLiteral("Extension directory does not exist or cannot be read."));
    QDirIterator iterator(directory_info.canonicalFilePath(), QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot, QDirIterator::NoIteratorFlags);
    QJsonArray output;
    int inspected = 0;
    while (iterator.hasNext()) {
        check_cancelled(stop);
        iterator.next();
        if (++inspected > 4096) fail(QStringLiteral("Extension directory contains too many files; use a dedicated directory."));
        if (!QLibrary::isLibrary(iterator.fileName())) continue;
        if (output.size() >= 128) fail(QStringLiteral("Extension discovery is limited to 128 libraries."));
        const auto path = iterator.filePath();
        QJsonObject item{{QStringLiteral("path"), path}, {QStringLiteral("loaded"), false}};
        try {
            const auto canonical = extension_path(path);
            const auto sha = hash_extension(canonical, stop);
            QPluginLoader loader(canonical);
            const auto data = manifest(loader); // metadata read, no instance/load
            if (hash_extension(canonical, stop) != sha) fail(QStringLiteral("Extension changed during discovery."));
            item.insert(QStringLiteral("path"), canonical);
            item.insert(QStringLiteral("sha256"), sha);
            item.insert(QStringLiteral("manifest"), data);
            item.insert(QStringLiteral("compatible"), true);
        } catch (const Cancelled&) { throw; }
        catch (const std::exception& error) {
            item.insert(QStringLiteral("compatible"), false);
            item.insert(QStringLiteral("error"), QString::fromUtf8(error.what()));
        }
        output.push_back(item);
    }
    return output;
}

QJsonObject invoke_extension(const QString& path, const QString& expected_sha256,
                             const QString& command, const QJsonObject& arguments,
                             bool explicit_opt_in, std::stop_token stop) {
    if (!explicit_opt_in) fail(QStringLiteral("Native extension execution requires explicit opt-in."));
    static const QRegularExpression sha_pattern(QStringLiteral("^[A-Fa-f0-9]{64}$"));
    if (!sha_pattern.match(expected_sha256).hasMatch()) fail(QStringLiteral("An exact SHA-256 from extension discovery is required."));
    if (QJsonDocument(arguments).toJson(QJsonDocument::Compact).size() > 1024 * 1024) fail(QStringLiteral("Extension arguments exceed 1 MiB."));
    check_cancelled(stop);
    const auto canonical = extension_path(path);
    const auto sha = hash_extension(canonical, stop);
    if (sha != expected_sha256.toLower()) fail(QStringLiteral("Extension hash does not match the selected file."));
    QPluginLoader loader(canonical);
    // Keep code mapped for the process lifetime; unloading arbitrary native
    // extension code is unsafe if a library retains callbacks or threads.
    loader.setLoadHints(QLibrary::ResolveAllSymbolsHint | QLibrary::PreventUnloadHint);
    const auto data = manifest(loader);
    if (!data.value(QStringLiteral("commands")).toArray().contains(command)) fail(QStringLiteral("The extension does not declare that command."));
    if (hash_extension(canonical, stop) != sha) fail(QStringLiteral("Extension changed before loading."));
    check_cancelled(stop);
    QObject* object = loader.instance(); // first operation that can execute code
    if (!object) fail(QStringLiteral("Cannot load extension: ") + loader.errorString());
    auto* extension = qobject_cast<NativeExtension*>(object);
    if (!extension || extension->api_version() != extension_api_version) fail(QStringLiteral("Extension interface or runtime API version mismatch."));
    if (hash_extension(canonical, stop) != sha) fail(QStringLiteral("Extension changed while loading; code may already have executed."));
    check_cancelled(stop);
    const auto response = extension->invoke(command, arguments);
    if (QJsonDocument(response).toJson(QJsonDocument::Compact).size() > 8 * 1024 * 1024) fail(QStringLiteral("Extension response exceeds 8 MiB."));
    return {{QStringLiteral("extension"), data.value(QStringLiteral("id"))}, {QStringLiteral("sha256"), sha},
            {QStringLiteral("command"), command}, {QStringLiteral("result"), response},
            {QStringLiteral("trusted_native_execution"), true}, {QStringLiteral("sandboxed"), false}};
}
} // namespace reworkbench
