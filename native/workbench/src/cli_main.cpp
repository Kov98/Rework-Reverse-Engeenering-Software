#include "reworkbench/analysis.hpp"
#include "reworkbench/benchmark.hpp"
#include "reworkbench/decompilation_cache.hpp"
#include "reworkbench/debugger.hpp"
#include "reworkbench/enrichment.hpp"
#include "reworkbench/investigation.hpp"
#include "reworkbench/plugin.hpp"
#include "serialization.hpp"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reworkbench {
namespace {
[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
quint64 number(QString text) {
    if (text.startsWith(QStringLiteral("ram:"))) text.remove(0, 4);
    bool ok = false; const auto result = text.toULongLong(&ok, 16);
    if (!ok || text.isEmpty() || text.size() > 18) fail("Endereço/offset deve ser hexadecimal de até 64 bits.");
    return result;
}
QJsonObject read_request(const QString& path) {
    if (path.isEmpty()) return {};
    QFile file;
    const bool opened = path == QStringLiteral("-") ? file.open(stdin, QIODevice::ReadOnly) : (file.setFileName(path), file.open(QIODevice::ReadOnly));
    if (!opened) fail("Não foi possível abrir o JSON de entrada.");
    QByteArray bytes;
    while (!file.atEnd()) {
        const auto part = file.read(65536);
        if (part.isEmpty() && file.error() != QFileDevice::NoError) fail("Falha na leitura do JSON.");
        bytes += part;
        if (bytes.size() > 4 * 1024 * 1024) fail("JSON de entrada excede 4 MiB.");
    }
    QJsonParseError error; const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) fail("Entrada deve ser um objeto JSON válido.");
    return document.object();
}
QString hash_file(const QString& path) {
    if (!QFileInfo(path).isFile()) fail("Origem deve ser um arquivo regular.");
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) fail("Arquivo ilegível.");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const auto bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFileDevice::NoError) fail("Falha na leitura do arquivo.");
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}
struct Context {
    const QCommandLineParser& parser;
    QJsonObject request;
    std::shared_ptr<Project> project;
    std::optional<AnalysisSnapshot> snapshot;
    QString id;
    QString text(const QString& key, const QString& fallback = {}) const {
        if (parser.isSet(key)) return parser.value(key);
        const auto value = request.value(key);
        if (!value.isUndefined() && !value.isString()) fail("Parâmetro textual deve ser uma string JSON.");
        return value.isString() ? value.toString() : fallback;
    }
    bool flag(const QString& key) const { return parser.isSet(key) || request.value(key).toBool(); }
    int count(const QString& key, int fallback, int maximum) const {
        if (parser.isSet(key)) {
            bool ok = false; const auto value = parser.value(key).toInt(&ok);
            if (!ok || value < 0 || value > maximum) fail("Contagem fora do limite."); return value;
        }
        if (!request.contains(key)) return fallback;
        const auto value = request.value(key); const double number = value.toDouble(-1);
        if (!value.isDouble() || !std::isfinite(number) || number != std::floor(number) || number < 0 || number > maximum) fail("Contagem fora do limite.");
        return int(number);
    }
    OpenOptions options() const {
        OpenOptions options; options.raw = flag("raw");
        options.raw_architecture = architecture_from_name(text("arch"));
        const auto endian = text("endian", "little");
        if (endian != QStringLiteral("little") && endian != QStringLiteral("big")) fail("Endian deve ser little ou big.");
        options.raw_byte_order = endian == QStringLiteral("big") ? ByteOrder::Big : ByteOrder::Little;
        if (!text("base").isEmpty()) options.raw_base = number(text("base"));
        QStringList entries = parser.values("entry");
        if (request.contains("entries")) {
            if (!request.value("entries").isArray()) fail("entries deve ser uma lista.");
            for (const auto& entry : request.value("entries").toArray()) { if (!entry.isString()) fail("Entrada deve ser hexadecimal em string."); entries.append(entry.toString()); }
        }
        if (entries.size() > 4096) fail("Limite de entradas excedido.");
        for (const auto& entry : entries) options.entries.append({"ram", number(entry)});
        if (!text("slice").isEmpty()) {
            bool ok = false; const auto index = text("slice").toUInt(&ok);
            if (!ok || index >= 256) fail("Slice inválido."); options.slice_index = index;
        }
        return options;
    }
    void open_project() {
        const auto path = text("project"); if (path.isEmpty() || project) return;
        project = flag("create-project") ? Project::create(path) : Project::open(path);
    }
    void load() {
        open_project();
        if (snapshot) return;
        const auto input = text("input");
        if (!input.isEmpty()) {
            auto config = options(); Sample sample;
            QString path = input;
            if (project) { sample = project->import_sample(input); path = project->root() + QLatin1Char('/') + sample.relative_path; }
            const auto symbols = text("symbols");
            if (!symbols.isEmpty()) {
                if (project) { const auto map = project->import_sample(symbols); config.symbol_map_sha256 = map.sha256; config.symbol_map_path = project->root() + QLatin1Char('/') + map.relative_path; }
                else { config.symbol_map_path = symbols; config.symbol_map_sha256 = hash_file(symbols); }
            }
            if (project && !flag("force")) if (const auto reusable = project->reusable_analysis(sample, config)) {
                id = *reusable; snapshot = project->load_analysis(id); return;
            }
            const auto sha = project ? sample.sha256 : hash_file(path);
            snapshot = analyze_binary(path, config);
            if (!snapshot->metadata.sha256.isEmpty() && snapshot->metadata.sha256 != sha) fail("A origem mudou antes da análise; resultado recusado.");
            if (!project && hash_file(path) != sha) fail("Origem mudou durante a análise.");
            snapshot->metadata.sha256 = sha;
            if (project) {
                snapshot->metadata.original_name = sample.original_name;
                id = project->save_new_analysis(sample, *snapshot);
            }
            return;
        }
        if (!project) fail("Informe --input ou --project.");
        id = text("analysis");
        if (id.isEmpty()) { const auto analyses = project->analyses(); if (analyses.isEmpty()) fail("Projeto sem análises."); id = analyses.last().id; }
        snapshot = project->load_analysis(id);
    }
    qsizetype function_index() const {
        if (!snapshot || snapshot->functions.isEmpty()) fail("Nenhuma função disponível.");
        const auto selector = text("function"); if (selector.isEmpty()) return 0;
        for (qsizetype index = 0; index < snapshot->functions.size(); ++index) if (snapshot->functions[index].name == selector) return index;
        const auto address = number(selector);
        for (qsizetype index = 0; index < snapshot->functions.size(); ++index) if (snapshot->functions[index].address == Address{"ram", address}) return index;
        fail("Função não encontrada.");
    }
    void export_bytes(const QByteArray& bytes) const {
        const auto path = text("output"); if (path.isEmpty()) return;
        if (!project || id.isEmpty()) fail("Use --project para exportação protegida; resultados também são fornecidos em stdout.");
        project->export_artifact(id, bytes, path);
    }
};
QJsonObject summary(const Context& context) {
    const auto& snapshot = *context.snapshot; QJsonArray capabilities, warnings;
    for (const auto& item : snapshot.capabilities) capabilities.append(item);
    for (const auto& item : snapshot.warnings) warnings.append(item);
    for (const auto& item : snapshot.metadata.warnings) warnings.append(item);
    return {{"analysis_id", context.id}, {"state", snapshot.state}, {"engine", snapshot.engine_version}, {"sha256", snapshot.metadata.sha256},
        {"format", format_name(snapshot.metadata.format)}, {"architecture", architecture_name(snapshot.metadata.architecture)},
        {"size", hex(snapshot.metadata.size)}, {"functions", int(snapshot.functions.size())}, {"references", int(snapshot.references.size())},
        {"capabilities", capabilities}, {"warnings", warnings}};
}
QJsonObject execute(const QString& command, Context& context) {
    if (command == QStringLiteral("capabilities")) return {{"engine", AnalysisSnapshot{}.engine_version}, {"decoder", decoder_description()},
        {"decompiler", "own scalar x86/x64 SSA, partial semantics"}, {"debugger", "Windows explicit launch/attach; host and bitness limits apply"},
        {"commands", QJsonArray{"analyze", "inspect", "functions", "xrefs", "strings", "decompile", "ir", "symbols", "diff", "diff-functions", "patch", "emulate", "debug", "plugins", "plugin-run", "benchmark"}}, {"runtime_validated", false}};
    if (command == QStringLiteral("benchmark")) return benchmark_corpus(context.request);
    if (command == QStringLiteral("plugins")) return {{"extensions", discover_extensions(context.text("input"))}};
    if (command == QStringLiteral("plugin-run")) return invoke_extension(context.text("input"), context.text("sha256"), context.text("command"), context.request.value("arguments").toObject(), context.parser.isSet("allow-plugin"));
    if (command == QStringLiteral("debug")) return run_debug_session(context.request);
    if (command == QStringLiteral("diff")) return compare_binaries(context.text("input"), context.text("other"));
    if (command == QStringLiteral("patch")) {
        context.open_project();
        const auto patches = context.request.value("patches"); if (!patches.isArray()) fail("patches deve ser uma lista JSON.");
        return export_patched_copy(context.text("input"), context.text("output"), patches.toArray(), context.project ? context.project->root() : QString{}, context.project ? context.project->protected_sources() : QStringList{});
    }
    if (command == QStringLiteral("inspect")) {
        const auto file = std::make_shared<BinaryFile>(context.text("input")); const auto probe = probe_binary(file);
        QJsonArray slices; for (const auto& slice : probe.slices) slices.append(QJsonObject{{"index", int(slice.index)}, {"label", slice.label}, {"offset", hex(slice.file_offset)}, {"size", hex(slice.size)}});
        if (probe.format == BinaryFormat::FatMachO && context.text("slice").isEmpty()) return {{"format", format_name(probe.format)}, {"slices", slices}, {"selection_required", true}};
        const auto metadata = load_binary(file, context.options()); const auto enriched = enrich_binary(file, metadata);
        QJsonArray warnings; for (const auto& warning : enriched.warnings) warnings.append(warning);
        return {{"format", format_name(metadata.format)}, {"architecture", architecture_name(metadata.architecture)}, {"size", hex(metadata.size)}, {"details", enriched.details}, {"warnings", warnings}};
    }
    static const QStringList analysis_commands{"analyze", "functions", "xrefs", "strings", "decompile", "ir", "symbols", "emulate", "diff-functions"};
    if (!analysis_commands.contains(command)) fail("Comando desconhecido; consulte --help.");
    context.load();
    const auto& snapshot = *context.snapshot;
    if (command == QStringLiteral("analyze")) {
        if (!context.text("output").isEmpty()) {
            if (!context.project) fail("Use --project para gravar o relatório.");
            context.project->export_report(context.id, context.text("output"));
        }
        return summary(context);
    }
    if (command == QStringLiteral("symbols")) return {{"analysis", summary(context)}, {"details", snapshot.enrichment}};
    if (command == QStringLiteral("decompile")) {
        const auto result = cached_decompile(context.project, snapshot, context.function_index());
        QJsonArray warnings; for (const auto& item : result.warnings) warnings.append(item);
        context.export_bytes(result.pseudocode.toUtf8());
        return {{"pseudocode", result.pseudocode}, {"ssa", result.ssa_text}, {"modeled", int(result.modeled_instructions)}, {"total", int(result.total_instructions)}, {"complete", result.complete}, {"warnings", warnings}};
    }
    if (command == QStringLiteral("ir")) {
        const auto result = render_ir(snapshot.functions[context.function_index()], snapshot.metadata.architecture);
        context.export_bytes(result.text.toUtf8()); return {{"ir", result.text}, {"modeled", int(result.modeled_instructions)}, {"total", int(result.total_instructions)}};
    }
    if (command == QStringLiteral("emulate")) return emulate_function(snapshot, context.function_index(), context.request.value("initial").toObject());
    if (command == QStringLiteral("diff-functions")) {
        if (!context.project) fail("Comparação de funções requer --project e other_analysis no JSON.");
        const auto other = context.project->load_analysis(context.request.value("other_analysis").toString());
        const auto wanted = number(context.request.value("other_function").toString());
        qsizetype other_index = -1; for (qsizetype index = 0; index < other.functions.size(); ++index) if (other.functions[index].address.value == wanted) { other_index = index; break; }
        if (other_index < 0) fail("Função de comparação não encontrada.");
        return compare_functions(snapshot, context.function_index(), other, other_index);
    }
    const auto offset = context.count("offset", 0, 2000000), count = context.count("count", 200, 2000);
    QJsonArray rows; qsizetype total = 0;
    if (command == QStringLiteral("functions")) {
        total = snapshot.functions.size();
        for (qsizetype index = offset; index < total && rows.size() < count; ++index) {
            const auto& function = snapshot.functions[index]; rows.append(QJsonObject{{"address", function.address.text()}, {"name", function.name}, {"instructions", int(function.instructions.size())}, {"partial", function.partial}});
        }
    } else if (command == QStringLiteral("strings")) {
        total = snapshot.strings.size();
        for (qsizetype index = offset; index < total && rows.size() < count; ++index) rows.append(QJsonObject{{"offset", hex(snapshot.strings[index].file_offset)}, {"value", snapshot.strings[index].value}});
    } else {
        total = snapshot.references.size();
        for (qsizetype index = offset; index < total && rows.size() < count; ++index) { const auto& reference = snapshot.references[index]; rows.append(QJsonObject{{"source", reference.source.text()}, {"target", reference.target.text()}, {"type", reference.type}}); }
    }
    return {{"rows", rows}, {"offset", offset}, {"total", int(total)}};
}
}
}

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv); QCoreApplication::setApplicationName(QStringLiteral("reworkbench-cli")); QCoreApplication::setApplicationVersion(QStringLiteral("0.3.0"));
    QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("Automação nativa de engenharia reversa. Comandos: capabilities, inspect, analyze, functions, xrefs, strings, symbols, decompile, ir, diff, diff-functions, patch, emulate, debug, plugins, plugin-run, benchmark."));
    parser.addHelpOption(); parser.addVersionOption(); parser.addPositionalArgument("command", "Comando nativo");
    for (const auto* key : {"request", "input", "other", "output", "project", "analysis", "function", "arch", "base", "entry", "endian", "slice", "offset", "count", "symbols", "sha256", "command"})
        parser.addOption(QCommandLineOption(QString::fromLatin1(key), QStringLiteral("Parâmetro %1; endereços em hexadecimal, caminhos locais").arg(QString::fromLatin1(key)), "value"));
    for (const auto* key : {"raw", "create-project", "force", "allow-plugin"}) parser.addOption(QCommandLineOption(QString::fromLatin1(key), QStringLiteral("Ativar %1 explicitamente").arg(QString::fromLatin1(key))));
    parser.process(application);
    QJsonObject result; int status = 0;
    try {
        if (parser.positionalArguments().size() != 1) reworkbench::fail("Informe exatamente um comando.");
        reworkbench::Context context{parser, reworkbench::read_request(parser.value("request")), {}, {}, {}};
        result = reworkbench::execute(parser.positionalArguments().first(), context);
        result.insert("ok", true);
    } catch (const std::exception& error) { result = {{"ok", false}, {"error", QString::fromUtf8(error.what())}}; status = 1; }
    catch (...) { result = {{"ok", false}, {"error", "Falha nativa inesperada."}}; status = 1; }
    QFile output; if (!output.open(stdout, QIODevice::WriteOnly)) return 1;
    const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Indented);
    if (output.write(bytes) != bytes.size() || !output.flush()) return 1;
    return status;
}
