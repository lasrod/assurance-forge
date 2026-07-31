#include "app/structure_problem_sync.h"

#include "core/problems/argument_cycles.h"
#include "core/problems/gsn_wellformedness.h"
#include "core/problems/problem_utils.h"
#include "ui/i18n/localization.h"

#include <string>
#include <vector>

namespace app {
namespace {

constexpr const char* kStructureProblemPrefix = "structure:";

// "G1 → S1 → G2 → G1" — the loop written out, closing back on itself so the
// reader can see it is a loop rather than a path.
std::string DescribeCycle(const core::ArgumentCycle& cycle) {
    std::string path;
    for (const std::string& element_id : cycle.element_ids) {
        if (!path.empty())
            path += " → ";
        path += element_id;
    }
    if (cycle.element_ids.size() > 1) {
        path += " → ";
        path += cycle.element_ids.front();
    }
    return path;
}

void SyncCycleProblems(core::ProblemsManager& problems_manager, const parser::AssuranceCase& model) {
    for (const core::ArgumentCycle& cycle : core::FindSupportCycles(model)) {
        if (cycle.element_ids.empty())
            continue;

        core::ProblemItem problem;
        // Keyed on the cycle's own members, so the problem is stable across
        // rebuilds and disappears when the loop is broken.
        problem.id = kStructureProblemPrefix + std::string("cycle:") + DescribeCycle(cycle);
        // An argument that assumes its own conclusion establishes nothing, and
        // it looks complete while doing it. That is an error, not advice.
        problem.severity = core::ProblemSeverity::Error;
        problem.source = core::ProblemSource::ModelValidation;
        // Anchor on the first element so selecting the problem navigates into
        // the loop.
        problem.element_id = cycle.element_ids.front();
        problem.type = "CircularArgument";
        problem.guideline_id = "GSN3-CORE-014";
        problem.message =
            cycle.element_ids.size() == 1
                ? ui::i18n::trf("{0} is supported by itself, so it establishes nothing.",
                                cycle.element_ids.front())
                : ui::i18n::trf("Circular support: {0}. An argument that assumes its own conclusion "
                                "establishes nothing.",
                                DescribeCycle(cycle));
        problem.quick_fix_label = ui::i18n::tr("Show cycle");
        problem.quick_fix_payload = cycle.element_ids.front();
        problems_manager.AddOrUpdateProblem(problem);
    }
}

// What a reader is told about a GSN v3 rule violation. The sentence names the
// defect and why it matters, because "SupportedElementIsALeaf" tells a safety
// engineer nothing about their argument.
std::string DescribeFinding(const core::GsnFinding& finding) {
    switch (finding.rule) {
    case core::GsnRule::UnresolvedEndpoint:
        return ui::i18n::trf("Relationship {0} refers to {1}, which is not in this case. The relationship is "
                             "not drawn, so the argument shown is smaller than the one stored.",
                             finding.relationship_id,
                             finding.detail);
    case core::GsnRule::ChallengeTargetUnresolved:
        return ui::i18n::trf("Challenge {0} targets {1}, which is not in this case. The challenge is stored "
                             "but never shown against anything.",
                             finding.relationship_id,
                             finding.detail);
    case core::GsnRule::SupportedElementIsALeaf:
        return ui::i18n::trf("{0} is supported by relationship {1}, but only a Goal or a Strategy can be "
                             "supported in GSN.",
                             finding.element_id,
                             finding.relationship_id);
    case core::GsnRule::ContextualizedElementIsALeaf:
        return ui::i18n::trf("{0} is given context by relationship {1}, but only a Goal or a Strategy is "
                             "declared in context in GSN.",
                             finding.element_id,
                             finding.relationship_id);
    case core::GsnRule::StrategyUsedAsAssertion:
        return ui::i18n::trf("Strategy {0} is wired as an end of relationship {1}. A Strategy is the "
                             "reasoning of an inference, not one of its ends.",
                             finding.element_id,
                             finding.relationship_id);
    case core::GsnRule::EvidenceSourceIsNotASolution:
        return ui::i18n::trf("{0} discharges a goal through relationship {1}, but it is not a Solution. "
                             "Only a reference to evidence can discharge a goal.",
                             finding.element_id,
                             finding.relationship_id);
    case core::GsnRule::DuplicateNotationIdentifier:
        return ui::i18n::trf("{0} and {1} both use the GSN identifier {2}, so neither can be referred to "
                             "unambiguously.",
                             finding.element_id,
                             finding.related_id,
                             finding.detail);
    case core::GsnRule::UndevelopedElementHasSupport:
        return ui::i18n::trf("{0} is marked undeveloped but is supported through relationship {1}. Either "
                             "the decorator or the support is out of date.",
                             finding.element_id,
                             finding.relationship_id);
    }
    return std::string();
}

// A violated structural rule is an error: the argument is not valid GSN. A
// stale undeveloped decorator is a warning, because the argument is still
// well-formed — the diagram just no longer says what the author meant.
core::ProblemSeverity SeverityFor(core::GsnRule rule) {
    return rule == core::GsnRule::UndevelopedElementHasSupport ? core::ProblemSeverity::Warning
                                                               : core::ProblemSeverity::Error;
}

void SyncWellFormednessProblems(core::ProblemsManager& problems_manager, const parser::AssuranceCase& model) {
    for (const core::GsnFinding& finding : core::CheckGsnWellFormedness(model)) {
        core::ProblemItem problem;
        // Keyed on the finding's own content so it survives a rebuild and
        // disappears the moment the defect is corrected.
        problem.id = kStructureProblemPrefix + std::string("gsn:") + core::GsnRuleName(finding.rule) + ":" +
                     finding.element_id + ":" + finding.related_id + ":" + finding.relationship_id + ":" +
                     finding.detail;
        problem.severity = SeverityFor(finding.rule);
        problem.source = core::ProblemSource::ModelValidation;
        problem.element_id = finding.element_id;
        problem.type = core::GsnRuleName(finding.rule);
        // The normative requirement the diagnostic enforces, so a reader can
        // take it back to docs/gsn/gsn-v3-conformance-matrix.md and the standard
        // rather than having to trust the tool (GSN3-VAL-001).
        problem.guideline_id = core::GsnRequirementId(finding.rule);
        problem.message = DescribeFinding(finding);
        if (!finding.element_id.empty()) {
            problem.quick_fix_label = ui::i18n::tr("Show element");
            problem.quick_fix_payload = finding.element_id;
        }
        problems_manager.AddOrUpdateProblem(problem);
    }
}

} // namespace

void SyncStructureProblems(core::ProblemsManager& problems_manager, const parser::AssuranceCase* model) {
    core::ClearProblemsByIdPrefix(problems_manager, kStructureProblemPrefix);
    if (!model)
        return;

    SyncCycleProblems(problems_manager, *model);
    SyncWellFormednessProblems(problems_manager, *model);
}

} // namespace app
