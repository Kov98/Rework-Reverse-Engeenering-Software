#include "reworkbench/control_regions.hpp"

#include <QQueue>
#include <QSet>

#include <algorithm>
#include <functional>
#include <utility>

namespace reworkbench {
namespace {

constexpr qsizetype max_blocks = 384;
constexpr qsizetype max_loops = 512;
constexpr qsizetype max_components = 384;
constexpr int max_fixed_point_passes = 800;
using Sets = QVector<QSet<qsizetype>>;
using Edges = QVector<QVector<qsizetype>>;

bool valid(qsizetype block, qsizetype count) {
    return block >= 0 && block < count;
}

QVector<qsizetype> sorted(const QSet<qsizetype>& members) {
    QVector<qsizetype> result;
    result.reserve(members.size());
    for (qsizetype member : members) result << member;
    std::sort(result.begin(), result.end());
    return result;
}

QSet<qsizetype> reachable_from(qsizetype start, const Edges& edges,
                               std::stop_token stop) {
    QSet<qsizetype> visited;
    if (!valid(start, edges.size())) return visited;
    QQueue<qsizetype> pending;
    pending.enqueue(start);
    visited.insert(start);
    while (!pending.isEmpty()) {
        check_cancelled(stop);
        const qsizetype node = pending.dequeue();
        for (const qsizetype next : edges[node]) {
            if (visited.contains(next)) continue;
            visited.insert(next);
            pending.enqueue(next);
        }
    }
    return visited;
}

bool fixed_point(Sets& facts, const Edges& inputs,
                 const QSet<qsizetype>& domain,
                 const QSet<qsizetype>& roots,
                 std::stop_token stop) {
    for (qsizetype index : domain)
        facts[index] = roots.contains(index) ? QSet<qsizetype>{index} : domain;
    for (int pass = 0; pass < max_fixed_point_passes; ++pass) {
        check_cancelled(stop);
        bool changed = false;
        const auto order = sorted(domain);
        for (qsizetype i : order) {
            if (roots.contains(i)) continue;
            bool first = true;
            QSet<qsizetype> next;
            for (qsizetype input : inputs[i]) {
                if (!domain.contains(input)) continue;
                if (first) {
                    next = facts[input];
                    first = false;
                } else next.intersect(facts[input]);
            }
            if (first) next.clear();
            next.insert(i);
            if (next == facts[i]) continue;
            facts[i] = std::move(next);
            changed = true;
        }
        if (!changed) return true;
    }
    return false;
}

std::optional<qsizetype> immediate(const Sets& facts, qsizetype node) {
    if (!valid(node, facts.size()) || facts[node].isEmpty())
        return std::nullopt;
    QSet<qsizetype> candidates = facts[node];
    candidates.remove(node);
    if (candidates.isEmpty()) return std::nullopt;
    qsizetype best = -1;
    qsizetype best_depth = -1;
    bool tied = false;
    for (qsizetype candidate : candidates) {
        const qsizetype depth = facts[candidate].size();
        if (depth > best_depth) {
            best = candidate;
            best_depth = depth;
            tied = false;
        } else if (depth == best_depth) tied = true;
    }
    if (tied) return std::nullopt;
    return best;
}

struct Scc {
    QVector<qsizetype> members;
    QSet<qsizetype> entries;
};

// Kosaraju over discovered reachable CFG, bounded by max_blocks.
// This identifies multiple-entry strongly-connected regions that cannot
// safely be treated as single-entry natural loops.
QVector<Scc> strongly_connected(const Edges& forward, const Edges& reverse,
                                const QSet<qsizetype>& domain,
                                std::stop_token stop) {
    QSet<qsizetype> seen;
    QVector<qsizetype> order;
    std::function<void(qsizetype)> visit = [&](qsizetype node) {
        check_cancelled(stop);
        seen.insert(node);
        for (qsizetype next : forward[node])
            if (domain.contains(next) && !seen.contains(next)) visit(next);
        order << node;
    };
    for (qsizetype node : sorted(domain))
        if (!seen.contains(node)) visit(node);

    seen.clear();
    QVector<Scc> result;
    std::function<void(qsizetype, Scc&)> collect =
        [&](qsizetype node, Scc& group) {
            check_cancelled(stop);
            seen.insert(node);
            group.members << node;
            for (qsizetype previous : reverse[node])
                if (domain.contains(previous) && !seen.contains(previous))
                    collect(previous, group);
        };
    for (qsizetype i = order.size(); i > 0; --i) {
        const qsizetype node = order[i - 1];
        if (seen.contains(node)) continue;
        Scc group;
        collect(node, group);
        QSet<qsizetype> inside;
        for (qsizetype member : group.members) inside.insert(member);
        for (qsizetype member : group.members)
            for (qsizetype predecessor : reverse[member])
                if (domain.contains(predecessor) && !inside.contains(predecessor))
                    group.entries.insert(member);
        std::sort(group.members.begin(), group.members.end());
        result << std::move(group);
    }
    return result;
}

} // namespace

ControlRegionObservation observe_hir_control_regions(
    const HirObservation& hir, std::stop_token stop) {
    ControlRegionObservation report;
    report.warnings << QStringLiteral(
        "All dominance/loop/join facts describe the observed HIR machine CFG, "
        "not recovered source-level control structures.");
    const qsizetype count = hir.blocks.size();
    if (count == 0 || count > max_blocks
        || !valid(hir.entry_block, count) || hir.limited) {
        report.limited = true;
        report.warnings << QStringLiteral(
            "Missing or oversized HIR CFG; no structural claims emitted.");
        return report;
    }

    Edges forward(count), reverse(count);
    bool malformed = false;
    bool unknown_exit = false;
    for (qsizetype i = 0; i < count; ++i) {
        check_cancelled(stop);
        const HirBlock& block = hir.blocks[i];
        if (block.index != i) malformed = true;
        QSet<qsizetype> unique;
        for (qsizetype successor : block.successors) {
            if (!valid(successor, count)) {
                malformed = true;
                unknown_exit = true;
                continue;
            }
            if (unique.contains(successor)) continue;
            unique.insert(successor);
            forward[i] << successor;
            reverse[successor] << i;
        }
        if (block.target_resolution_incomplete
            || block.terminator == QStringLiteral("opaque_exit"))
            unknown_exit = true;
        if (block.terminator == QStringLiteral("conditional")) {
            if (!block.resolved_taken_block || !block.resolved_not_taken_block
                || *block.resolved_taken_block == *block.resolved_not_taken_block
                || forward[i].size() != 2)
                unknown_exit = true;
        }
        if (forward[i].isEmpty()
            && block.terminator != QStringLiteral("machine_state_return"))
            unknown_exit = true;
    }

    const QSet<qsizetype> reached =
        reachable_from(hir.entry_block, forward, stop);
    for (qsizetype i = 0; i < count; ++i) {
        if (hir.blocks[i].reachable != reached.contains(i))
            malformed = true;
        for (qsizetype predecessor : hir.blocks[i].predecessors)
            if (!valid(predecessor, count)
                || !forward[predecessor].contains(i))
                malformed = true;
        for (qsizetype predecessor : reverse[i])
            if (!hir.blocks[i].predecessors.contains(predecessor))
                malformed = true;
    }

    QSet<qsizetype> exits;
    for (qsizetype i : reached) {
        if (forward[i].isEmpty()
            && hir.blocks[i].terminator ==
                QStringLiteral("machine_state_return")) exits.insert(i);
    }

    report.has_unknown_exits = unknown_exit
        || hir.partial || hir.has_opaque_barriers || malformed;
    report.cfg_closed = !report.has_unknown_exits;
    report.partial = !report.cfg_closed;
    if (malformed)
        report.warnings << QStringLiteral(
            "CFG indices/reachability/predecessor edges disagree; "
            "structural patterns are diagnostic only.");
    if (report.has_unknown_exits)
        report.warnings << QStringLiteral(
            "Unresolved control-flow or opaque machine effects exist. "
            "Post-dominance cannot be certified for this function.");

    Sets dominators(count), post_dominators(count);
    report.dominators_converged = fixed_point(
        dominators, reverse, reached, QSet<qsizetype>{hir.entry_block}, stop);
    if (!report.dominators_converged) {
        report.limited = true;
        report.partial = true;
        report.warnings << QStringLiteral(
            "Dominator fixed point did not converge under budget.");
    }

    QSet<qsizetype> reaches_exit;
    for (qsizetype exit : exits)
        reaches_exit.unite(reachable_from(exit, reverse, stop));

    bool all_reach_exit = true;
    for (qsizetype node : reached)
        if (!reaches_exit.contains(node)) all_reach_exit = false;
    if (exits.isEmpty() || !all_reach_exit)
        report.warnings << QStringLiteral(
            "Some paths have no known return (including infinite loops), "
            "so post-dominators and conditional joins remain uncertain.");

    const bool can_postdom = report.cfg_closed && all_reach_exit
        && !exits.isEmpty() && report.dominators_converged;
    if (can_postdom) {
        // A virtual common exit is represented by treating each real Return
        // as a fixed root. No invented edge is inserted into the HIR.
        report.post_dominators_converged = fixed_point(
            post_dominators, forward, reached, exits, stop);
        report.post_dominators_reliable = report.post_dominators_converged;
        if (!report.post_dominators_converged) {
            report.limited = true;
            report.partial = true;
        }
    }

    report.blocks.reserve(count);
    for (qsizetype i = 0; i < count; ++i) {
        check_cancelled(stop);
        ControlBlockFacts facts;
        facts.block_index = i;
        facts.reached_from_entry = reached.contains(i);
        facts.can_reach_known_return = reaches_exit.contains(i);
        if (report.dominators_converged && reached.contains(i)) {
            facts.dominators = sorted(dominators[i]);
            facts.immediate_dominator = immediate(dominators, i);
        }
        if (report.post_dominators_reliable && reached.contains(i)) {
            facts.post_dominators = sorted(post_dominators[i]);
            facts.immediate_post_dominator = immediate(post_dominators, i);
        }
        report.blocks << std::move(facts);
    }

    const auto components = strongly_connected(
        forward, reverse, reached, stop);
    QSet<qsizetype> irreducible_blocks;
    for (const auto& component : components) {
        if (component.members.size() <= 1
            || component.entries.size() <= 1) continue;
        if (report.irreducible_components.size() >= max_components) {
            report.limited = true;
            report.partial = true;
            break;
        }
        IrreducibleCfgComponent group;
        group.member_blocks = component.members;
        group.entry_blocks = sorted(component.entries);
        report.irreducible_components << std::move(group);
        for (qsizetype node : component.members)
            irreducible_blocks.insert(node);
    }
    if (!report.irreducible_components.isEmpty()) {
        report.warnings << QStringLiteral(
            "Multi-entry strongly-connected CFG region detected. "
            "Natural-loop/if transformations must retain a label/goto "
            "fallback unless independently proven equivalent.");
    }

    if (report.dominators_converged) {
        for (qsizetype tail : sorted(reached)) {
            check_cancelled(stop);
            for (qsizetype head : forward[tail]) {
                if (!reached.contains(head)
                    || !dominators[tail].contains(head))
                    continue;
                ++report.observed_backedges;
                if (report.natural_loops.size() >= max_loops) {
                    report.limited = true;
                    report.partial = true;
                    break;
                }

                // Build the smallest predecessor-closed region for a
                // head-dominating backedge. Do not move any blocks in HIR.
                QSet<qsizetype> members{head, tail};
                QVector<qsizetype> pending;
                if (tail != head) pending << tail;
                while (!pending.isEmpty()) {
                    check_cancelled(stop);
                    qsizetype current = pending.takeLast();
                    for (qsizetype predecessor : reverse[current]) {
                        if (!reached.contains(predecessor)
                            || members.contains(predecessor)) continue;
                        members.insert(predecessor);
                        if (predecessor != head) pending << predecessor;
                    }
                }

                bool single_entry = true;
                for (qsizetype member : members) {
                    if (!dominators[member].contains(head))
                        single_entry = false;
                    for (qsizetype predecessor : reverse[member]) {
                        if (reached.contains(predecessor)
                            && !members.contains(predecessor)
                            && member != head)
                            single_entry = false;
                    }
                }
                for (qsizetype member : members)
                    if (irreducible_blocks.contains(member))
                        single_entry = false;

                NaturalLoopCandidate candidate;
                candidate.header_block = head;
                candidate.latch_block = tail;
                candidate.member_blocks = sorted(members);
                candidate.single_entry_proven_in_observed_cfg =
                    single_entry && report.cfg_closed;
                if (report.cfg_closed && single_entry
                    && hir.blocks[head].terminator ==
                        QStringLiteral("conditional")) {
                    int inside = 0, outside = 0;
                    for (qsizetype successor : forward[head]) {
                        if (members.contains(successor)) ++inside;
                        else ++outside;
                    }
                    if (inside == 1 && outside == 1)
                        candidate.classification =
                            QStringLiteral("while_header_candidate");
                }
                report.natural_loops << std::move(candidate);
            }
            if (report.limited) break;
        }
    }

    if (report.post_dominators_reliable) {
        for (qsizetype header : sorted(reached)) {
            check_cancelled(stop);
            const HirBlock& raw = hir.blocks[header];
            if (raw.terminator != QStringLiteral("conditional")
                || !raw.resolved_taken_block
                || !raw.resolved_not_taken_block
                || irreducible_blocks.contains(header))
                continue;
            const qsizetype taken = *raw.resolved_taken_block;
            const qsizetype otherwise = *raw.resolved_not_taken_block;
            if (!valid(taken, count) || !valid(otherwise, count)
                || taken == otherwise || forward[header].size() != 2
                || !forward[header].contains(taken)
                || !forward[header].contains(otherwise))
                continue;
            const auto join = report.blocks[header].immediate_post_dominator;
            if (!join || *join == header
                || !post_dominators[taken].contains(*join)
                || !post_dominators[otherwise].contains(*join))
                continue;
            if (!dominators[taken].contains(header)
                || !dominators[otherwise].contains(header))
                continue;

            // A branch embedded in an observed loop can still be a nested
            // if, but skip it here until loop boundaries are modeled
            // precisely. The conservative fallback remains the original CFG.
            bool part_of_loop = false;
            for (const auto& loop : report.natural_loops)
                if (loop.member_blocks.contains(header)) {
                    part_of_loop = true;
                    break;
                }
            if (part_of_loop) continue;

            report.conditional_joins << ConditionalJoinCandidate{
                header, taken, otherwise, *join,
                QStringLiteral("conditional_join_candidate")
            };
        }
    }

    if (!report.cfg_closed || !report.dominators_converged
        || !report.post_dominators_reliable)
        report.warnings << QStringLiteral(
            "Unavailable or partial control-flow knowledge prevents "
            "certifying an AST transformation. Keep raw blocks/edges.");
    if (report.limited)
        report.warnings << QStringLiteral(
            "CFG region analysis exceeded limits; no incomplete findings "
            "may be interpreted as proof of absent loops or branches.");

    // The input HIR may be only structurally complete, and exception/tail
    // call semantics are outside this pass. source_control_verified stays
    // FALSE even when dominance reaches a mathematical fixed point.
    report.partial |= report.limited || !report.cfg_closed
        || !report.dominators_converged || !report.post_dominators_reliable;
    return report;
}

} // namespace reworkbench
