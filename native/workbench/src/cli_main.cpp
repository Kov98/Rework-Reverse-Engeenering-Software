#include "reworkbench/abi.hpp"
#include "reworkbench/analysis.hpp"
#include "reworkbench/benchmark.hpp"
#include "reworkbench/decompilation_cache.hpp"
#include "reworkbench/debugger.hpp"
#include "reworkbench/enrichment.hpp"
#include "reworkbench/function_abi.hpp"
#include "reworkbench/hir.hpp"
#include "reworkbench/control_regions.hpp"
#include "reworkbench/structuring_plan.hpp"
#include "reworkbench/interprocedural.hpp"
#include "reworkbench/effect_summary.hpp"
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
QJsonObject abi_hint_json(const BinaryMetadata& metadata) {
    const auto profile = suggest_abi(metadata);
    if (!profile) return {{ "status", "unknown" }, { "verified_for_function", false }};
    auto strings = [](const QStringList& values) {
        QJsonArray result;
        for (const auto& value : values) result.append(value);
        return result;
    };
    return {
        { "status", "candidate_from_file_format" },
        { "verified_for_function", false },
        { "id", profile->id },
        { "name", profile->name },
        { "integer_args", strings(profile->integer_argument_registers) },
        { "vector_args", strings(profile->vector_argument_registers) },
        { "caller_saved_gprs", strings(profile->caller_saved_gprs) },
        { "callee_saved_gprs", strings(profile->callee_saved_gprs) },
        { "callee_saved_vector_registers", strings(profile->callee_saved_vector_registers) },
        { "integer_returns", strings(profile->integer_return_registers) },
        { "vector_returns", strings(profile->vector_return_registers) },
        { "shadow_space_bytes", int(profile->shadow_space_bytes) },
        { "red_zone_bytes", int(profile->red_zone_bytes) },
        { "entry_rsp_first_stack_arg_offset", int(profile->stack_argument_start_from_entry_rsp) },
        { "stack_alignment_bytes", int(profile->stack_alignment_bytes) },
        { "vector_args_follow_position", profile->positional_vector_arguments }
    };
}
QJsonArray stack_accesses_json(const QVector<AbiStackAccess>& accesses) {
    QJsonArray result;
    for (const auto& access : accesses) {
        result.append(QJsonObject{
            { "instruction", access.instruction.text() },
            { "offset_bytes", static_cast<qint64>(access.stack_offset_bytes) },
            { "width_bits", access.width_bits }
        });
    }
    return result;
}
QJsonObject call_effects_json(const CallEffectContract& effects) {
    return {
        { "status", effects.status },
        { "candidate_caller_saved_gprs",
            QJsonArray::fromStringList(effects.candidate_caller_saved_gprs) },
        { "candidate_callee_saved_gprs",
            QJsonArray::fromStringList(effects.candidate_callee_saved_gprs) },
        { "all_tracked_register_effects_unknown",
            effects.all_tracked_register_effects_unknown },
        { "memory_reads_unknown", effects.memory_reads_unknown },
        { "memory_writes_unknown", effects.memory_writes_unknown },
        { "control_effects_unknown", effects.control_effects_unknown },
        { "vector_register_effects_unknown",
            effects.vector_register_effects_unknown },
        { "can_assume_return", effects.can_assume_return }
    };
}
QJsonObject stack_frame_json(const StackFrameObservation& frame) {
    QJsonArray candidates;
    for (const auto& slot : frame.slots) {
        QJsonArray sources;
        for (const auto& source : slot.evidence) {
            sources.append(QJsonObject{
                { "instruction", source.instruction.text() },
                { "entry_rsp_offset_bytes", static_cast<qint64>(source.entry_rsp_offset_bytes) },
                { "width_bits", source.width_bits },
                { "kind", source.write ? "write" : "read" }
            });
        }
        candidates.append(QJsonObject{
            { "offset_from_entry_rsp_bytes", static_cast<qint64>(slot.entry_rsp_offset_bytes) },
            { "size_bytes", int(slot.size_bytes) },
            { "region", slot.region },
            { "modeled_reads", int(slot.modeled_reads) },
            { "modeled_writes", int(slot.modeled_writes) },
            { "overlaps_other_candidates", slot.overlaps_other_candidates },
            { "evidence", sources }
        });
    }
    QJsonArray warnings;
    for (const auto& warning : frame.warnings) warnings.append(warning);
    return {
        { "address_basis", frame.address_basis },
        { "verified_source_variables", false },
        { "partial", frame.partial },
        { "limited", frame.limited },
        { "unresolved_memory_accesses", int(frame.unresolved_memory_accesses) },
        { "unknown_machine_effects_present", frame.unknown_machine_effects_present },
        { "candidates", candidates },
        { "warnings", warnings }
    };
}
QJsonObject stack_value_flow_json(const StackValueFlowObservation& flow) {
    QJsonArray relations;
    for (const auto& item : flow.read_relations) {
        QJsonArray writes;
        for (const auto& instruction : item.possible_source_writes)
            writes.append(instruction.text());
        QJsonArray writer_nodes;
        for (const auto node_id : item.possible_source_write_nodes)
            writer_nodes.append(static_cast<int>(node_id));
        relations.append(QJsonObject{
            { "read_instruction", item.read_instruction.text() },
            { "read_ir_node", static_cast<int>(item.read_ir_node) },
            { "entry_rsp_offset_bytes", static_cast<qint64>(item.entry_rsp_offset_bytes) },
            { "size_bytes", static_cast<int>(item.size_bytes) },
            { "possible_source_writes", writes },
            { "possible_source_write_nodes", writer_nodes },
            { "may_read_entry_memory", item.may_read_entry_memory },
            { "unknown_memory_effect_may_intervene",
                item.unknown_memory_effect_may_intervene },
            { "ambiguous_overlapping_write", item.ambiguous_overlapping_write },
            { "reporting_limited", item.reporting_limited }
        });
    }
    QJsonArray generations;
    for (const auto& item : flow.write_generations) {
        QJsonArray readers;
        for (const auto& read : item.possible_reaching_reads)
            readers.append(read.text());
        generations.append(QJsonObject{
            { "write_instruction", item.write_instruction.text() },
            { "write_ir_node", static_cast<int>(item.write_ir_node) },
            { "entry_rsp_offset_bytes", static_cast<qint64>(
                item.entry_rsp_offset_bytes) },
            { "size_bytes", static_cast<int>(item.size_bytes) },
            { "possible_reaching_reads", readers },
            { "may_cross_basic_blocks", item.may_cross_basic_blocks }
        });
    }
    QJsonArray warnings;
    for (const auto& warning : flow.warnings) warnings.append(warning);
    return {
        { "write_generations", generations },
        { "locations_examined", static_cast<int>(flow.locations_examined) },
        { "dataflow_converged", flow.dataflow_converged },
        { "has_unknown_aliases", flow.has_unknown_aliases },
        { "limited", flow.limited },
        { "verified_source_variables", false },
        { "read_relations", relations },
        { "warnings", warnings }
    };
}
QJsonObject memory_dependence_json(const MemoryDependenceObservation& report) {
    QJsonArray uses;
    for (const auto& use : report.uses) {
        QJsonArray sources;
        for (quint32 id : use.possible_defining_store_nodes)
            sources.append(static_cast<int>(id));
        uses.append(QJsonObject{
            { "instruction", use.instruction.text() },
            { "read_ir_node", static_cast<int>(use.read_ir_node) },
            { "offset_from_entry_rsp_bytes", static_cast<qint64>(
                use.entry_rsp_offset_bytes) },
            { "size_bytes", static_cast<int>(use.size_bytes) },
            { "possible_store_nodes", sources },
            { "may_use_entry_memory", use.may_use_entry_memory },
            { "memory_phi_candidate", use.needs_memory_phi },
            { "unknown_write_may_intervene", use.may_depend_on_unknown_write },
            { "partial_alias", use.has_partial_alias },
            { "producer_list_incomplete", use.incomplete_producers }
        });
    }
    QJsonArray overlaps;
    for (const auto& pair : report.overlap_pairs) {
        overlaps.append(QJsonObject{
            { "first_offset", static_cast<qint64>(pair.first_offset_bytes) },
            { "first_size", static_cast<int>(pair.first_size_bytes) },
            { "second_offset", static_cast<qint64>(pair.second_offset_bytes) },
            { "second_size", static_cast<int>(pair.second_size_bytes) },
            { "relation", pair.relation }
        });
    }
    QJsonArray warnings;
    for (const auto& warning : report.warnings) warnings.append(warning);
    return {
        { "scope", report.scope },
        { "complete", report.complete },
        { "limited", report.limited },
        { "unknown_memory_effects", report.unknown_memory_effects },
        { "disjoint_range_pairs", static_cast<int>(report.proven_disjoint_range_pairs) },
        { "overlapping_range_pairs", overlaps },
        { "uses", uses },
        { "warnings", warnings }
    };
}
QJsonObject interprocedural_json(const InterproceduralObservation& graph) {
    QJsonArray edges;
    for (const auto& call : graph.calls) {
        QJsonObject edge{
            { "caller_index", static_cast<int>(call.caller_index) },
            { "call_instruction", call.call_instruction.text() },
            { "classification", call.classification }
        };
        if (call.direct_target) edge.insert("direct_target", call.direct_target->text());
        if (call.recovered_callee_index)
            edge.insert("callee_index", static_cast<int>(*call.recovered_callee_index));
        edges.append(edge);
    }
    QJsonArray functions;
    for (const auto& fn : graph.functions) {
        functions.append(QJsonObject{
            { "function_index", static_cast<int>(fn.function_index) },
            { "entry", fn.entry.text() },
            { "name", fn.name },
            { "direct_internal_calls", static_cast<int>(fn.direct_internal_calls) },
            { "unresolved_calls", static_cast<int>(fn.unresolved_calls) },
            { "recovered_body_partial", fn.recovered_body_partial },
            { "direct_self_recursion_observed", fn.direct_self_recursion_observed },
            { "may_transitively_reach_unresolved", fn.may_transitively_reach_unresolved },
            { "register_effects_unknown", fn.register_effects_unknown },
            { "memory_effects_unknown", fn.memory_effects_unknown }
        });
    }
    QJsonArray warnings;
    for (const auto& warning : graph.warnings) warnings.append(warning);
    return {
        { "status", graph.graph_status },
        { "complete", graph.complete },
        { "limited", graph.limited },
        { "functions", functions },
        { "calls", edges },
        { "warnings", warnings }
    };
}
QJsonObject memory_ssa_json(const MemorySSAObservation& report) {
    constexpr qsizetype max_display = 1024;
    QJsonArray versions;
    for (qsizetype index = 0; index < report.versions.size()
            && index < max_display; ++index) {
        const auto& version = report.versions[index];
        QJsonArray parents, predecessor_blocks;
        for (quint32 parent : version.incoming_versions)
            parents.append(static_cast<int>(parent));
        for (qsizetype predecessor : version.incoming_predecessors)
            predecessor_blocks.append(static_cast<int>(predecessor));
        QJsonObject record{
            { "version", static_cast<int>(version.id) },
            { "original_ir_node", static_cast<int>(version.original_ir_node) },
            { "kind", version.kind },
            { "instruction", version.instruction.text() },
            { "block", static_cast<int>(version.basic_block) },
            { "phi_incoming_versions", parents },
            { "phi_predecessor_blocks", predecessor_blocks },
            { "opaque_effect", version.unknown_effect }
        };
        if (version.previous_version)
            record.insert("previous_version",
                static_cast<int>(*version.previous_version));
        versions.append(record);
    }
    QJsonArray uses;
    for (qsizetype index = 0; index < report.reads.size()
            && index < max_display; ++index) {
        const auto& use = report.reads[index];
        uses.append(QJsonObject{
            { "load_ir_node", static_cast<int>(use.load_ir_node) },
            { "instruction", use.instruction.text() },
            { "block", static_cast<int>(use.basic_block) },
            { "memory_version", static_cast<int>(use.observed_memory_version) },
            { "opaque_effect_may_reach", use.may_contain_unknown_effects }
        });
    }
    QJsonArray warnings;
    for (const auto& warning : report.warnings) warnings.append(warning);
    return {
        { "scope", report.scope },
        { "complete", report.complete },
        { "limited", report.limited },
        { "opaque_effects_present", report.opaque_effects_present },
        { "total_versions", static_cast<int>(report.versions.size()) },
        { "total_load_uses", static_cast<int>(report.reads.size()) },
        { "display_truncated", report.versions.size() > max_display
            || report.reads.size() > max_display },
        { "versions", versions },
        { "load_uses", uses },
        { "warnings", warnings }
    };
}
QJsonObject memory_region_json(const MemoryRegionObservation& report) {
    constexpr qsizetype max_display = 256;
    QJsonArray accesses, pairs, warnings;
    for (qsizetype i = 0; i < report.accesses.size() && i < max_display; ++i) {
        const auto& a = report.accesses[i];
        QJsonObject entry{
            { "access_ir_node", static_cast<int>(a.access_ir_node) },
            { "pointer_ir_node", static_cast<int>(a.pointer_ir_node) },
            { "instruction", a.instruction.text() },
            { "block", static_cast<int>(a.block) },
            { "write", a.write },
            { "width_bits", a.width_bits },
            { "region", a.region }
        };
        if (a.entry_rsp_offset_bytes)
            entry.insert("offset_from_entry_rsp_bytes",
                static_cast<qint64>(*a.entry_rsp_offset_bytes));
        if (a.absolute_address)
            entry.insert("absolute_address", hex(*a.absolute_address));
        if (!a.mapped_image_region.isEmpty())
            entry.insert("mapped_image_region", a.mapped_image_region);
        accesses.append(entry);
    }
    for (qsizetype i = 0; i < report.noteworthy_pairs.size()
            && i < max_display; ++i) {
        const auto& pair = report.noteworthy_pairs[i];
        pairs.append(QJsonObject{
            { "first_ir_node", static_cast<int>(pair.first_access_ir_node) },
            { "second_ir_node", static_cast<int>(pair.second_access_ir_node) },
            { "relation", pair.relation },
            { "reason", pair.reason }
        });
    }
    for (const auto& warning : report.warnings) warnings.append(warning);
    return {
        { "scope", report.scope },
        { "complete", report.complete },
        { "limited", report.limited },
        { "opaque_effects_present", report.opaque_effects_present },
        { "unknown_pointer_accesses", static_cast<int>(report.unknown_pointer_accesses) },
        { "must_overlap_pairs", static_cast<int>(report.must_overlap_pairs) },
        { "proven_disjoint_pairs", static_cast<int>(report.proven_disjoint_pairs) },
        { "may_alias_pairs", static_cast<int>(report.may_alias_pairs) },
        { "total_accesses", static_cast<int>(report.accesses.size()) },
        { "accesses", accesses },
        { "noteworthy_pairs", pairs },
        { "display_truncated", report.accesses.size() > max_display
            || report.noteworthy_pairs.size() > max_display },
        { "warnings", warnings }
    };
}
QJsonObject effects_json(const InterproceduralEffectsObservation& report) {
    constexpr qsizetype max_display_functions = 256;
    QJsonArray rows;
    for (qsizetype i = 0; i < report.functions.size()
            && i < max_display_functions; ++i) {
        const auto& function = report.functions[i];
        QJsonArray evidences, notes;
        for (const auto& item : function.evidence) {
            evidences.append(QJsonObject{
                { "domain", item.domain },
                { "observation", item.observation },
                { "confidence", item.confidence },
                { "instruction", item.instruction.text() },
                { "ir_node", static_cast<int>(item.ir_node) }
            });
        }
        for (const auto& note : function.warnings) notes.append(note);
        QJsonArray regions;
        for (const auto& region : function.regional_effects) {
            QJsonArray sites;
            for (const auto& site : region.sites) {
                sites.append(QJsonObject{
                    { "instruction", site.instruction.text() },
                    { "ir_node", static_cast<int>(site.ir_node) },
                    { "write", site.write },
                    { "confidence", site.confidence }
                });
            }
            QJsonObject item{
                { "region", region.region },
                { "observed_loads", static_cast<int>(region.observed_loads) },
                { "observed_stores", static_cast<int>(region.observed_stores) },
                { "transitive_read_observed", region.transitive_read_observed },
                { "transitive_write_observed", region.transitive_write_observed },
                { "samples_truncated", region.samples_truncated },
                { "sites", sites }
            };
            if (region.read_witness_function_index)
                item.insert("read_witness_function_index",
                    static_cast<int>(*region.read_witness_function_index));
            if (region.write_witness_function_index)
                item.insert("write_witness_function_index",
                    static_cast<int>(*region.write_witness_function_index));
            regions.append(item);
        }
        QJsonObject record{
            { "index", static_cast<int>(function.function_index) },
            { "entry", function.entry.text() },
            { "name", function.name },
            { "evidence_level", function.evidence_level },
            { "regional_effects_partial", function.regional_effects_partial },
            { "regional_effects", regions },
            { "direct_read_observed", function.direct_read_observed },
            { "direct_write_observed", function.direct_write_observed },
            { "transitive_read_observed", function.transitive_read_observed },
            { "transitive_write_observed", function.transitive_write_observed },
            { "may_reach_unresolved_call", function.may_reach_unresolved_call },
            { "may_read_unknown_memory", function.may_read_unknown_memory },
            { "may_write_unknown_memory", function.may_write_unknown_memory },
            { "register_effects_unknown", function.register_effects_unknown },
            { "control_effects_unknown", function.control_effects_unknown },
            { "signature_verified", function.signature_verified },
            { "purity_verified", function.purity_verified },
            { "analysis_partial", function.analysis_partial },
            { "evidence_truncated", function.report_truncated },
            { "evidence", evidences },
            { "warnings", notes }
        };
        if (function.read_witness_function_index)
            record.insert("read_witness_function_index",
                static_cast<int>(*function.read_witness_function_index));
        if (function.write_witness_function_index)
            record.insert("write_witness_function_index",
                static_cast<int>(*function.write_witness_function_index));
        rows.append(record);
    }
    QJsonArray warnings;
    for (const auto& warning : report.warnings) warnings.append(warning);
    return {
        { "status", report.status },
        { "complete", report.complete },
        { "limited", report.limited },
        { "analyzed_bodies", static_cast<int>(report.analyzed_bodies) },
        { "direct_edges", static_cast<int>(report.direct_edges) },
        { "unresolved_edges", static_cast<int>(report.unresolved_edges) },
        { "total_functions", static_cast<int>(report.functions.size()) },
        { "display_truncated", report.functions.size() > max_display_functions },
        { "functions", rows },
        { "warnings", warnings }
    };
}
QJsonObject type_evidence_json(const TypeEvidenceObservation& report) {
    constexpr qsizetype max_display = 256;
    QJsonArray values;
    for (qsizetype i = 0; i < report.values.size() && i < max_display; ++i) {
        const auto& value = report.values[i];
        QJsonArray clues;
        for (const auto& clue : value.clues) {
            clues.append(QJsonObject{
                { "kind", clue.kind },
                { "origin", clue.origin },
                { "confidence", clue.confidence },
                { "instruction", clue.instruction.text() },
                { "origin_ir_node", static_cast<int>(clue.origin_ir_node) },
                { "detail", clue.detail }
            });
        }
        values.append(QJsonObject{
            { "ir_node", static_cast<int>(value.ir_node) },
            { "definition", value.definition.text() },
            { "ir_representation", value.ir_representation },
            { "width_bits", value.width_bits },
            { "source_type_status", value.source_type_status },
            { "signed_operation_seen", value.signed_operation_seen },
            { "unsigned_operation_seen", value.unsigned_operation_seen },
            { "address_use_seen", value.address_use_seen },
            { "interpretation_conflict", value.interpretation_conflict },
            { "clues_truncated", value.clues_truncated },
            { "clues", clues }
        });
    }
    QJsonArray warnings;
    for (const auto& item : report.warnings) warnings.append(item);
    return {
        { "status", report.status },
        { "partial", report.partial },
        { "limited", report.limited },
        { "abi_was_candidate_only", report.abi_was_candidate_only },
        { "total_ir_nodes", static_cast<int>(report.total_ir_nodes) },
        { "reported_value_count", static_cast<int>(report.values.size()) },
        { "values_with_conflicts", static_cast<int>(report.values_with_conflicts) },
        { "display_truncated", report.values.size() > max_display },
        { "original_source_types_verified", false },
        { "values", values },
        { "warnings", warnings }
    };
}
QJsonObject control_regions_json(const ControlRegionObservation& report) {
    constexpr qsizetype max_display = 128;
    QJsonArray blocks, loops, joins, irreducible, warnings;
    auto indices = [](const QVector<qsizetype>& values) {
        QJsonArray result;
        for (qsizetype value : values) result.append(static_cast<int>(value));
        return result;
    };
    for (qsizetype i = 0; i < report.blocks.size() && i < max_display; ++i) {
        const auto& block = report.blocks[i];
        QJsonObject item{
            { "block_index", static_cast<int>(block.block_index) },
            { "reached_from_entry", block.reached_from_entry },
            { "can_reach_known_return", block.can_reach_known_return },
            { "dominators", indices(block.dominators) },
            { "post_dominators", indices(block.post_dominators) }
        };
        if (block.immediate_dominator)
            item.insert("immediate_dominator",
                        static_cast<int>(*block.immediate_dominator));
        if (block.immediate_post_dominator)
            item.insert("immediate_post_dominator",
                        static_cast<int>(*block.immediate_post_dominator));
        blocks.append(item);
    }
    for (qsizetype i = 0; i < report.natural_loops.size()
            && i < max_display; ++i) {
        const auto& loop = report.natural_loops[i];
        loops.append(QJsonObject{
            { "header_block", static_cast<int>(loop.header_block) },
            { "latch_block", static_cast<int>(loop.latch_block) },
            { "member_blocks", indices(loop.member_blocks) },
            { "single_entry_proven_in_observed_cfg",
                loop.single_entry_proven_in_observed_cfg },
            { "classification", loop.classification }
        });
    }
    for (qsizetype i = 0; i < report.conditional_joins.size()
            && i < max_display; ++i) {
        const auto& region = report.conditional_joins[i];
        joins.append(QJsonObject{
            { "header_block", static_cast<int>(region.header_block) },
            { "taken_block", static_cast<int>(region.taken_block) },
            { "not_taken_block", static_cast<int>(region.not_taken_block) },
            { "join_block", static_cast<int>(region.join_block) },
            { "classification", region.classification }
        });
    }
    for (qsizetype i = 0; i < report.irreducible_components.size()
            && i < max_display; ++i) {
        const auto& component = report.irreducible_components[i];
        irreducible.append(QJsonObject{
            { "member_blocks", indices(component.member_blocks) },
            { "entry_blocks", indices(component.entry_blocks) }
        });
    }
    for (const auto& warning : report.warnings) warnings.append(warning);
    return {
        { "scope", report.scope },
        { "total_blocks", static_cast<int>(report.blocks.size()) },
        { "observed_backedges", static_cast<int>(report.observed_backedges) },
        { "total_loop_candidates", static_cast<int>(report.natural_loops.size()) },
        { "total_conditional_candidates", static_cast<int>(report.conditional_joins.size()) },
        { "total_irreducible_components",
            static_cast<int>(report.irreducible_components.size()) },
        { "blocks", blocks },
        { "natural_loops", loops },
        { "conditional_joins", joins },
        { "irreducible_components", irreducible },
        { "dominators_converged", report.dominators_converged },
        { "post_dominators_converged", report.post_dominators_converged },
        { "post_dominators_reliable", report.post_dominators_reliable },
        { "cfg_closed", report.cfg_closed },
        { "has_unknown_exits", report.has_unknown_exits },
        { "source_control_verified", report.source_control_verified },
        { "limited", report.limited },
        { "partial", report.partial },
        { "display_truncated", report.blocks.size() > max_display
            || report.natural_loops.size() > max_display
            || report.conditional_joins.size() > max_display
            || report.irreducible_components.size() > max_display },
        { "warnings", warnings }
    };
}
QJsonObject ir_semantic_audit_json(const IrSemanticAudit& audit) {
    constexpr qsizetype max_display = 64;
    QJsonArray issues;
    for (qsizetype i = 0; i < audit.issues.size() && i < max_display; ++i) {
        const auto& item = audit.issues[i];
        issues.append(QJsonObject{
            { "code", item.code },
            { "severity", item.severity },
            { "ir_node", static_cast<int>(item.ir_node) },
            { "block", static_cast<int>(item.block) },
            { "detail", item.detail }
        });
    }
    return {
        { "scope", audit.scope },
        { "errors", static_cast<int>(audit.errors) },
        { "warnings", static_cast<int>(audit.warnings) },
        { "nodes_examined", static_cast<int>(audit.nodes_examined) },
        { "phi_inputs_examined", static_cast<int>(audit.phi_inputs_examined) },
        { "memory_operations_examined", static_cast<int>(audit.memory_operations_examined) },
        { "dominance_converged", audit.dominance_converged },
        { "cfg_consistent", audit.cfg_consistent },
        { "operand_types_consistent", audit.operand_types_consistent },
        { "ssa_uses_dominated", audit.ssa_uses_dominated },
        { "memory_tokens_well_formed", audit.memory_tokens_well_formed },
        { "opaque_effects_present", audit.opaque_effects_present },
        { "ir_incomplete", audit.ir_incomplete },
        { "structural_checks_passed", audit.structural_checks_passed },
        { "semantic_equivalence_proven", audit.semantic_equivalence_proven },
        { "rewrite_authorized", audit.rewrite_authorized },
        { "issues_truncated", audit.issues_truncated || audit.issues.size() > max_display },
        { "issues", issues },
        { "warnings_detail", QJsonArray::fromStringList(audit.warnings_detail) }
    };
}
QJsonObject structuring_plan_json(const HirStructuringPlan& plan) {
    constexpr qsizetype max_display = 128;
    const auto numbers = [](const QVector<qsizetype>& blocks) {
        QJsonArray result;
        for (qsizetype block : blocks) result.append(static_cast<int>(block));
        return result;
    };
    QJsonArray proposals, raw_blocks, warnings;
    for (qsizetype i = 0; i < plan.proposals.size() && i < max_display; ++i) {
        const auto& candidate = plan.proposals[i];
        QJsonObject record{
            { "kind", candidate.kind },
            { "header_block", static_cast<int>(candidate.header) },
            { "member_blocks", numbers(candidate.member_blocks) },
            { "confidence", candidate.confidence },
            { "blockers", QJsonArray::fromStringList(candidate.blockers) },
            { "structurally_eligible", candidate.structurally_eligible },
            { "rewrite_authorized", candidate.rewrite_authorized }
        };
        if (candidate.join_block)
            record.insert("join_block", static_cast<int>(*candidate.join_block));
        if (candidate.exit_block)
            record.insert("exit_block", static_cast<int>(*candidate.exit_block));
        proposals.append(record);
    }
    for (qsizetype i = 0; i < plan.fallback_blocks.size()
            && i < max_display; ++i) {
        const auto& block = plan.fallback_blocks[i];
        QJsonObject fallback{
            { "block_index", static_cast<int>(block.block_index) },
            { "entry", block.entry.text() },
            { "terminator", block.terminator },
            { "successors", numbers(block.successors) },
            { "unknown_successor", block.unknown_successor },
            { "reachable", block.reachable },
            { "covered_by_proposal", block.covered_by_proposal },
            { "proposal_count", static_cast<int>(block.proposal_count) },
            { "fallback_reason", block.fallback_reason }
        };
        if (block.condition_value_id)
            fallback.insert("condition_value_id",
                static_cast<int>(*block.condition_value_id));
        if (block.taken_address)
            fallback.insert("taken_address", block.taken_address->text());
        if (block.not_taken_address)
            fallback.insert("not_taken_address", block.not_taken_address->text());
        raw_blocks.append(fallback);
    }
    for (const auto& warning : plan.warnings) warnings.append(warning);
    return {
        { "version", plan.version },
        { "total_proposals", static_cast<int>(plan.proposals.size()) },
        { "eligible_proposals", static_cast<int>(plan.eligible_proposals) },
        { "blocked_proposals", static_cast<int>(plan.blocked_proposals) },
        { "overlapping_candidate_blocks",
            static_cast<int>(plan.overlapping_candidate_blocks) },
        { "total_fallback_blocks", static_cast<int>(plan.fallback_blocks.size()) },
        { "uncovered_reachable_blocks", numbers(plan.uncovered_reachable_blocks) },
        { "raw_cfg_preserved", plan.raw_cfg_preserved },
        { "ir_structural_checks_passed", plan.ir_structural_checks_passed },
        { "unproved_rewrite_obligations",
            QJsonArray::fromStringList(plan.unproved_rewrite_obligations) },
        { "emitted_source_code", plan.emitted_source_code },
        { "rewrite_authorized", plan.rewrite_authorized },
        { "all_reachable_blocks_have_fallback",
            plan.all_reachable_blocks_have_fallback },
        { "limited", plan.limited },
        { "partial", plan.partial },
        { "display_truncated", plan.proposals.size() > max_display
            || plan.fallback_blocks.size() > max_display },
        { "proposals", proposals },
        { "fallback_blocks", raw_blocks },
        { "warnings", warnings }
    };
}
QJsonObject hir_json(const HirObservation& report) {
    constexpr qsizetype max_display_values = 256;
    constexpr qsizetype max_display_blocks = 128;
    QJsonArray values, blocks, synthetic, warnings;
    for (qsizetype i = 0; i < report.values.size()
            && i < max_display_values; ++i) {
        const auto& value = report.values[i];
        QJsonArray operands, incoming;
        for (quint32 operand : value.operands)
            operands.append(static_cast<int>(operand));
        for (const auto& edge : value.phi_inputs) {
            incoming.append(QJsonObject{
                { "predecessor_block", static_cast<int>(edge.predecessor_block) },
                { "value_id", static_cast<int>(edge.source_value_id) }
            });
        }
        QJsonObject item{
            { "id", static_cast<int>(value.id) },
            { "instruction", value.source_instruction.text() },
            { "owner_block", static_cast<int>(value.owner_block) },
            { "operation", value.operation },
            { "representation", value.representation },
            { "width_bits", value.width_bits },
            { "source_type_status", value.source_type_status },
            { "effect_kind", value.effect_kind },
            { "operands", operands },
            { "phi_inputs", incoming },
            { "interpretation_clues",
                QJsonArray::fromStringList(value.interpretation_clues) },
            { "conflicting_interpretations", value.conflicting_interpretations },
            { "clues_incomplete", value.clues_incomplete },
            { "bit_offset", value.extract_insert_bit_offset },
            { "original_ir_text", value.original_ir_text }
        };
        if (value.constant) item.insert("constant_hex", hex(*value.constant));
        values.append(item);
    }
    for (qsizetype i = 0; i < report.blocks.size()
            && i < max_display_blocks; ++i) {
        const auto& block = report.blocks[i];
        QJsonArray belongs, definitions, predecessors, successors;
        for (quint32 id : block.belonging_values)
            belongs.append(static_cast<int>(id));
        for (quint32 id : block.ordered_definitions)
            definitions.append(static_cast<int>(id));
        for (qsizetype predecessor : block.predecessors)
            predecessors.append(static_cast<int>(predecessor));
        for (qsizetype successor : block.successors)
            successors.append(static_cast<int>(successor));
        QJsonObject item{
            { "index", static_cast<int>(block.index) },
            { "entry_address", block.entry_address.text() },
            { "reachable", block.reachable },
            { "belonging_values", belongs },
            { "ordered_ir_definitions", definitions },
            { "predecessors", predecessors },
            { "successors", successors },
            { "terminator", block.terminator },
            { "target_resolution_incomplete", block.target_resolution_incomplete }
        };
        if (block.condition_value)
            item.insert("condition_value_id", static_cast<int>(*block.condition_value));
        if (block.return_target_value)
            item.insert("return_target_value_id", static_cast<int>(*block.return_target_value));
        if (block.taken_address)
            item.insert("taken_address", block.taken_address->text());
        if (block.not_taken_address)
            item.insert("not_taken_address", block.not_taken_address->text());
        if (block.resolved_taken_block)
            item.insert("resolved_taken_block", static_cast<int>(*block.resolved_taken_block));
        if (block.resolved_not_taken_block)
            item.insert("resolved_not_taken_block", static_cast<int>(*block.resolved_not_taken_block));
        blocks.append(item);
    }
    for (qsizetype i = 0; i < report.synthetic_or_entry_values.size()
            && i < max_display_values; ++i)
        synthetic.append(static_cast<int>(report.synthetic_or_entry_values[i]));
    for (const auto& warning : report.warnings) warnings.append(warning);
    return {
        { "version", report.version },
        { "semantic_status", report.semantic_status },
        { "entry_block", static_cast<int>(report.entry_block) },
        { "total_values", static_cast<int>(report.values.size()) },
        { "total_blocks", static_cast<int>(report.blocks.size()) },
        { "values", values },
        { "blocks", blocks },
        { "synthetic_or_entry_values", synthetic },
        { "display_truncated", report.values.size() > max_display_values
            || report.blocks.size() > max_display_blocks
            || report.synthetic_or_entry_values.size() > max_display_values },
        { "invalid_value_references", static_cast<int>(report.invalid_value_references) },
        { "ir_semantic_audit", ir_semantic_audit_json(report.ir_audit) },
        { "has_opaque_barriers", report.has_opaque_barriers },
        { "source_types_verified", report.source_types_verified },
        { "structured_control_flow_verified", report.structured_control_flow_verified },
        { "report_complete", report.report_complete },
        { "partial", report.partial },
        { "limited", report.limited },
        { "warnings", warnings }
    };
}
QJsonObject function_abi_json(const FunctionAbiObservation& observation) {
    QJsonArray callsites;
    for (const auto& call : observation.calls) {
        QJsonArray register_values;
        for (const auto& reg : call.possible_argument_producers) {
            QJsonArray addresses;
            for (const auto& address : reg.possible_producers)
                addresses.append(address.text());
            register_values.append(QJsonObject{
                { "register", reg.register_name },
                { "possible_producer_addresses", addresses }
            });
        }
        QJsonObject item{
            { "instruction", call.instruction.text() },
            { "target_known", call.target.has_value() },
            { "target_in_snapshot", call.target_in_snapshot },
            { "effects_unknown", call.effects_unknown },
            { "effects", call_effects_json(call.effects) },
            { "possible_incoming_register_values", register_values },
            { "possible_outgoing_stack_stores",
                stack_accesses_json(call.possible_outgoing_stack_stores) },
            { "outgoing_stack_offset_basis", "caller_rsp_before_call" },
            { "stack_observation_limited", call.stack_observation_limited },
            { "stack_memory_path_unknown", call.stack_memory_path_unknown }
        };
        if (call.target) item.insert("target", call.target->text());
        if (call.target_in_snapshot) item.insert("recovered_target_name", call.recovered_target_name);
        callsites.append(item);
    }
    QJsonArray warnings;
    for (const auto& warning : observation.warnings) warnings.append(warning);
    return {
        { "status", observation.status },
        { "abi_candidate", observation.profile_id },
        { "verified_for_function", false },
        { "partial", observation.partial },
        { "modeled_instructions", int(observation.modeled_instructions) },
        { "total_instructions", int(observation.total_instructions) },
        { "observed_integer_input_dependencies",
            QJsonArray::fromStringList(observation.observed_integer_input_dependencies) },
        { "possible_integer_return_writes",
            QJsonArray::fromStringList(observation.possible_integer_return_writes) },
        { "possible_incoming_stack_reads",
            stack_accesses_json(observation.possible_incoming_stack_reads) },
        { "incoming_stack_offset_basis", "callee_rsp_at_entry" },
        { "stack_frame", stack_frame_json(observation.frame) },
        { "stack_value_flow", stack_value_flow_json(observation.value_flow) },
        { "memory_dependence", memory_dependence_json(observation.memory_dependence) },
        { "memory_ssa", memory_ssa_json(observation.memory_ssa) },
        { "memory_regions", memory_region_json(observation.memory_regions) },
        { "type_evidence", type_evidence_json(observation.type_evidence) },
        { "incoming_stack_observation_limited",
            observation.incoming_stack_observation_limited },
        { "calls", callsites },
        { "warnings", warnings }
    };
}
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
        {"commands", QJsonArray{"analyze", "inspect", "functions", "xrefs", "strings", "decompile", "abi-observe", "type-observe", "hir-observe", "ir-audit", "cfg-structure", "structuring-plan", "call-graph", "effects-observe", "ir", "symbols", "diff", "diff-functions", "patch", "emulate", "debug", "plugins", "plugin-run", "benchmark"}}, {"runtime_validated", false}};
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
        return {{"format", format_name(metadata.format)}, {"architecture", architecture_name(metadata.architecture)}, {"size", hex(metadata.size)}, {"details", enriched.details}, {"abi_hint", abi_hint_json(metadata)}, {"warnings", warnings}};
    }
    static const QStringList analysis_commands{"analyze", "functions", "xrefs", "strings", "decompile", "abi-observe", "type-observe", "hir-observe", "ir-audit", "cfg-structure", "structuring-plan", "call-graph", "effects-observe", "ir", "symbols", "emulate", "diff-functions"};
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
    if (command == QStringLiteral("effects-observe")) {
        return { { "interprocedural_effects",
            effects_json(observe_interprocedural_effects(snapshot)) } };
    }
    if (command == QStringLiteral("call-graph")) {
        return { { "interprocedural",
            interprocedural_json(observe_interprocedural_calls(snapshot)) } };
    }
    if (command == QStringLiteral("hir-observe")
        || command == QStringLiteral("ir-audit")
        || command == QStringLiteral("cfg-structure")
        || command == QStringLiteral("structuring-plan")) {
        const auto hir = observe_function_hir(snapshot, context.function_index());
        if (command == QStringLiteral("ir-audit"))
            return { { "ir_semantic_audit", ir_semantic_audit_json(hir.ir_audit) },
                     { "hir_partial", hir.partial } };
        const auto regions = observe_hir_control_regions(hir);
        if (command == QStringLiteral("cfg-structure"))
            return { { "control_regions", control_regions_json(regions) },
                     { "hir_version", hir.version },
                     { "hir_partial", hir.partial } };
        const auto plan = plan_hir_structuring(hir, regions);
        if (command == QStringLiteral("structuring-plan"))
            return { { "structuring_plan", structuring_plan_json(plan) },
                     { "hir_version", hir.version },
                     { "hir_partial", hir.partial } };
        return { { "hir", hir_json(hir) },
                 { "control_regions", control_regions_json(regions) },
                 { "structuring_plan", structuring_plan_json(plan) } };
    }
    if (command == QStringLiteral("type-observe")) {
        return { { "type_evidence",
            type_evidence_json(observe_function_types(
                snapshot, context.function_index())) } };
    }
    if (command == QStringLiteral("abi-observe")) {
        std::optional<AbiProfile> chosen_profile;
        const auto id = context.text("abi");
        if (!id.isEmpty()) {
            chosen_profile = abi_profile_by_id(id);
            if (!chosen_profile) fail("ABI desconhecida; use windows-x64 ou sysv-amd64.");
        }
        return {
            { "abi_hint", abi_hint_json(snapshot.metadata) },
            { "observation", function_abi_json(observe_function_abi(
                snapshot, context.function_index(), chosen_profile)) }
        };
    }
    if (command == QStringLiteral("decompile")) {
        const auto result = cached_decompile(context.project, snapshot, context.function_index());
        QJsonArray warnings; for (const auto& item : result.warnings) warnings.append(item);
        context.export_bytes(result.pseudocode.toUtf8());
        return {{"pseudocode", result.pseudocode}, {"ssa", result.ssa_text}, {"modeled", int(result.modeled_instructions)}, {"total", int(result.total_instructions)}, {"complete", result.complete}, {"abi_hint", abi_hint_json(snapshot.metadata)}, {"warnings", warnings}};
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
    QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("Automação nativa de engenharia reversa. Comandos: capabilities, inspect, analyze, functions, xrefs, strings, symbols, decompile, abi-observe, type-observe, hir-observe, ir-audit, cfg-structure, structuring-plan, call-graph, effects-observe, ir, diff, diff-functions, patch, emulate, debug, plugins, plugin-run, benchmark."));
    parser.addHelpOption(); parser.addVersionOption(); parser.addPositionalArgument("command", "Comando nativo");
    for (const auto* key : {"request", "input", "other", "output", "project", "analysis", "function", "abi", "arch", "base", "entry", "endian", "slice", "offset", "count", "symbols", "sha256", "command"})
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
