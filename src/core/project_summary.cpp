#include "core/project_summary.h"

#include "core/registers/register_model.h"

#include <unordered_set>

namespace core {

std::size_t ProjectSummary::attention_count() const {
    // Must stay in step with the rows the overview renders under "Needs
    // Attention" — a header claiming no alerts above a list of them is worse
    // than either number alone.
    //
    // Problems already include review findings, so do not add open review items
    // again. Broken proposals are not represented in ProblemsManager.
    return warning_problems + error_problems + undeveloped + unlinked_evidence + broken_proposals;
}

ProjectSummary BuildProjectSummary(const AssuranceProject* project,
                                   const AssuranceCase* assurance_case,
                                   const AssuranceTree* assurance_tree,
                                   const std::vector<ProblemItem>& problems,
                                   const std::vector<reviews::ReviewItem>& review_items,
                                   const std::vector<reviews::ProposalValidityResult>& proposal_validities) {
    ProjectSummary summary;

    if (project) {
        for (const ProjectFileEntry& entry : project->files) {
            switch (entry.role) {
            case ProjectFileRole::SacmArgument:
                ++summary.argument_files;
                break;
            case ProjectFileRole::J3377CaeRegister:
            case ProjectFileRole::ConformanceSheet:
                ++summary.conformance_files;
                break;
            case ProjectFileRole::ExportedReport:
                ++summary.exported_reports;
                break;
            default:
                break;
            }
        }
    }

    if (assurance_tree) {
        summary.elements = assurance_tree->nodes.size();
        for (const std::unique_ptr<TreeNode>& node : assurance_tree->nodes) {
            if (!node)
                continue;
            switch (node->role) {
            case NodeRole::Claim:
                ++summary.claims;
                break;
            case NodeRole::Strategy:
                ++summary.strategies;
                break;
            case NodeRole::Solution:
                ++summary.evidence;
                break;
            default:
                break;
            }
            if (node->undeveloped)
                ++summary.undeveloped;
        }
    }

    if (assurance_case) {
        const std::vector<std::string> evidence_ids = registers::DeriveEvidenceIds(*assurance_case);
        const std::vector<registers::CseLink> links = registers::DeriveCseLinks(*assurance_case);
        std::unordered_set<std::string> linked_evidence;
        for (const registers::CseLink& link : links)
            linked_evidence.insert(link.evidence_id);
        for (const std::string& evidence_id : evidence_ids) {
            if (linked_evidence.count(evidence_id) == 0)
                ++summary.unlinked_evidence;
        }
        // The register projection is authoritative for evidence inventory and
        // includes unlinked evidence deliberately.
        summary.evidence = evidence_ids.size();
        if (summary.elements == 0)
            summary.elements = assurance_case->elements.size();
    }

    for (const reviews::ReviewItem& item : review_items) {
        if (item.status == reviews::ReviewItemStatus::Open) {
            ++summary.open_review_items;
        } else {
            ++summary.resolved_review_items;
        }
    }
    for (const reviews::ProposalValidityResult& validity : proposal_validities) {
        if (validity.validity == reviews::ProposalValidity::Valid) {
            ++summary.valid_proposals;
        } else {
            ++summary.broken_proposals;
        }
    }
    for (const ProblemItem& problem : problems) {
        if (problem.severity == ProblemSeverity::Error) {
            ++summary.error_problems;
        } else if (problem.severity == ProblemSeverity::Warning) {
            ++summary.warning_problems;
        }
    }

    return summary;
}

} // namespace core
