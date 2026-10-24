#include "reworkbench/effect_summary.hpp"
#include "decompiler_internal.hpp"
#include "region_effect_analysis.hpp"

#include <QQueue>
#include <utility>

namespace reworkbench {
using namespace decompiler_detail;
namespace {

constexpr qsizetype max_analyzed_functions = 128;
constexpr qsizetype max_evidence_per_function = 32;
constexpr qsizetype max_function_warnings = 8;

void add_evidence(FunctionEffectSummary& summary, const Node& node,
                  quint32 ir_index, const QString& observation) {
    if (summary.evidence.size() >= max_evidence_per_function) {
        summary.report_truncated = true;
        return;
    }
    summary.evidence << FunctionEffectEvidence{
        QStringLiteral("memory"), observation,
        observation == QStringLiteral("opaque_effect")
            ? QStringLiteral("unknown") : QStringLiteral("inferred"),
        node.address, ir_index
    };
}

void warn(FunctionEffectSummary& summary, const QString& message) {
    if (summary.warnings.size() < max_function_warnings)
        summary.warnings << message;
}

} // namespace

InterproceduralEffectsObservation observe_interprocedural_effects(
    const AnalysisSnapshot& snapshot, std::stop_token stop) {
    InterproceduralEffectsObservation report;
    report.status = QStringLiteral("observational_only");
    report.warnings << QStringLiteral(
        "Observed effects are IR-derived MAY facts. Absence of a modeled "
        "load/store is NOT a proof of purity or of preserved registers.");
    report.warnings << QStringLiteral(
        "All call effects remain conservative and unknown: no ABI or "
        "function signature is independently verified.");

    const auto graph = observe_interprocedural_calls(snapshot, stop);
    const qsizetype count = snapshot.functions.size();
    if (graph.functions.size() != count) {
        report.limited = true;
        report.status = QStringLiteral("graph_unavailable");
        report.warnings << QStringLiteral(
            "Call graph could not cover every function; no transitive "
            "effects are reported.");
        return report;
    }

    report.limited = graph.limited;
    report.functions.reserve(count);
    QVector<QVector<quint32>> callers(count);

    for (qsizetype i = 0; i < count; ++i) {
        check_cancelled(stop);
        const auto& f = snapshot.functions[i];
        FunctionEffectSummary summary;
        summary.function_index = static_cast<quint32>(i);
        summary.entry = f.address;
        summary.name = f.name;
        summary.regional_effects = empty_region_effects();
        summary.analysis_partial = f.partial;
        summary.may_reach_unresolved_call = graph.limited
            || graph.functions[i].may_transitively_reach_unresolved;
        if (f.partial)
            warn(summary, QStringLiteral("Function discovery is partial."));
        report.functions << std::move(summary);
    }

    for (const auto& edge : graph.calls) {
        check_cancelled(stop);
        if (edge.caller_index >= static_cast<quint32>(count)) {
            report.limited = true;
            continue;
        }
        if (!edge.recovered_callee_index) {
            ++report.unresolved_edges;
            report.functions[edge.caller_index].may_reach_unresolved_call = true;
            continue;
        }
        if (*edge.recovered_callee_index >= static_cast<quint32>(count)) {
            report.limited = true;
            report.functions[edge.caller_index].may_reach_unresolved_call = true;
            continue;
        }
        ++report.direct_edges;
        callers[*edge.recovered_callee_index] << edge.caller_index;
    }

    const bool architecture_supported =
        snapshot.metadata.architecture == Architecture::X86
        || snapshot.metadata.architecture == Architecture::X64;
    const qsizetype budget = architecture_supported
        ? qMin(count, max_analyzed_functions) : 0;
    if (!architecture_supported) {
        report.limited = true;
        report.warnings << QStringLiteral(
            "Modeled direct effects currently require x86/x64 IR. "
            "Other architectures are reported as unknown.");
    }
    for (qsizetype i = 0; i < count; ++i) {
        check_cancelled(stop);
        auto& summary = report.functions[i];
        if (i >= budget) {
            report.limited = true;
            summary.analysis_partial = true;
            warn(summary, QStringLiteral(
                "Body not lifted: per-request function budget reached."));
            continue;
        }

        // Bound work on the user's machine and preserve an explicit
        // unknown status if an individual lift exceeds its budget.
        DecompilationOptions options;
        options.max_instructions = 2048;
        options.max_blocks = 384;
        options.max_ir_nodes = 8192;
        options.max_dataflow_iterations = 96;
        try {
            const auto program = lift(snapshot, snapshot.functions[i], options, stop);
            ++report.analyzed_bodies;
            summary.analysis_partial = summary.analysis_partial
                || program.incomplete || program.total != program.modeled;
            const auto regional = region_effect_analysis::observe(
                program, snapshot.metadata, stop);
            summary.regional_effects = regional.regions;
            summary.regional_effects_partial = regional.partial;
            if (regional.classification_limited)
                warn(summary, QStringLiteral(
                    "Some pointer regions are unclassified; no alias or "
                    "absence-of-effects inference is authorized."));
            for (qsizetype n = 0; n < program.nodes.size(); ++n) {
                if ((n & 0x7f) == 0) check_cancelled(stop);
                const auto& node = program.nodes[n];
                if (node.block < 0 || node.block >= program.blocks.size()
                    || !program.blocks[node.block].reachable) continue;
                if (node.op == Op::Load) {
                    summary.direct_read_observed = true;
                    add_evidence(summary, node, static_cast<quint32>(n),
                                 QStringLiteral("modeled_load"));
                } else if (node.op == Op::Store) {
                    summary.direct_write_observed = true;
                    add_evidence(summary, node, static_cast<quint32>(n),
                                 QStringLiteral("modeled_store"));
                } else if (node.op == Op::CallOpaque || node.op == Op::Opaque) {
                    summary.analysis_partial = true;
                    add_evidence(summary, node, static_cast<quint32>(n),
                                 QStringLiteral("opaque_effect"));
                }
            }
            if (summary.direct_read_observed || summary.direct_write_observed)
                summary.evidence_level = QStringLiteral("inferred");
            if (program.incomplete)
                warn(summary, QStringLiteral(
                    "Opaque instructions or incomplete lifting: "
                    "unobserved effects remain unknown."));
            if (summary.report_truncated)
                warn(summary, QStringLiteral(
                    "Evidence samples capped; positive effect flags "
                    "cover all modeled IR nodes visited."));
        } catch (const Limit& limit) {
            report.limited = true;
            summary.analysis_partial = true;
            warn(summary, QStringLiteral("IR lift budget exhausted: %1")
                .arg(QString::fromUtf8(limit.what())));
        }
        summary.transitive_read_observed = summary.direct_read_observed;
        summary.transitive_write_observed = summary.direct_write_observed;
        if (summary.direct_read_observed)
            summary.read_witness_function_index = summary.function_index;
        if (summary.direct_write_observed)
            summary.write_witness_function_index = summary.function_index;
        for (auto& region : summary.regional_effects) {
            region.transitive_read_observed = region.observed_loads != 0;
            region.transitive_write_observed = region.observed_stores != 0;
            if (region.transitive_read_observed)
                region.read_witness_function_index = summary.function_index;
            if (region.transitive_write_observed)
                region.write_witness_function_index = summary.function_index;
        }
    }

    // The remaining function bodies are unknown but can still have known
    // caller edges. Transitive positive evidence propagates monotonically
    // through cycles; UNKNOWN always stays UNKNOWN.
    QQueue<quint32> pending;
    QVector<quint8> in_queue(count, quint8(0));
    for (qsizetype i = 0; i < count; ++i) {
        pending.enqueue(static_cast<quint32>(i));
        in_queue[i] = 1;
    }
    while (!pending.isEmpty()) {
        check_cancelled(stop);
        const quint32 callee = pending.dequeue();
        in_queue[callee] = 0;
        const auto& observed = report.functions[callee];
        for (quint32 caller : callers[callee]) {
            auto& target = report.functions[caller];
            const bool read_before = target.transitive_read_observed;
            const bool write_before = target.transitive_write_observed;
            const bool unknown_before = target.may_reach_unresolved_call;
            if (!read_before && observed.transitive_read_observed)
                target.read_witness_function_index = observed.read_witness_function_index;
            if (!write_before && observed.transitive_write_observed)
                target.write_witness_function_index = observed.write_witness_function_index;
            target.transitive_read_observed |= observed.transitive_read_observed;
            target.transitive_write_observed |= observed.transitive_write_observed;
            target.may_reach_unresolved_call |= observed.may_reach_unresolved_call;
            bool regional_changed = false;
            const bool partial_before = target.regional_effects_partial;
            target.regional_effects_partial |= observed.regional_effects_partial;
            regional_changed |= (!partial_before && target.regional_effects_partial);
            for (qsizetype region_id = 0; region_id < target.regional_effects.size()
                    && region_id < observed.regional_effects.size(); ++region_id) {
                auto& destination = target.regional_effects[region_id];
                const auto& source = observed.regional_effects[region_id];
                if (!destination.transitive_read_observed
                    && source.transitive_read_observed) {
                    destination.transitive_read_observed = true;
                    destination.read_witness_function_index =
                        source.read_witness_function_index;
                    regional_changed = true;
                }
                if (!destination.transitive_write_observed
                    && source.transitive_write_observed) {
                    destination.transitive_write_observed = true;
                    destination.write_witness_function_index =
                        source.write_witness_function_index;
                    regional_changed = true;
                }
            }
            if ((!read_before && target.transitive_read_observed)
                || (!write_before && target.transitive_write_observed)
                || (!unknown_before && target.may_reach_unresolved_call)
                || regional_changed) {
                if (!in_queue[caller]) {
                    pending.enqueue(caller);
                    in_queue[caller] = 1;
                }
            }
        }
    }

    if (graph.limited) {
        for (auto& fn : report.functions) fn.may_reach_unresolved_call = true;
    }

    for (auto& summary : report.functions) {
        if (summary.transitive_read_observed || summary.transitive_write_observed)
            summary.evidence_level = QStringLiteral("inferred");
    }
    for (const auto& summary : report.functions) {
        if (summary.analysis_partial || summary.report_truncated
            || summary.regional_effects_partial)
            report.limited = true;
    }

    // Complete means the bounded REPORT was not cut short, never that
    // a function is pure, free of effects, or correctly decompiled.
    report.complete = !report.limited && graph.complete
        && report.analyzed_bodies == static_cast<quint32>(count);
    if (report.limited)
        report.warnings << QStringLiteral(
            "At least one body or evidence list is partial; unreported "
            "effects must not be assumed absent.");
    return report;
}

} // namespace reworkbench
