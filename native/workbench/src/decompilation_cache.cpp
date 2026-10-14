#include "reworkbench/decompilation_cache.hpp"
#include "serialization.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>

namespace reworkbench {
namespace {
QString cache_key(const AnalysisSnapshot& snapshot, qsizetype index, std::stop_token stop) {
    AnalysisSnapshot selected;
    selected.metadata = snapshot.metadata;
    selected.metadata.symbols.clear(); selected.metadata.imports.clear();
    selected.functions = {snapshot.functions[index]};
    selected.options = snapshot.options; selected.revision = snapshot.revision;
    selected.engine_version = snapshot.engine_version;
    selected.enrichment = snapshot.enrichment;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto part = [&](const QByteArray& bytes) { hash.addData(QByteArray::number(bytes.size())); hash.addData(":", 1); hash.addData(bytes); };
    part(QByteArrayLiteral("reworkbench-decompiler-default-v1"));
    part(QByteArray(decompiler_semantics_version));
    part(QJsonDocument(persistence::snapshot_json(selected, stop)).toJson(QJsonDocument::Compact));
    // Names/import context may affect rendered call targets. Feed incrementally.
    for (const auto& function : snapshot.functions) {
        check_cancelled(stop); part(function.address.text().toUtf8()); part(function.name.toUtf8());
    }
    for (const auto& symbol : snapshot.metadata.symbols) {
        check_cancelled(stop); part(symbol.address.text().toUtf8()); part(symbol.name.toUtf8()); part(symbol.kind.toUtf8());
    }
    for (const auto& imported : snapshot.metadata.imports) {
        check_cancelled(stop); part(imported.name.toUtf8()); part(imported.library.toUtf8());
        part(imported.slot ? imported.slot->text().toUtf8() : QByteArray{});
    }
    return QString::fromLatin1(hash.result().toHex());
}
QJsonObject encode(const DecompilationResult& result) {
    QJsonArray warnings; for (const auto& warning : result.warnings) warnings.append(warning);
    return {{"kind", "decompiler-default-v1"}, {"pseudocode", result.pseudocode}, {"ssa", result.ssa_text},
        {"warnings", warnings}, {"modeled", int(result.modeled_instructions)}, {"total", int(result.total_instructions)},
        {"blocks", int(result.reachable_blocks)}, {"nodes", int(result.ir_nodes)}, {"converged", result.dataflow_converged},
        {"truncated", result.output_truncated}, {"complete", result.complete}};
}
std::optional<DecompilationResult> decode(const QJsonObject& object) {
    if (object.value("kind").toString() != QStringLiteral("decompiler-default-v1") || !object.value("pseudocode").isString() || !object.value("ssa").isString() ||
        !object.value("warnings").isArray()) return {};
    DecompilationResult result;
    result.pseudocode = object.value("pseudocode").toString(); result.ssa_text = object.value("ssa").toString();
    if (result.pseudocode.size() > 2 * 1024 * 1024 || result.ssa_text.size() > 2 * 1024 * 1024 || object.value("warnings").toArray().size() > 10000) return {};
    for (const auto& item : object.value("warnings").toArray()) {
        if (!item.isString() || item.toString().size() > 4096) return {};
        result.warnings.append(item.toString());
    }
    auto count = [&](const char* key, quint32& destination, quint32 maximum) {
        const auto value = object.value(QLatin1String(key)); const double number = value.toDouble(-1);
        if (!value.isDouble() || !std::isfinite(number) || std::floor(number) != number || number < 0 || number > maximum) return false;
        destination = quint32(number); return true;
    };
    if (!count("modeled", result.modeled_instructions, 4096) || !count("total", result.total_instructions, 4096) ||
        !count("blocks", result.reachable_blocks, 4096) || !count("nodes", result.ir_nodes, 196608) || result.modeled_instructions > result.total_instructions) return {};
    for (const auto* key : {"converged", "truncated", "complete"}) if (!object.value(QLatin1String(key)).isBool()) return {};
    result.dataflow_converged = object.value("converged").toBool(); result.output_truncated = object.value("truncated").toBool(); result.complete = object.value("complete").toBool();
    return result;
}
}
DecompilationResult cached_decompile(const std::shared_ptr<Project>& project, const AnalysisSnapshot& snapshot, qsizetype index, std::stop_token stop) {
    check_cancelled(stop);
    if (index < 0 || index >= snapshot.functions.size()) throw std::runtime_error("Índice de função inválido.");
    QString key, cache_warning;
    if (project && snapshot.metadata.sha256.size() == 64) {
        key = cache_key(snapshot, index, stop);
        try {
            if (const auto stored = project->cached_result(key, stop)) if (auto result = decode(*stored)) return *result;
        } catch (const Cancelled&) { throw; }
        catch (const std::exception&) { cache_warning = QStringLiteral("Cache indisponível; decompilação recalculada."); }
    }
    auto result = decompile_function(snapshot, index, {}, stop);
    check_cancelled(stop);
    if (project && !key.isEmpty() && result.dataflow_converged && !result.output_truncated && !result.pseudocode.isEmpty()) {
        try { project->cache_result(key, encode(result), stop); }
        catch (const Cancelled&) { throw; }
        catch (const std::exception&) { cache_warning = QStringLiteral("Resultado exibido; cache não pôde ser gravado."); }
    }
    if (!cache_warning.isEmpty()) result.warnings.append(cache_warning);
    return result;
}
}
