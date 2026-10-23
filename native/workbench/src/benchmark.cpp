#include "reworkbench/benchmark.hpp"
#include "reworkbench/analysis.hpp"
#include "reworkbench/decompiler.hpp"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace reworkbench {
namespace {
quint64 address(const QJsonValue& value) {
    if (!value.isString()) throw std::runtime_error("Endereços do corpus devem ser strings hexadecimais.");
    const auto text = value.toString(); bool ok = false;
    const auto result = text.toULongLong(&ok, 16);
    if (!ok || text.size() > 18) throw std::runtime_error("Endereço inválido no corpus.");
    return result;
}
QString digest(const QString& path, std::stop_token stop) {
    if (!QFileInfo(path).isFile()) throw std::runtime_error("Amostra do corpus não é arquivo regular.");
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Amostra do corpus ilegível.");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        check_cancelled(stop); const auto bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFileDevice::NoError) throw std::runtime_error("Falha na leitura do corpus.");
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}
}
QJsonObject benchmark_corpus(const QJsonObject& manifest, std::stop_token stop, ProgressCallback progress) {
    std::stop_source operation;
    std::stop_callback external(stop, [&operation] { operation.request_stop(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    std::jthread watchdog([&operation, deadline](std::stop_token timer_stop) {
        std::mutex mutex; std::condition_variable_any condition; std::unique_lock lock(mutex);
        condition.wait_until(lock, timer_stop, deadline, [] { return false; });
        if (!timer_stop.stop_requested()) operation.request_stop();
    });
    stop = operation.get_token();
    const auto samples = manifest.value("samples").toArray();
    if (!manifest.value("samples").isArray() || samples.isEmpty() || samples.size() > 32) throw std::runtime_error("Corpus deve conter entre 1 e 32 amostras.");
    QJsonArray rows; QElapsedTimer overall; overall.start();
    for (const auto& value : samples) {
        check_cancelled(stop);
        if (overall.elapsed() >= 600000) { rows.append(QJsonObject{{"state", "corpus-deadline"}}); break; }
        if (!value.isObject()) throw std::runtime_error("Entrada inválida no corpus.");
        const auto sample = value.toObject(); const auto path = sample.value("path").toString();
        if (path.isEmpty()) throw std::runtime_error("Amostra do corpus sem caminho.");
        QJsonObject row{{"path", path}};
        try {
            const auto sha = digest(path, stop); row.insert("sha256", sha);
            if (!sample.value("sha256").isString() || sample.value("sha256").toString() != sha) throw std::runtime_error("SHA-256 obrigatório diverge do arquivo do corpus.");
            OpenOptions options;
            const auto config = sample.value("options").toObject();
            options.raw = config.value("raw").toBool();
            options.raw_architecture = architecture_from_name(config.value("architecture").toString());
            options.raw_byte_order = config.value("endian").toString() == QStringLiteral("big") ? ByteOrder::Big : ByteOrder::Little;
            if (config.contains("base")) options.raw_base = address(config.value("base"));
            const auto entries = config.value("entries").toArray();
            if (entries.size() > 4096) throw std::runtime_error("Entradas excessivas no corpus.");
            for (const auto& entry : entries) options.entries.append({"ram", address(entry)});
            if (config.contains("slice")) {
                bool valid = false; const auto index = config.value("slice").toString().toUInt(&valid);
                if (!valid || index >= 256) throw std::runtime_error("Slice inválido no corpus.");
                options.slice_index = index;
            }
            AnalysisLimits limits;
            limits.timeout_seconds = std::min(60, int((600000 - overall.elapsed()) / 1000));
            QElapsedTimer timer; timer.start();
            const auto snapshot = analyze_binary(path, options, limits, stop, progress);
            row.insert("analysis_milliseconds", double(timer.elapsed()));
            if (digest(path, stop) != sha) throw std::runtime_error("Amostra do corpus mudou durante a análise.");
            QSet<QString> recovered;
            quint64 instructions = 0;
            for (const auto& function : snapshot.functions) { recovered.insert(function.address.text()); instructions += quint64(function.instructions.size()); }
            row.insert("functions", int(snapshot.functions.size())); row.insert("instructions", hex(instructions)); row.insert("state", snapshot.state);
            row.insert("engine", snapshot.engine_version); row.insert("decoder", decoder_description());
            QJsonArray expected_matches, expected_missing;
            const auto expected = sample.value("expected_functions").toArray();
            if (expected.size() > 10000) throw std::runtime_error("Referência de funções excessiva.");
            QSet<QString> expected_set;
            for (const auto& item : expected) expected_set.insert(Address{"ram", address(item)}.text());
            for (const auto& item : expected_set) (recovered.contains(item) ? expected_matches : expected_missing).append(item);
            row.insert("expected_matches", expected_matches); row.insert("expected_missing", expected_missing);
            row.insert("expected_reference_complete", sample.value("reference_complete").toBool());
            if (sample.value("reference_complete").toBool()) {
                quint32 extra = 0; for (const auto& item : recovered) if (!expected_set.contains(item)) ++extra;
                row.insert("unexpected_candidates", int(extra));
            }
            // Coverage is reported separately from semantic correctness.
            quint64 modeled = 0, total = 0; quint32 visited = 0;
            timer.restart();
            for (qsizetype index = 0; index < snapshot.functions.size() && visited < 32 && timer.elapsed() < 10000 && overall.elapsed() < 600000; ++index, ++visited) {
                check_cancelled(stop); const auto decompiled = decompile_function(snapshot, index, {}, stop);
                modeled += decompiled.modeled_instructions; total += decompiled.total_instructions;
            }
            row.insert("decompilation_sampled_functions", int(visited)); row.insert("modeled_instructions", hex(modeled)); row.insert("decompiled_instruction_total", hex(total));
            row.insert("semantic_equivalence_verified", false);
        } catch (const Cancelled&) { throw; }
        catch (const std::exception& error) { row.insert("state", "error"); row.insert("error", QString::fromUtf8(error.what())); }
        rows.append(row);
        if (progress) progress({QStringLiteral("corpus"), quint64(rows.size()), quint64(samples.size())});
    }
    return {{"kind", "reworkbench-corpus-v1"}, {"samples", rows}, {"total_milliseconds", double(overall.elapsed())},
        {"notice", "Analysis timing and labeled candidate counts only. Coverage is not semantic correctness; no Ghidra comparison or memory measurement is fabricated."}};
}
}
