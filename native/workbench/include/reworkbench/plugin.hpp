#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <stop_token>

#define REWORKBENCH_EXTENSION_IID "org.reworkbench.NativeExtension/1.0"

namespace reworkbench {
inline constexpr int extension_api_version = 1;

// Implement as QObject + this interface, Q_PLUGIN_METADATA(IID ... FILE ...),
// Q_INTERFACES(reworkbench::NativeExtension). Manifest requires api_version:1,
// id:string and commands:[string]. Invocation executes trusted native code in-process.
class NativeExtension {
public:
    virtual ~NativeExtension() = default;
    [[nodiscard]] virtual int api_version() const noexcept = 0;
    [[nodiscard]] virtual QJsonObject invoke(const QString& command, const QJsonObject& arguments) = 0;
};

// Reads Qt metadata and hashes only; does not load or invoke libraries.
[[nodiscard]] QJsonArray discover_extensions(const QString& directory, std::stop_token stop = {});
// Requires explicitOptIn and the exact SHA-256 returned by discovery. Extensions
// are trusted code, not sandboxed. A plugin may have side effects during loading.
[[nodiscard]] QJsonObject invoke_extension(const QString& path, const QString& expected_sha256,
                                           const QString& command, const QJsonObject& arguments,
                                           bool explicit_opt_in, std::stop_token stop = {});
} // namespace reworkbench

Q_DECLARE_INTERFACE(reworkbench::NativeExtension, REWORKBENCH_EXTENSION_IID)
