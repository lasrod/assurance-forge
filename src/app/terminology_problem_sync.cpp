#include "app/terminology_problem_sync.h"

#include "core/problems/problem_item.h"
#include "core/problems/problem_utils.h"
#include "core/terminology_package_service.h"
#include "core/terminology_scope_service.h"
#include "parser/model_utils.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_model.h"
#include "ui/i18n/localization.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace app {
namespace {

std::string TerminologyAmbiguityProblemId(const core::TermOccurrence& occurrence) {
    return "terminology-ambiguity:" + occurrence.element_id + ":" + occurrence.text + ":" +
           std::to_string(occurrence.start_offset) + ":" + std::to_string(occurrence.end_offset);
}

std::string TerminologyUndefinedProblemId(const core::TermOccurrence& occurrence) {
    return "terminology-undefined:" + occurrence.element_id + ":" + occurrence.text + ":" +
           std::to_string(occurrence.start_offset) + ":" + std::to_string(occurrence.end_offset);
}

core::TerminologyPackageRef TerminologyPackageRefFor(const sacm::TerminologyPackage& package) {
    return core::TerminologyPackageRef{package.id, package.gid};
}

std::string RefValue(const core::TerminologyPackageRef& ref) {
    return ref.id.empty() ? ref.gid : ref.id;
}

std::string RefValue(const core::TerminologyTermRef& ref) {
    return ref.id.empty() ? ref.gid : ref.id;
}

const char* TermIssueKindCode(core::TerminologyTermIssueKind kind) {
    switch (kind) {
    case core::TerminologyTermIssueKind::MissingValue:
        return "missing-value";
    case core::TerminologyTermIssueKind::DuplicateDefinition:
        return "duplicate-definition";
    case core::TerminologyTermIssueKind::MissingDescription:
        return "missing-description";
    case core::TerminologyTermIssueKind::MissingCategory:
        return "missing-category";
    case core::TerminologyTermIssueKind::MissingExternalReference:
        return "missing-external-reference";
    }
    return "unknown";
}

const char* TermIssueProblemType(core::TerminologyTermIssueKind kind) {
    switch (kind) {
    case core::TerminologyTermIssueKind::MissingValue:
        return "TerminologyTermMissingValue";
    case core::TerminologyTermIssueKind::DuplicateDefinition:
        return "TerminologyTermDuplicateDefinition";
    case core::TerminologyTermIssueKind::MissingDescription:
        return "TerminologyTermMissingDescription";
    case core::TerminologyTermIssueKind::MissingCategory:
        return "TerminologyTermMissingCategory";
    case core::TerminologyTermIssueKind::MissingExternalReference:
        return "TerminologyTermMissingExternalReference";
    }
    return "TerminologyTermIssue";
}

const char* TermIssueQuickFixLabel(core::TerminologyTermIssueKind kind) {
    switch (kind) {
    case core::TerminologyTermIssueKind::DuplicateDefinition:
        return "Open duplicates";
    case core::TerminologyTermIssueKind::MissingValue:
    case core::TerminologyTermIssueKind::MissingDescription:
    case core::TerminologyTermIssueKind::MissingCategory:
    case core::TerminologyTermIssueKind::MissingExternalReference:
        return "Edit term";
    }
    return "";
}

std::string TerminologyTermProblemId(const core::TerminologyPackageRef& package_ref,
                                     const core::TerminologyTermIssue& issue) {
    return "terminology-term:" + RefValue(package_ref) + ":" + RefValue(issue.term_ref) + ":" +
           TermIssueKindCode(issue.kind);
}

std::string EncodeTerminologyTermQuickFixPayload(const core::TerminologyPackageRef& package_ref,
                                                 const core::TerminologyTermRef& term_ref,
                                                 const std::string& term_value) {
    const nlohmann::json payload{
        {"packageId", package_ref.id},
        {"packageGid", package_ref.gid},
        {"termId", term_ref.id},
        {"termGid", term_ref.gid},
        {"termValue", term_value},
    };
    return payload.dump();
}

std::string TerminologyContextReferenceProblemId(const core::TerminologyContextReferenceIssue& issue) {
    if (!issue.artifact_reference_id.empty())
        return "terminology-context-reference:" + issue.artifact_reference_id + ":" + issue.target_ref;
    if (!issue.asserted_context_id.empty())
        return "terminology-context-reference:" + issue.asserted_context_id + ":" + issue.target_ref;
    return "terminology-context-reference:" + issue.referenced_artifact + ":" + issue.target_ref;
}

core::ProblemSeverity ProblemSeverityFor(core::TerminologyTermIssueSeverity severity) {
    switch (severity) {
    case core::TerminologyTermIssueSeverity::Error:
        return core::ProblemSeverity::Error;
    case core::TerminologyTermIssueSeverity::Warning:
        return core::ProblemSeverity::Warning;
    case core::TerminologyTermIssueSeverity::Info:
        return core::ProblemSeverity::Info;
    }
    return core::ProblemSeverity::Info;
}

std::vector<core::ProblemItem> BuildTerminologyTermProblems(const sacm::TerminologyPackage& terminology_package) {
    std::vector<core::ProblemItem> problems;
    const core::TerminologyPackageRef package_ref = TerminologyPackageRefFor(terminology_package);
    for (const core::TerminologyTermIssue& issue : core::ValidateTerminologyTerms(terminology_package)) {
        const sacm::Term* term = core::FindTerminologyTerm(terminology_package, issue.term_ref);
        const std::string term_value = term ? term->value : std::string{};
        core::ProblemItem problem;
        problem.id = TerminologyTermProblemId(package_ref, issue);
        problem.severity = ProblemSeverityFor(issue.severity);
        problem.source = core::ProblemSource::ModelValidation;
        problem.element_id = RefValue(issue.term_ref);
        problem.type = TermIssueProblemType(issue.kind);
        problem.message = issue.message;
        problem.quick_fix_label = TermIssueQuickFixLabel(issue.kind);
        problem.quick_fix_payload = EncodeTerminologyTermQuickFixPayload(package_ref, issue.term_ref, term_value);
        problems.push_back(std::move(problem));
    }
    return problems;
}

core::ProblemItem BuildTerminologyContextReferenceProblem(const core::TerminologyContextReferenceIssue& issue) {
    core::ProblemItem problem;
    problem.id = TerminologyContextReferenceProblemId(issue);
    problem.severity = ProblemSeverityFor(issue.severity);
    problem.source = core::ProblemSource::ModelValidation;
    problem.element_id = issue.artifact_reference_id.empty() ? issue.target_ref : issue.artifact_reference_id;
    problem.type = "TerminologyBrokenContextReference";
    problem.message = issue.message;
    return problem;
}

std::optional<core::ProblemItem> BuildTerminologyOccurrenceProblem(const core::TermOccurrence& occurrence,
                                                                   bool ignored) {
    if (ignored)
        return std::nullopt;

    if (occurrence.resolution.status == core::TermResolutionStatus::Ambiguous) {
        core::ProblemItem problem;
        problem.id = TerminologyAmbiguityProblemId(occurrence);
        problem.severity = core::ProblemSeverity::Warning;
        problem.source = core::ProblemSource::ModelValidation;
        problem.element_id = occurrence.element_id;
        problem.type = "TerminologyAmbiguity";
        // Translate at sync time. Terminology problems are re-synced when the
        // model or terminology changes, so this picks up the current UI language.
        problem.message = ui::i18n::trf("{0} has {1} visible meanings. Choose the intended terminology entry.",
                                        occurrence.text,
                                        occurrence.resolution.candidates.size());
        problem.quick_fix_label = "Open glossary";
        problem.quick_fix_payload = occurrence.text;
        return problem;
    }

    if (occurrence.kind == core::TermOccurrenceKind::UndefinedAcronym && occurrence.resolution.important_undefined) {
        core::ProblemItem problem;
        problem.id = TerminologyUndefinedProblemId(occurrence);
        problem.severity = core::ProblemSeverity::Warning;
        problem.source = core::ProblemSource::ModelValidation;
        problem.element_id = occurrence.element_id;
        problem.type = "TerminologyUndefinedAcronym";
        problem.message = ui::i18n::trf("{0} looks like an undefined terminology entry.", occurrence.text);
        problem.quick_fix_label = "Define term";
        problem.quick_fix_payload = occurrence.text;
        return problem;
    }

    return std::nullopt;
}

} // namespace

void SyncTerminologyProblems(core::ProblemsManager& problems_manager,
                             const parser::AssuranceCase* model,
                             const sacm::AssuranceCasePackage* package,
                             const TerminologySuggestionIgnoredFn& is_suggestion_ignored) {
    constexpr const char* kTerminologyTermProblemPrefix = "terminology-term:";
    constexpr const char* kTerminologyAmbiguityProblemPrefix = "terminology-ambiguity:";
    constexpr const char* kTerminologyUndefinedProblemPrefix = "terminology-undefined:";
    constexpr const char* kTerminologyContextReferenceProblemPrefix = "terminology-context-reference:";
    core::ClearProblemsByIdPrefix(problems_manager, kTerminologyTermProblemPrefix);
    core::ClearProblemsByIdPrefix(problems_manager, kTerminologyAmbiguityProblemPrefix);
    core::ClearProblemsByIdPrefix(problems_manager, kTerminologyUndefinedProblemPrefix);
    core::ClearProblemsByIdPrefix(problems_manager, kTerminologyContextReferenceProblemPrefix);

    if (!model || !package)
        return;

    auto add_term_problems = [&](const sacm::TerminologyPackage& terminology_package) {
        for (core::ProblemItem& problem : BuildTerminologyTermProblems(terminology_package)) {
            problems_manager.AddOrUpdateProblem(problem);
        }
    };

    for (const sacm::TerminologyPackage& terminology_package : package->terminologyPackages)
        add_term_problems(terminology_package);
    for (const sacm::ArgumentPackage& argument_package : package->argumentPackages) {
        for (const sacm::TerminologyPackage& terminology_package : argument_package.terminologyPackages)
            add_term_problems(terminology_package);
    }

    for (const core::TerminologyContextReferenceIssue& issue : core::ValidateTerminologyContextReferences(*package)) {
        core::ProblemItem problem = BuildTerminologyContextReferenceProblem(issue);
        problems_manager.AddOrUpdateProblem(problem);
    }

    core::TerminologyService terminology_service(*package);

    for (const parser::SacmElement& element : model->elements) {
        if (parser::IsRelationshipElement(element))
            continue;
        const std::string text = parser::ElementTerminologyText(element);
        if (text.empty())
            continue;

        std::vector<core::TermOccurrence> occurrences = terminology_service.DetectTermsInText(element.id, text);
        for (const core::TermOccurrence& occurrence : occurrences) {
            const bool ignored = is_suggestion_ignored && is_suggestion_ignored(occurrence.element_id, occurrence.text);
            std::optional<core::ProblemItem> problem = BuildTerminologyOccurrenceProblem(occurrence, ignored);
            if (problem.has_value())
                problems_manager.AddOrUpdateProblem(*problem);
        }
    }
}

} // namespace app
