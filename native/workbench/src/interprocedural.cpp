#include "reworkbench/interprocedural.hpp"

#include <QHash>
#include <QQueue>
#include <QSet>
#include <utility>

namespace reworkbench {

InterproceduralObservation observe_interprocedural_calls(
    const AnalysisSnapshot& snapshot, std::stop_token stop) {
    InterproceduralObservation result;
    constexpr qsizetype max_functions = 4096;
    constexpr qsizetype max_calls = 32768;
    if (snapshot.functions.size() > max_functions) {
        result.limited = true;
        result.graph_status = QStringLiteral("function_limit");
        result.warnings << QStringLiteral(
            "Function count exceeds the graph ceiling; no misleading "
            "partial call graph was published.");
        return result;
    }

    const qsizetype count = snapshot.functions.size();
    QHash<QString, quint32> by_address;
    QSet<QString> ambiguous_entries;
    for (qsizetype i = 0; i < count; ++i) {
        check_cancelled(stop);
        const auto& function = snapshot.functions[i];
        // Duplicate entries are ambiguous; keep the first and mark graph
        // incomplete rather than arbitrarily claiming a unique target.
        const QString key = function.address.text();
        if (by_address.contains(key)) {
            ambiguous_entries.insert(key);
            result.limited = true;
            result.warnings << QStringLiteral(
                "Duplicate function entries; direct call target identity "
                "is ambiguous.");
        } else by_address.insert(key, static_cast<quint32>(i));
        result.functions << InterproceduralFunction{
            static_cast<quint32>(i), function.address, function.name, 0, 0,
            function.partial, false, function.partial, true, true
        };
    }

    QVector<QVector<quint32>> reverse_edges(count);
    for (qsizetype i = 0; i < count; ++i) {
        check_cancelled(stop);
        for (const auto& instruction : snapshot.functions[i].instructions) {
            if (instruction.flow != FlowKind::Call) continue;
            if (result.calls.size() >= max_calls) {
                result.limited = true;
                result.graph_status = QStringLiteral("call_limit");
                result.warnings << QStringLiteral(
                    "Call graph reached its edge ceiling; unresolved "
                    "reachability is conservatively unknown.");
                // On a truncated graph no function can be certified as
                // free from calls outside the published subset.
                for (auto& fn : result.functions)
                    fn.may_transitively_reach_unresolved = true;
                return result;
            }

            InterproceduralCallsite edge;
            edge.caller_index = static_cast<quint32>(i);
            edge.call_instruction = instruction.address;
            edge.direct_target = instruction.direct_target;
            auto& caller = result.functions[i];
            if (!edge.direct_target) {
                edge.classification = QStringLiteral("indirect");
                ++caller.unresolved_calls;
                caller.may_transitively_reach_unresolved = true;
            } else {
                const auto found = by_address.constFind(edge.direct_target->text());
                if (found == by_address.cend()
                    || ambiguous_entries.contains(edge.direct_target->text())) {
                    edge.classification = QStringLiteral("external_or_undiscovered");
                    ++caller.unresolved_calls;
                    caller.may_transitively_reach_unresolved = true;
                } else {
                    edge.classification = QStringLiteral("internal_direct");
                    edge.recovered_callee_index = *found;
                    ++caller.direct_internal_calls;
                    reverse_edges[*found] << static_cast<quint32>(i);
                    if (*found == i) caller.direct_self_recursion_observed = true;
                }
            }
            result.calls << std::move(edge);
        }
    }

    // Backward propagation along known direct edges: a caller can reach an
    // unresolved destination if any of its known callees can. This is a
    // MAY property, safe for cycles and bounded by the number of functions.
    QQueue<quint32> work;
    for (const auto& fn : result.functions)
        if (fn.may_transitively_reach_unresolved)
            work.enqueue(fn.function_index);

    while (!work.isEmpty()) {
        check_cancelled(stop);
        const auto callee = work.dequeue();
        for (quint32 caller : reverse_edges[callee]) {
            if (result.functions[caller].may_transitively_reach_unresolved)
                continue;
            result.functions[caller].may_transitively_reach_unresolved = true;
            work.enqueue(caller);
        }
    }

    // Being absent from the direct-call graph does NOT prove pure functions,
    // lack of side effects, absence of tail-calls, or complete discovery.
    result.graph_status = QStringLiteral("direct_calls_analyzed");
    result.complete = !result.limited
        && snapshot.state == QStringLiteral("ready");
    for (const auto& fn : result.functions) {
        if (fn.recovered_body_partial || fn.unresolved_calls != 0)
            result.complete = false;
    }
    if (result.limited) {
        for (auto& fn : result.functions)
            fn.may_transitively_reach_unresolved = true;
    }
    result.warnings << QStringLiteral(
        "Graph contains modeled CALL instructions only. Tail-calls, "
        "indirect targets, dynamic dispatch, thunks and undiscovered "
        "functions may be missing; all call effects remain unknown.");
    if (!result.complete)
        result.warnings << QStringLiteral(
            "The graph or snapshot is incomplete. Absence of a call "
            "does not imply absence of reachable code.");
    return result;
}

} // namespace reworkbench
