#include "reworkbench/structuring_plan.hpp"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace reworkbench {
namespace {

constexpr qsizetype max_blocks = 384;
constexpr qsizetype max_proposals = 512;

bool valid(qsizetype id, qsizetype count) {
    return id >= 0 && id < count;
}

QVector<qsizetype> unique_sorted(const QVector<qsizetype>& source) {
    QVector<qsizetype> result;
    QSet<qsizetype> seen;
    for (qsizetype id : source) {
        if (seen.contains(id)) continue;
        seen.insert(id);
        result << id;
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool has_edge(const HirObservation& hir, qsizetype from, qsizetype to) {
    return valid(from, hir.blocks.size()) && valid(to, hir.blocks.size())
        && hir.blocks[from].successors.contains(to);
}

void block(RegionProposal& proposal, const QString& why) {
    if (!proposal.blockers.contains(why)) proposal.blockers << why;
}

bool intersects_irreducible(const QSet<qsizetype>& members,
                            const QSet<qsizetype>& irreducible) {
    for (qsizetype member : members)
        if (irreducible.contains(member)) return true;
    return false;
}

void finalize(RegionProposal& proposal, bool base_eligible) {
    proposal.structurally_eligible = base_eligible && proposal.blockers.isEmpty();
    proposal.confidence = proposal.structurally_eligible
        ? QStringLiteral("candidate_in_observed_cfg")
        : QStringLiteral("blocked_requires_raw_cfg");
    // No structural observation here is proof that an AST transformation
    // preserves control/data/memory/exception semantics.
    proposal.rewrite_authorized = false;
}

} // namespace

HirStructuringPlan plan_hir_structuring(
    const HirObservation& hir, const ControlRegionObservation& control,
    std::stop_token stop) {
    HirStructuringPlan report;
    report.warnings << QStringLiteral(
        "Structuring proposals only. No if/while AST is emitted and "
        "no machine-level branch, PHI, memory operation or call is rewritten.");
    report.ir_structural_checks_passed =
        hir.ir_audit.structural_checks_passed;
    // An internally consistent SSA graph cannot establish preservation
    // of machine semantics, memory order, PHI edge values or exceptions.
    if (!report.ir_structural_checks_passed)
        report.unproved_rewrite_obligations
            << QStringLiteral("machine_ir_ssa_static_invariants");
    report.unproved_rewrite_obligations
        << QStringLiteral("instruction_semantics_equivalence")
        << QStringLiteral("phi_edge_lowering_preserves_values")
        << QStringLiteral("memory_and_opaque_effect_order_preserved")
        << QStringLiteral("exceptions_indirect_edges_and_tail_calls_modeled")
        << QStringLiteral("candidate_region_global_nonoverlap_selection")
        << QStringLiteral("differential_regression_validation");
    report.warnings << QStringLiteral(
        "Even structurally eligible regions need independent semantic "
        "validation before any source-language emitter may use them.");

    const qsizetype count = hir.blocks.size();
    if (count == 0 || count > max_blocks) {
        report.limited = true;
        report.warnings << QStringLiteral(
            "HIR has no blocks or exceeds the structuring-plan budget.");
        return report;
    }

    // Preserve the raw CFG as a complete, independently useful fallback.
    // The plan can be discarded without changing the original HIR.
    QVector<quint32> coverage(count, 0);
    report.fallback_blocks.reserve(count);
    for (qsizetype id = 0; id < count; ++id) {
        check_cancelled(stop);
        const auto& block = hir.blocks[id];
        RawBlockFallback fallback;
        fallback.block_index = id;
        fallback.entry = block.entry_address;
        fallback.terminator = block.terminator;
        fallback.successors = unique_sorted(block.successors);
        fallback.condition_value_id = block.condition_value;
        fallback.taken_address = block.taken_address;
        fallback.not_taken_address = block.not_taken_address;
        fallback.reachable = block.reachable;
        fallback.unknown_successor = block.target_resolution_incomplete
            || block.terminator == QStringLiteral("opaque_exit")
            || (block.terminator == QStringLiteral("conditional")
                && (!block.resolved_taken_block
                    || !block.resolved_not_taken_block));
        for (qsizetype successor : fallback.successors)
            if (!valid(successor, count)) fallback.unknown_successor = true;
        if (fallback.unknown_successor)
            fallback.fallback_reason = QStringLiteral("unresolved_control_target");
        else if (!fallback.reachable)
            fallback.fallback_reason = QStringLiteral("unreachable_in_observed_cfg");
        else
            fallback.fallback_reason = QStringLiteral("original_cfg_always_available");
        report.fallback_blocks << std::move(fallback);
    }
    report.all_reachable_blocks_have_fallback = true;
    for (qsizetype id = 0; id < count; ++id) {
        if (!hir.blocks[id].reachable) continue;
        if (report.fallback_blocks[id].block_index != id)
            report.all_reachable_blocks_have_fallback = false;
    }

    const bool structure_reliable = report.ir_structural_checks_passed
        && !hir.partial && !hir.limited
        && hir.report_complete && control.cfg_closed
        && !control.partial && !control.limited
        && control.dominators_converged;
    const bool joins_reliable = structure_reliable
        && control.post_dominators_reliable;
    QSet<qsizetype> irreducible_members;
    for (const auto& component : control.irreducible_components)
        for (qsizetype member : component.member_blocks)
            if (valid(member, count)) irreducible_members.insert(member);

    const auto register_proposal = [&](RegionProposal proposal) {
        // Deliberately counts candidates but does not remove a single raw
        // fallback block. Overlapping proposals may be mutually exclusive.
        if (report.proposals.size() >= max_proposals) {
            report.limited = true;
            return;
        }
        proposal.member_blocks = unique_sorted(proposal.member_blocks);
        for (qsizetype member : proposal.member_blocks) {
            if (!valid(member, count) || !hir.blocks[member].reachable) {
                block(proposal, QStringLiteral("invalid_or_unreachable_member"));
                continue;
            }
        }
        if (!proposal.blockers.isEmpty())
            finalize(proposal, false);
        if (proposal.structurally_eligible) ++report.eligible_proposals;
        else ++report.blocked_proposals;
        for (qsizetype member : proposal.member_blocks) {
            if (valid(member, count) && hir.blocks[member].reachable)
                ++coverage[member];
        }
        report.proposals << std::move(proposal);
    };

    for (const auto& candidate : control.natural_loops) {
        check_cancelled(stop);
        RegionProposal plan;
        plan.kind = QStringLiteral("natural_loop_candidate");
        plan.header = candidate.header_block;
        plan.member_blocks = candidate.member_blocks;
        QSet<qsizetype> members;
        for (qsizetype node : candidate.member_blocks) members.insert(node);
        if (!structure_reliable)
            block(plan, QStringLiteral("incomplete_or_unclosed_cfg"));
        if (!candidate.single_entry_proven_in_observed_cfg)
            block(plan, QStringLiteral("single_entry_not_proven"));
        if (!valid(candidate.header_block, count)
            || !valid(candidate.latch_block, count)
            || !members.contains(candidate.header_block)
            || !members.contains(candidate.latch_block))
            block(plan, QStringLiteral("invalid_loop_endpoints"));
        if (intersects_irreducible(members, irreducible_members))
            block(plan, QStringLiteral("irreducible_flow_overlap"));
        if (!has_edge(hir, candidate.latch_block, candidate.header_block))
            block(plan, QStringLiteral("missing_observed_backedge"));

        if (valid(candidate.header_block, count)) {
            const auto& header = hir.blocks[candidate.header_block];
            if (header.terminator == QStringLiteral("conditional")
                && header.resolved_taken_block
                && header.resolved_not_taken_block) {
                const qsizetype taken = *header.resolved_taken_block;
                const qsizetype other = *header.resolved_not_taken_block;
                const bool take_inside = members.contains(taken);
                const bool other_inside = members.contains(other);
                if (take_inside != other_inside) {
                    plan.kind = QStringLiteral("while_header_candidate");
                    plan.exit_block = take_inside ? other : taken;
                } else {
                    block(plan, QStringLiteral("no_unique_header_exit"));
                }
            } else {
                block(plan, QStringLiteral("header_not_resolved_conditional"));
            }
        }

        finalize(plan, structure_reliable
            && candidate.single_entry_proven_in_observed_cfg);
        register_proposal(std::move(plan));
    }

    for (const auto& conditional : control.conditional_joins) {
        check_cancelled(stop);
        RegionProposal plan;
        plan.kind = QStringLiteral("if_else_diamond_candidate");
        plan.header = conditional.header_block;
        plan.join_block = conditional.join_block;
        plan.member_blocks = {
            conditional.header_block, conditional.taken_block,
            conditional.not_taken_block, conditional.join_block
        };
        if (!report.ir_structural_checks_passed)
            block(plan, QStringLiteral("ssa_static_invariants_unproven"));
        if (!joins_reliable)
            block(plan, QStringLiteral("post_dominance_not_reliable"));
        const bool valid_ids = valid(conditional.header_block, count)
            && valid(conditional.taken_block, count)
            && valid(conditional.not_taken_block, count)
            && valid(conditional.join_block, count);
        if (!valid_ids)
            block(plan, QStringLiteral("invalid_conditional_block"));
        if (valid_ids) {
            const auto& head = hir.blocks[conditional.header_block];
            if (head.terminator != QStringLiteral("conditional")
                || !has_edge(hir, conditional.header_block, conditional.taken_block)
                || !has_edge(hir, conditional.header_block, conditional.not_taken_block))
                block(plan, QStringLiteral("branch_edges_not_proven"));
            if (conditional.taken_block == conditional.not_taken_block
                || conditional.join_block == conditional.header_block)
                block(plan, QStringLiteral("degenerate_conditional"));
            // Conservative v0: only a true 2-arm diamond, where both arms
            // are distinct single blocks that flow straight into the join.
            const auto& taken = hir.blocks[conditional.taken_block];
            const auto& otherwise = hir.blocks[conditional.not_taken_block];
            if (conditional.taken_block == conditional.join_block
                || conditional.not_taken_block == conditional.join_block
                || taken.successors.size() != 1
                || otherwise.successors.size() != 1
                || !has_edge(hir, conditional.taken_block, conditional.join_block)
                || !has_edge(hir, conditional.not_taken_block, conditional.join_block))
                block(plan, QStringLiteral("complex_non_diamond_region"));
            const auto& join = hir.blocks[conditional.join_block];
            if (!join.predecessors.contains(conditional.taken_block)
                || !join.predecessors.contains(conditional.not_taken_block))
                block(plan, QStringLiteral("join_predecessors_not_verified"));
        }
        QSet<qsizetype> members;
        for (qsizetype member : plan.member_blocks) members.insert(member);
        if (intersects_irreducible(members, irreducible_members))
            block(plan, QStringLiteral("irreducible_flow_overlap"));
        finalize(plan, joins_reliable);
        register_proposal(std::move(plan));
    }

    for (qsizetype id = 0; id < count; ++id) {
        check_cancelled(stop);
        report.fallback_blocks[id].proposal_count = coverage[id];
        report.fallback_blocks[id].covered_by_proposal = coverage[id] != 0;
        if (coverage[id] > 1) ++report.overlapping_candidate_blocks;
        if (hir.blocks[id].reachable && coverage[id] == 0)
            report.uncovered_reachable_blocks << id;
    }
    if (!irreducible_members.isEmpty())
        report.warnings << QStringLiteral(
            "Irreducible multi-entry regions retain the original CFG fallback.");
    if (report.overlapping_candidate_blocks)
        report.warnings << QStringLiteral(
            "Some region candidates overlap; no mutually consistent "
            "AST-region selection has been performed.");
    if (report.limited)
        report.warnings << QStringLiteral(
            "Plan reached its bounded proposal/block ceiling.");
    report.partial = hir.partial || control.partial || report.limited
        || !report.all_reachable_blocks_have_fallback
        || report.blocked_proposals != 0;
    return report;
}

} // namespace reworkbench
