#include "app/app_runtime.h"

#include "app/actions/element_actions.h"
#include "app/actions/proposal_actions.h"
#include "app/actions/review_actions.h"
#include "app/areas/ai_debug_area.h"
#include "app/areas/argument_navigator_area.h"
#include "app/areas/feedback_dock_area.h"
#include "app/areas/inspector_area.h"
#include "app/areas/modal_host.h"
#include "app/areas/project_explorer_area.h"
#include "app/areas/review_panel_area.h"
#include "app/areas/workbench_area.h"
#include "app/app_runtime_state.h"
#include "app/frame/app_menu_bar.h"
#include "app/frame/app_shell.h"
#include "app/project_workflow.h"
#include "app/recent_projects.h"
#include "app/review_problem_sync.h"
#include "core/app_state.h"
#include "core/element_factory.h"
#include "core/problems/problem_attention.h"
#include "core/problems/problems_manager.h"
#include "core/project_service.h"
#include "core/reviews/review_proposal_manager.h"
#include "core/reviews/review_proposal_patch_service.h"
#include "core/terminology_package_service.h"
#include "core/terminology_scope_service.h"
#include "imgui.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/panels/sacm_viewer_panel.h"
#include "ui/register_views.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace app {
namespace {

const ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;

void CopyToBuffer(char* buffer, size_t buffer_size, const std::string& value) {
    if (!buffer || buffer_size == 0)
        return;
    size_t count = std::min(buffer_size - 1, value.size());
    std::memcpy(buffer, value.data(), count);
    buffer[count] = '\0';
}

std::string TrimWhitespace(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin)))
        ++begin;
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))))
        --end;
    return std::string(begin, end);
}

std::string NowUtcString() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    if (gmtime_s(&utc, &time) != 0)
        return "1970-01-01T00:00:00Z";
#else
    if (!gmtime_r(&time, &utc))
        return "1970-01-01T00:00:00Z";
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string GenerateReviewProposalId() {
    static unsigned long long counter = 0;
    auto ticks = std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream out;
    out << "proposal-" << std::hex << ticks << "-" << ++counter;
    return out.str();
}

std::string TruncateForProblemMessage(const std::string& value, size_t limit = 400) {
    if (value.size() <= limit)
        return value;
    return value.substr(0, limit) + "...";
}

bool IsReviewDerivedProblem(const core::ProblemItem& problem) {
    return problem.id.rfind("review-comment:", 0) == 0 || problem.id.rfind("guideline-review:", 0) == 0;
}

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin());
}

void ClearProblemsByIdPrefix(core::ProblemsManager& problems_manager, const std::string& prefix) {
    std::vector<std::string> problem_ids;
    for (const core::ProblemItem& problem : problems_manager.GetProblems()) {
        if (StartsWith(problem.id, prefix))
            problem_ids.push_back(problem.id);
    }
    for (const std::string& problem_id : problem_ids) {
        problems_manager.RemoveProblem(problem_id);
    }
}

bool IsRelationshipElement(const parser::SacmElement& element) {
    return element.type == "assertedinference" || element.type == "assertedcontext" ||
           element.type == "assertedevidence";
}

std::string ElementTerminologyText(const parser::SacmElement& element) {
    if (element.type == "claim" || element.type == "argumentreasoning")
        return element.content;
    return element.description;
}

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
    return package_ref.id + "\n" + package_ref.gid + "\n" + term_ref.id + "\n" + term_ref.gid + "\n" + term_value;
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
        problem.message = occurrence.text + " has " + std::to_string(occurrence.resolution.candidates.size()) +
                          " visible meanings. Choose the intended terminology entry.";
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
        problem.message = occurrence.text + " looks like an undefined terminology entry.";
        problem.quick_fix_label = "Define term";
        problem.quick_fix_payload = occurrence.text;
        return problem;
    }

    return std::nullopt;
}

const parser::SacmElement* FindParserElement(const parser::AssuranceCase& model, const std::string& element_id) {
    auto found = std::find_if(model.elements.begin(), model.elements.end(), [&](const parser::SacmElement& element) {
        return element.id == element_id;
    });
    return found == model.elements.end() ? nullptr : &*found;
}

core::reviews::ReviewProposal BuildDraftReviewProposal(const core::reviews::ReviewItem& item,
                                                       const parser::AssuranceCase& model,
                                                       const parser::SacmElement& anchor) {
    core::reviews::ReviewProposal proposal;
    proposal.id = GenerateReviewProposalId();
    proposal.review_item_id = item.id;
    proposal.title = item.title.empty() ? "Proposed change" : item.title;
    proposal.summary = item.message.empty() ? "Draft proposed change." : TruncateForProblemMessage(item.message, 180);
    proposal.author_name = "Manual reviewer";
    proposal.created_utc = NowUtcString();
    proposal.anchor_element_id = anchor.id;
    proposal.affected_existing_element_ids = {anchor.id};
    proposal.base_model_hash = core::reviews::ComputeModelSemanticHash(model);
    proposal.base_element_hashes[anchor.id] = core::reviews::ComputeElementSemanticHash(anchor);
    return proposal;
}

core::reviews::ElementRef ExistingElementRef(const std::string& id) {
    return core::reviews::ElementRef{id, std::nullopt};
}

core::reviews::ElementRef CreatedElementRef(const std::string& create_ref) {
    return core::reviews::ElementRef{std::nullopt, create_ref};
}

bool SameElementRef(const core::reviews::ElementRef& lhs, const core::reviews::ElementRef& rhs) {
    return lhs.existing_id == rhs.existing_id && lhs.create_ref == rhs.create_ref;
}

std::optional<core::reviews::ElementRef>
ProposalRefForPreviewId(const std::string& preview_id, const std::map<std::string, std::string>& generated_ids) {
    for (const auto& generated : generated_ids) {
        if (generated.second == preview_id)
            return CreatedElementRef(generated.first);
    }
    if (!preview_id.empty())
        return ExistingElementRef(preview_id);
    return std::nullopt;
}

void TrackAffectedExistingElement(core::reviews::ReviewProposal& proposal,
                                  const parser::AssuranceCase& base_model,
                                  const std::string& element_id) {
    if (element_id.empty())
        return;
    if (std::find(proposal.affected_existing_element_ids.begin(),
                  proposal.affected_existing_element_ids.end(),
                  element_id) == proposal.affected_existing_element_ids.end()) {
        proposal.affected_existing_element_ids.push_back(element_id);
    }
    if (proposal.base_element_hashes.count(element_id) == 0) {
        if (const parser::SacmElement* element = FindParserElement(base_model, element_id)) {
            proposal.base_element_hashes[element_id] = core::reviews::ComputeElementSemanticHash(*element);
        }
    }
}

void TrackAffectedRef(core::reviews::ReviewProposal& proposal,
                      const parser::AssuranceCase& base_model,
                      const core::reviews::ElementRef& ref) {
    if (ref.existing_id.has_value())
        TrackAffectedExistingElement(proposal, base_model, ref.existing_id.value());
}

void ClearProposalHighlightState(ui::UiState& ui_state) {
    ui_state.proposal_highlight_ids.clear();
    ui_state.marked_for_removal.clear();
    ui_state.center_on_marked = false;
    ui_state.dim_non_proposal_nodes = false;
}

bool IsUpdateForElement(const core::reviews::PatchOperation& operation,
                        core::reviews::PatchOperationType type,
                        const core::reviews::ElementRef& ref,
                        const std::string& field) {
    return operation.type == type && operation.element.has_value() && SameElementRef(operation.element.value(), ref) &&
           operation.field == field;
}

void UpsertElementUpdate(core::reviews::ReviewProposal& proposal,
                         core::reviews::PatchOperationType type,
                         const core::reviews::ElementRef& ref,
                         const std::string& field,
                         const std::string& old_value,
                         const std::string& new_value) {
    proposal.operations.erase(std::remove_if(proposal.operations.begin(),
                                             proposal.operations.end(),
                                             [&](const core::reviews::PatchOperation& operation) {
                                                 return IsUpdateForElement(operation, type, ref, field);
                                             }),
                              proposal.operations.end());

    if (old_value == new_value)
        return;

    core::reviews::PatchOperation operation;
    operation.type = type;
    operation.element = ref;
    operation.field = field;
    operation.old_value = old_value;
    operation.new_value = new_value;
    proposal.operations.push_back(std::move(operation));
}

void UpsertUndevelopedUpdate(core::reviews::ReviewProposal& proposal,
                             const core::reviews::ElementRef& ref,
                             bool old_value,
                             bool new_value) {
    proposal.operations.erase(
        std::remove_if(proposal.operations.begin(),
                       proposal.operations.end(),
                       [&](const core::reviews::PatchOperation& operation) {
                           if (operation.type != core::reviews::PatchOperationType::SetUndeveloped &&
                               operation.type != core::reviews::PatchOperationType::ClearUndeveloped) {
                               return false;
                           }
                           return operation.element.has_value() && SameElementRef(operation.element.value(), ref);
                       }),
        proposal.operations.end());

    if (old_value == new_value)
        return;

    core::reviews::PatchOperation operation;
    operation.type = new_value ? core::reviews::PatchOperationType::SetUndeveloped
                               : core::reviews::PatchOperationType::ClearUndeveloped;
    operation.element = ref;
    proposal.operations.push_back(std::move(operation));
}

std::string EditableTextFor(const parser::SacmElement& element) {
    return (element.type == "claim" || element.type == "argumentreasoning") ? element.content : element.description;
}

const char* EditableTextFieldFor(const parser::SacmElement& element) {
    return (element.type == "claim" || element.type == "argumentreasoning") ? "content" : "description";
}

void CopyCommonSacmFields(sacm::SacmElement& target, const parser::SacmElement& source) {
    target.id = source.id;
    target.gid = source.gid;
    target.name = source.name;
    target.description = source.description;
    target.name_ml.texts = source.name_langs;
    target.description_ml.texts = source.description_langs;
    if (target.name_ml.texts.empty() && !source.name.empty())
        target.name_ml.set("en", source.name);
    if (target.description_ml.texts.empty() && !source.description.empty())
        target.description_ml.set("en", source.description);
}

std::string NormalizeSacmRef(std::string ref) {
    if (!ref.empty() && ref.front() == '#')
        ref.erase(ref.begin());
    return ref;
}

void CollectTermRefs(const sacm::TerminologyPackage& terminology_package, std::unordered_set<std::string>& refs) {
    for (const sacm::Term& term : terminology_package.terms) {
        if (!term.id.empty())
            refs.insert(term.id);
        if (!term.gid.empty())
            refs.insert(term.gid);
    }
}

std::unordered_set<std::string> CollectTermRefs(const sacm::AssuranceCasePackage& package) {
    std::unordered_set<std::string> refs;
    for (const sacm::TerminologyPackage& terminology_package : package.terminologyPackages)
        CollectTermRefs(terminology_package, refs);
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::TerminologyPackage& terminology_package : argument_package.terminologyPackages)
            CollectTermRefs(terminology_package, refs);
    }
    return refs;
}

bool ReferencesAny(const std::vector<std::string>& refs, const std::unordered_set<std::string>& candidates) {
    return std::any_of(refs.begin(), refs.end(), [&](const std::string& ref) {
        return candidates.find(NormalizeSacmRef(ref)) != candidates.end();
    });
}

bool ArtifactReferenceTargetsTerm(const sacm::ArtifactReference& artifact_reference,
                                  const std::unordered_set<std::string>& term_refs) {
    return term_refs.find(NormalizeSacmRef(artifact_reference.referencedArtifact)) != term_refs.end();
}

void AddElementRefs(const parser::AssuranceCase& model, std::unordered_set<std::string>& refs) {
    for (const parser::SacmElement& element : model.elements) {
        if (IsRelationshipElement(element))
            continue;
        if (!element.id.empty())
            refs.insert(element.id);
        if (!element.gid.empty())
            refs.insert(element.gid);
    }
}

bool ContainsSacmElementRef(const std::vector<std::string>& refs, const std::unordered_set<std::string>& candidates) {
    return ReferencesAny(refs, candidates);
}

bool HasArtifactReference(const sacm::ArgumentPackage& argument_package, const sacm::ArtifactReference& candidate) {
    return std::any_of(argument_package.artifactReferences.begin(),
                       argument_package.artifactReferences.end(),
                       [&](const auto& existing) {
                           return (!candidate.id.empty() && existing.id == candidate.id) ||
                                  (!candidate.gid.empty() && existing.gid == candidate.gid);
                       });
}

bool HasAssertedContext(const sacm::ArgumentPackage& argument_package, const sacm::AssertedContext& candidate) {
    return std::any_of(
        argument_package.assertedContexts.begin(), argument_package.assertedContexts.end(), [&](const auto& existing) {
            return (!candidate.id.empty() && existing.id == candidate.id) ||
                   (!candidate.gid.empty() && existing.gid == candidate.gid) ||
                   (existing.sources == candidate.sources && existing.targets == candidate.targets);
        });
}

void RebuildSacmArgumentPackageFromParser(const parser::AssuranceCase& model, sacm::AssuranceCasePackage& package) {
    if (package.argumentPackages.empty())
        package.argumentPackages.emplace_back();
    std::map<std::string, std::string> artifact_reference_targets;
    const std::unordered_set<std::string> term_refs = CollectTermRefs(package);
    std::vector<sacm::ArtifactReference> preserved_term_references;
    std::vector<sacm::AssertedContext> preserved_term_contexts;
    for (sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        std::unordered_set<std::string> term_artifact_refs;
        for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
            if (artifact_reference.referencedArtifact.empty())
                continue;
            if (!artifact_reference.id.empty())
                artifact_reference_targets[artifact_reference.id] = artifact_reference.referencedArtifact;
            if (!artifact_reference.gid.empty())
                artifact_reference_targets[artifact_reference.gid] = artifact_reference.referencedArtifact;
            if (ArtifactReferenceTargetsTerm(artifact_reference, term_refs) &&
                !core::IsVisibleTerminologyArtifactReference(package, argument_package, artifact_reference)) {
                preserved_term_references.push_back(artifact_reference);
                if (!artifact_reference.id.empty())
                    term_artifact_refs.insert(artifact_reference.id);
                if (!artifact_reference.gid.empty())
                    term_artifact_refs.insert(artifact_reference.gid);
            }
        }
        for (const sacm::AssertedContext& context : argument_package.assertedContexts) {
            if (ReferencesAny(context.sources, term_artifact_refs))
                preserved_term_contexts.push_back(context);
        }
        argument_package.claims.clear();
        argument_package.argumentReasonings.clear();
        argument_package.artifactReferences.clear();
        argument_package.assertedInferences.clear();
        argument_package.assertedContexts.clear();
        argument_package.assertedEvidences.clear();
    }

    sacm::ArgumentPackage& argument_package = package.argumentPackages.front();
    for (const parser::SacmElement& element : model.elements) {
        if (element.type == "claim") {
            sacm::Claim claim;
            CopyCommonSacmFields(claim, element);
            claim.content = element.content;
            claim.content_ml.texts = element.content_langs;
            if (claim.content_ml.texts.empty() && !element.content.empty())
                claim.content_ml.set("en", element.content);
            claim.assertionDeclaration = element.assertion_declaration;
            claim.undeveloped = element.undeveloped;
            argument_package.claims.push_back(std::move(claim));
        } else if (element.type == "argumentreasoning") {
            sacm::ArgumentReasoning reasoning;
            CopyCommonSacmFields(reasoning, element);
            reasoning.content = element.content;
            reasoning.content_ml.texts = element.content_langs;
            if (reasoning.content_ml.texts.empty() && !element.content.empty())
                reasoning.content_ml.set("en", element.content);
            reasoning.undeveloped = element.undeveloped;
            argument_package.argumentReasonings.push_back(std::move(reasoning));
        } else if (element.type == "artifactreference" || element.type == "artifact") {
            sacm::ArtifactReference artifact_reference;
            CopyCommonSacmFields(artifact_reference, element);
            auto target = artifact_reference_targets.find(element.id);
            if (target == artifact_reference_targets.end())
                target = artifact_reference_targets.find(element.gid);
            if (target != artifact_reference_targets.end())
                artifact_reference.referencedArtifact = target->second;
            argument_package.artifactReferences.push_back(std::move(artifact_reference));
        } else if (element.type == "assertedinference") {
            sacm::AssertedInference inference;
            CopyCommonSacmFields(inference, element);
            inference.sources = element.source_refs;
            inference.targets = element.target_refs;
            inference.reasoning = element.reasoning_ref;
            inference.assertionDeclaration = element.assertion_declaration;
            argument_package.assertedInferences.push_back(std::move(inference));
        } else if (element.type == "assertedcontext") {
            sacm::AssertedContext context;
            CopyCommonSacmFields(context, element);
            context.sources = element.source_refs;
            context.targets = element.target_refs;
            context.assertionDeclaration = element.assertion_declaration;
            argument_package.assertedContexts.push_back(std::move(context));
        } else if (element.type == "assertedevidence") {
            sacm::AssertedEvidence evidence;
            CopyCommonSacmFields(evidence, element);
            evidence.sources = element.source_refs;
            evidence.targets = element.target_refs;
            evidence.assertionDeclaration = element.assertion_declaration;
            argument_package.assertedEvidences.push_back(std::move(evidence));
        }
    }

    std::unordered_set<std::string> model_element_refs;
    AddElementRefs(model, model_element_refs);
    std::unordered_set<std::string> kept_artifact_refs;
    for (const sacm::ArtifactReference& artifact_reference : preserved_term_references) {
        if (HasArtifactReference(argument_package, artifact_reference))
            continue;
        argument_package.artifactReferences.push_back(artifact_reference);
        if (!artifact_reference.id.empty())
            kept_artifact_refs.insert(artifact_reference.id);
        if (!artifact_reference.gid.empty())
            kept_artifact_refs.insert(artifact_reference.gid);
    }
    for (const sacm::AssertedContext& context : preserved_term_contexts) {
        if (!ReferencesAny(context.sources, kept_artifact_refs) ||
            !ContainsSacmElementRef(context.targets, model_element_refs)) {
            continue;
        }
        if (!HasAssertedContext(argument_package, context))
            argument_package.assertedContexts.push_back(context);
    }
    for (sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
        if (!core::IsVisibleTerminologyArtifactReference(package, argument_package, artifact_reference))
            continue;
        artifact_reference.description.clear();
        artifact_reference.description_ml.texts.clear();
    }
}

} // namespace

ui::ElementContextActions MakeElementContextActions(AppRuntime& runtime) {
    return ui::ElementContextActions{
        [&runtime](core::NewElementKind kind) { runtime.AddChildToSelected(kind); },
        [&runtime]() { runtime.AddTopGoal(); },
        [&runtime](core::RemoveMode mode) { runtime.RemoveSelected(mode); },
        [&runtime]() { runtime.RenderAiReviewContextMenuForSelected(); },
        [&runtime](const char* feature) {
            if (feature)
                runtime.ShowNotImplementedModal(feature);
        },
    };
}

AppRuntime::AppRuntime() : impl_(new AppRuntimeState()) {
    RegisterAppEventListeners();

    impl_->current_tree = core::AssuranceTree();
    ui::gsn::SetCanvasTree(impl_->current_tree);
    ui::RebuildRegisterViews(nullptr);

    ui::UiState& ui_state = ui::GetUiState();
    ui_state.center_view = ui::CenterView::GsnCanvas;
    ui_state.selected_element_id.clear();
    ui_state.selected_problem_id.clear();
    ui_state.selected_problem_element_id.clear();
}

AppRuntime::~AppRuntime() {
    delete impl_;
}

void AppRuntime::RegisterAppEventListeners() {
    impl_->events.Subscribe<StatusMessageEvent>(
        [this](const StatusMessageEvent& event) { impl_->app_state.status_message = event.message; });
    impl_->events.Subscribe<TreeDirtyEvent>([this](const TreeDirtyEvent& event) {
        impl_->tree_needs_rebuild = event.dirty;
        impl_->tree_edit_index_valid = false;
        if (event.focus_root)
            impl_->workbench.pending_focus_root = true;
    });
    impl_->events.Subscribe<DocumentDirtyEvent>([this](const DocumentDirtyEvent& event) {
        impl_->document_dirty = event.dirty;
        if (event.mark_app_dirty)
            impl_->app_state.mark_dirty();
    });
    impl_->events.Subscribe<ReviewItemsDirtyEvent>([this](const ReviewItemsDirtyEvent& event) {
        if (event.mark_app_dirty)
            impl_->app_state.mark_dirty();
        SyncReviewProblems();
        SyncReviewVisualStatesFromReviews();
    });
    impl_->events.Subscribe<SelectionChangedEvent>([](const SelectionChangedEvent& event) {
        ui::UiState& ui_state = ui::GetUiState();
        ui_state.selected_element_id = event.element_id;
        ui_state.center_on_selection = event.center_on_selection;
    });
    impl_->events.Subscribe<ElementReviewVisualEvent>([](const ElementReviewVisualEvent& event) {
        ui::UiState& ui_state = ui::GetUiState();
        switch (event.kind) {
        case ElementReviewVisualEventKind::AiStarted:
            ui::MarkAiReviewRunning(ui_state,
                                    event.element_id,
                                    event.review_profile_id,
                                    event.review_profile_name,
                                    event.review_scope_element_ids);
            break;
        case ElementReviewVisualEventKind::AiNoFindings:
            ui::MarkAiReviewNoFindings(ui_state, event.element_id, event.review_profile_id, event.review_profile_name);
            break;
        case ElementReviewVisualEventKind::AiFindings:
            ui::MarkAiReviewFindings(ui_state, event.element_id);
            break;
        case ElementReviewVisualEventKind::AiFailed:
            ui::MarkAiReviewFailed(
                ui_state, event.element_id, event.message, event.review_profile_id, event.review_profile_name);
            break;
        case ElementReviewVisualEventKind::ManualOk:
            ui::MarkReviewOkManually(ui_state, event.element_id);
            break;
        }
    });
    impl_->events.Subscribe<AiReviewProposalSuggestionsEvent>(
        [this](const AiReviewProposalSuggestionsEvent& event) { CreateAiGeneratedProposals(event.suggestions); });
    impl_->events.Subscribe<CenterRequestEvent>([this](const CenterRequestEvent& event) {
        ui::UiState& ui_state = ui::GetUiState();
        switch (event.view) {
        case CenterViewRequest::Preserve:
            break;
        case CenterViewRequest::GsnCanvas:
            ui_state.center_view = ui::CenterView::GsnCanvas;
            break;
        case CenterViewRequest::CseRegister:
            ui_state.center_view = ui::CenterView::CseRegister;
            break;
        case CenterViewRequest::EvidenceRegister:
            ui_state.center_view = ui::CenterView::EvidenceRegister;
            break;
        }
        ui_state.center_on_selection = event.center_on_selection;
        ui_state.center_on_marked = event.center_on_marked;
        if (event.force_tab_selection)
            impl_->workbench.force_center_tab_selection = true;
    });
    impl_->events.Subscribe<ProposalHighlightEvent>([](const ProposalHighlightEvent& event) {
        ui::UiState& ui_state = ui::GetUiState();
        ui_state.proposal_highlight_ids = event.highlight_ids;
        ui_state.marked_for_removal = event.marked_for_removal;
        ui_state.dim_non_proposal_nodes = event.dim_non_proposal_nodes;
        ui_state.center_on_marked = event.center_on_marked;
    });
    impl_->events.Subscribe<ModalRequestEvent>([this](const ModalRequestEvent& event) {
        impl_->modal_coordinator->ApplyModalRequest(event);
        switch (event.kind) {
        case ModalKind::StartupProject:
            impl_->project_controller->show_startup_project_window = event.open;
            break;
        default:
            break;
        }
    });
}

void AppRuntime::RequestClose() {
    impl_->modal_coordinator->RequestClose();
}

bool AppRuntime::AddChildToSelected(core::NewElementKind kind) {
    return actions::ElementActions(*impl_).AddChildToSelected(kind);
}

bool AppRuntime::AddTopGoal() {
    return actions::ElementActions(*impl_).AddTopGoal();
}

void AppRuntime::RemoveSelected(core::RemoveMode mode) {
    actions::ElementActions(*impl_).RemoveSelected(mode);
}

core::TreeDropValidationResult AppRuntime::ValidateTreeDrop(const std::string& dragged_id,
                                                            const std::string& target_id,
                                                            core::TreeDropMode drop_mode) const {
    return actions::ElementActions(*impl_).ValidateTreeDrop(dragged_id, target_id, drop_mode);
}

bool AppRuntime::PerformTreeDrop(const std::string& dragged_id,
                                 const std::string& target_id,
                                 core::TreeDropMode drop_mode) {
    return actions::ElementActions(*impl_).PerformTreeDrop(dragged_id, target_id, drop_mode);
}

void AppRuntime::SetStatus(const std::string& message) {
    impl_->events.Emit(StatusMessageEvent{message});
}

void AppRuntime::ShowNotImplementedModal(const std::string& feature) {
    impl_->events.Emit(ModalRequestEvent{ModalKind::NotImplemented, true, feature});
}

bool AppRuntime::RefreshProposalCreatorPreview() {
    return actions::ProposalActions(*impl_).RefreshCreatorPreview();
}

void AppRuntime::ProcessPendingProposalCreatorPreviewRefresh() {
    actions::ProposalActions(*impl_).ProcessPendingCreatorPreviewRefresh();
}

bool AppRuntime::BeginProposalForReviewItem(const core::reviews::ReviewItem& item) {
    return actions::ProposalActions(*impl_).BeginForReviewItem(item);
}

bool AppRuntime::BeginEditProposalForReviewItem(const core::reviews::ReviewItem& item) {
    return actions::ProposalActions(*impl_).BeginEditForReviewItem(item);
}

bool AppRuntime::BeginEditProposalById(const std::string& proposal_id) {
    return actions::ProposalActions(*impl_).BeginEditById(proposal_id);
}

bool AppRuntime::PreviewProposalById(const std::string& proposal_id) {
    return actions::ProposalActions(*impl_).PreviewById(proposal_id);
}

bool AppRuntime::SaveActiveProposal(const core::reviews::ReviewItem& item) {
    return actions::ProposalActions(*impl_).SaveActive(item);
}

void AppRuntime::CreateAiGeneratedProposals(const std::vector<AiReviewProposalSuggestion>& suggestions) {
    if (suggestions.empty())
        return;
    if (!impl_->app_state.current_project.has_value() || !impl_->app_state.loaded_case.has_value()) {
        SetStatus("AI found proposed wording, but a project and SACM file must be open to save proposals.");
        return;
    }

    core::AssuranceProject& project = impl_->app_state.current_project.value();
    const parser::AssuranceCase& model = impl_->app_state.loaded_case.value();
    size_t saved_count = 0;
    for (const AiReviewProposalSuggestion& suggestion : suggestions) {
        const std::string suggested_text = TrimWhitespace(suggestion.suggested_text);
        if (suggested_text.empty())
            continue;

        std::optional<core::reviews::ReviewItem> item =
            impl_->review_controller->GetItemById(suggestion.review_item_id);
        if (!item.has_value() || item->proposal_id.has_value())
            continue;

        const parser::SacmElement* anchor = FindParserElement(model, item->element_id);
        if (!anchor || anchor->type != "claim")
            continue;

        const std::string current_text = anchor->content.empty() ? anchor->description : anchor->content;
        if (TrimWhitespace(current_text) == suggested_text)
            continue;

        core::reviews::ReviewProposal proposal = BuildDraftReviewProposal(*item, model, *anchor);
        proposal.author_name = "AI Review";
        proposal.summary = "AI suggested replacement wording for " + anchor->id + ".";

        core::reviews::PatchOperation operation;
        operation.type = core::reviews::PatchOperationType::UpdateElementText;
        operation.element = core::reviews::ElementRef{anchor->id, std::nullopt};
        operation.field = "content";
        operation.old_value = current_text;
        operation.new_value = suggested_text;
        proposal.operations.push_back(std::move(operation));

        core::ProjectFileEntry entry;
        std::string error;
        if (!core::ProjectService::SaveReviewProposalFile(
                project, proposal.id, core::reviews::SerializeReviewProposal(proposal), entry, error)) {
            SetStatus("AI proposal save failed: " + error);
            continue;
        }

        if (!impl_->review_controller->SetProposal(item->id, proposal.id)) {
            std::string cleanup_error;
            core::ProjectService::RemoveTrackedFile(project, entry.relativePath, true, cleanup_error);
            SetStatus("AI proposal link update failed.");
            continue;
        }
        ++saved_count;
    }

    if (saved_count > 0) {
        core::ProjectService::RefreshFileStatus(project);
        SetStatus("AI generated " + std::to_string(saved_count) + " proposed change(s). Review before applying.");
    }
}

void AppRuntime::CancelActiveProposal() {
    actions::ProposalActions(*impl_).CancelActive();
}

bool AppRuntime::DeleteProposalPatchFile(const std::string& proposal_id, std::string& error) {
    return actions::ReviewActions(*impl_).DeleteProposalPatchFile(proposal_id, error);
}

void AppRuntime::CloseProposalPreviewIfOpen(const std::string& proposal_id) {
    actions::ReviewActions(*impl_).CloseProposalPreviewIfOpen(proposal_id);
}

void AppRuntime::BeginDeleteReviewItem(const core::reviews::ReviewItem& item) {
    actions::ReviewActions(*impl_).BeginDeleteReviewItem(item);
}

bool AppRuntime::DeleteReviewItem(const core::reviews::ReviewItem& item) {
    return actions::ReviewActions(*impl_).DeleteReviewItem(item);
}

bool AppRuntime::ResolveReviewItem(const core::reviews::ReviewItem& item) {
    return actions::ReviewActions(*impl_).ResolveReviewItem(item, NowUtcString());
}

bool AppRuntime::AddProposalChildToSelected(core::NewElementKind kind) {
    return actions::ProposalActions(*impl_).AddChildToSelected(kind);
}

bool AppRuntime::AddProposalTopGoal() {
    return actions::ProposalActions(*impl_).AddTopGoal();
}

void AppRuntime::RemoveProposalSelected(core::RemoveMode mode) {
    actions::ProposalActions(*impl_).RemoveSelected(mode);
}

void AppRuntime::ScanDirectory() {
    impl_->project_controller->ScanDirectory();
}

void AppRuntime::RebuildDerivedViewsIfNeeded() {
    if (impl_->IsProposalCanvasActive()) {
        return;
    }

    if (impl_->tree_needs_rebuild && !impl_->app_state.loaded_case.has_value()) {
        ui::RebuildRegisterViews(nullptr);
        impl_->tree_edit_index = core::TreeEditIndex();
        impl_->tree_edit_index_valid = false;
        impl_->tree_needs_rebuild = false;
        return;
    }

    if (!impl_->app_state.loaded_case.has_value() || !impl_->tree_needs_rebuild) {
        return;
    }

    const auto& ac = impl_->app_state.loaded_case.value();
    impl_->current_tree = ui::gsn::BuildAssuranceTree(ac);
    core::ApplyTreeDisplayOrder(impl_->current_tree, impl_->tree_display_order);
    impl_->tree_edit_index = core::BuildTreeEditIndex(ac);
    impl_->tree_edit_index_valid = true;
    ui::gsn::SetCanvasTree(impl_->current_tree);
    ui::RebuildRegisterViews(&ac);
    ui::GetUiState().model_has_translations = ui::ModelHasTranslations(ac);
    SyncTerminologyProblems();

    if (impl_->workbench.pending_focus_root && impl_->current_tree.root) {
        ui::UiState& ui_state = ui::GetUiState();
        ui_state.selected_element_id = impl_->current_tree.root->id;
        ui_state.center_on_selection = true;
        ui_state.center_view = ui::CenterView::GsnCanvas;
        impl_->workbench.force_center_tab_selection = true;
        impl_->workbench.pending_focus_root = false;
    }

    impl_->tree_needs_rebuild = false;
}

void AppRuntime::RenderSacmViewerPanel(float left_w, float sacm_h, float top_y) {
    ui::panels::SacmViewerPanelModel model{
        impl_->app_state,
        impl_->project_controller->dir_path_buf,
        sizeof(impl_->project_controller->dir_path_buf),
        impl_->project_controller->file_path_buf,
        sizeof(impl_->project_controller->file_path_buf),
        impl_->project_controller->xml_files,
        impl_->project_controller->selected_file_idx,
        impl_->project_controller->show_overwrite_confirm,
    };
    ui::panels::SacmViewerPanelCallbacks callbacks{
        [this]() { ScanDirectory(); },
        [this]() {
            impl_->tree_needs_rebuild = true;
            impl_->workbench.pending_focus_root = true;
        },
        [this]() {
            impl_->current_tree = core::AssuranceTree();
            ui::gsn::SetCanvasTree(impl_->current_tree);
            ui::RebuildRegisterViews(nullptr);
            ui::GetUiState().selected_element_id.clear();
        },
    };
    ui::panels::ShowSacmViewerPanel(left_w, sacm_h, top_y, kPanelFlags, model, callbacks);
}

areas::WorkbenchAreaCallbacks AppRuntime::MakeWorkbenchAreaCallbacks() {
    return areas::WorkbenchAreaCallbacks{
        [this]() { return MakeElementContextActions(*this); },
        [this](core::NewElementKind kind) { AddProposalChildToSelected(kind); },
        [this]() { AddProposalTopGoal(); },
        [this](core::RemoveMode mode) { RemoveProposalSelected(mode); },
        [this](const char* feature) {
            if (feature)
                ShowNotImplementedModal(feature);
        },
        [this](const std::string& proposal_id) { return BeginEditProposalById(proposal_id); },
        [this](bool was_creator) {
            impl_->proposal_controller->ClearActiveState();
            ClearProposalHighlightState(ui::GetUiState());
            if (impl_->app_state.loaded_case.has_value()) {
                impl_->current_tree = ui::gsn::BuildAssuranceTree(impl_->app_state.loaded_case.value());
                ui::gsn::SetCanvasTree(impl_->current_tree);
            } else {
                impl_->tree_needs_rebuild = true;
            }
            if (was_creator)
                SetStatus("Discarded proposal draft.");
        },
        [this](const core::TerminologyPackageRef& package_ref, const core::TerminologyTermRef& term_ref) {
            OpenTerminologyTermFromCanvas(package_ref, term_ref);
        },
        [this](const core::TerminologyPackageRef& package_ref, const core::TerminologyTermRef& term_ref) {
            EditTerminologyTermFromCanvas(package_ref, term_ref);
        },
        [this](const std::string& element_id, const std::string& term_value) {
            BeginQuickDefineTerminologyTerm(element_id, term_value);
        },
        [this](const std::string& element_id,
               const core::TerminologyPackageRef& package_ref,
               const core::TerminologyTermRef& term_ref) {
            AddTerminologyTermAsContextFromCanvas(element_id, package_ref, term_ref);
        },
        [this](const std::string& element_id,
               const core::TerminologyPackageRef& package_ref,
               const core::TerminologyTermRef& term_ref) {
            AddVisibleTerminologyTermContextFromCanvas(element_id, package_ref, term_ref);
        },
        [this](const core::TerminologyPackageRef& package_ref, const core::TerminologyTermRef& term_ref) {
            FindTerminologyUsagesFromCanvas(package_ref, term_ref);
        },
        [this](const std::string& element_id, const std::string& term_value) {
            ChangeTerminologyMeaningFromCanvas(element_id, term_value);
        },
        [this]() { SyncReviewVisualStatesFromReviews(); },
        [this]() { ApplyTerminologyPackageEdits(); },
        [this]() { BeginDeleteTerminologyPackage(); },
        [this]() { BeginAddTerminologyTerm(); },
        [this](const core::TerminologyTermRef& term_ref) { SelectTerminologyTerm(term_ref); },
        [this](const core::TerminologyTermRef& term_ref) { BeginEditTerminologyTerm(term_ref); },
        [this](const core::TerminologyTermRef& term_ref) { BeginDeleteTerminologyTerm(term_ref); },
        [this](const core::TerminologyTermRef& term_ref) {
            BeginFindTerminologyUsages(impl_->terminology.selected_package_ref, term_ref);
        },
        [this](const std::string& category_filter) { SetTerminologyCategoryFilter(category_filter); },
        [this]() { BeginAddTerminologyCategory(); },
        [this](const core::TerminologyCategoryRef& category_ref) { SelectTerminologyCategory(category_ref); },
        [this](const core::TerminologyCategoryRef& category_ref) { BeginEditTerminologyCategory(category_ref); },
        [this](const core::TerminologyCategoryRef& category_ref) { BeginDeleteTerminologyCategory(category_ref); },
        [this]() { SeedRecommendedTerminologyCategories(); },
    };
}

void AppRuntime::SyncReviewProblems() {
    app::SyncReviewProblems(impl_->problems_manager, impl_->review_controller->Items());
}

void AppRuntime::SyncTerminologyProblems() {
    constexpr const char* kTerminologyTermProblemPrefix = "terminology-term:";
    constexpr const char* kTerminologyAmbiguityProblemPrefix = "terminology-ambiguity:";
    constexpr const char* kTerminologyUndefinedProblemPrefix = "terminology-undefined:";
    constexpr const char* kTerminologyContextReferenceProblemPrefix = "terminology-context-reference:";
    ClearProblemsByIdPrefix(impl_->problems_manager, kTerminologyTermProblemPrefix);
    ClearProblemsByIdPrefix(impl_->problems_manager, kTerminologyAmbiguityProblemPrefix);
    ClearProblemsByIdPrefix(impl_->problems_manager, kTerminologyUndefinedProblemPrefix);
    ClearProblemsByIdPrefix(impl_->problems_manager, kTerminologyContextReferenceProblemPrefix);

    if (!impl_->app_state.loaded_case.has_value() || !impl_->app_state.sacm_package.has_value())
        return;

    const parser::AssuranceCase& model = impl_->app_state.loaded_case.value();
    const sacm::AssuranceCasePackage& package = impl_->app_state.sacm_package.value();

    auto add_term_problems = [&](const sacm::TerminologyPackage& terminology_package) {
        for (core::ProblemItem& problem : BuildTerminologyTermProblems(terminology_package)) {
            impl_->problems_manager.AddOrUpdateProblem(problem);
        }
    };

    for (const sacm::TerminologyPackage& terminology_package : package.terminologyPackages)
        add_term_problems(terminology_package);
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::TerminologyPackage& terminology_package : argument_package.terminologyPackages)
            add_term_problems(terminology_package);
    }

    for (const core::TerminologyContextReferenceIssue& issue : core::ValidateTerminologyContextReferences(package)) {
        core::ProblemItem problem = BuildTerminologyContextReferenceProblem(issue);
        impl_->problems_manager.AddOrUpdateProblem(problem);
    }

    core::TerminologyService terminology_service(package);

    for (const parser::SacmElement& element : model.elements) {
        if (IsRelationshipElement(element))
            continue;
        const std::string text = ElementTerminologyText(element);
        if (text.empty())
            continue;

        std::vector<core::TermOccurrence> occurrences = terminology_service.DetectTermsInText(element.id, text);
        for (const core::TermOccurrence& occurrence : occurrences) {
            std::optional<core::ProblemItem> problem = BuildTerminologyOccurrenceProblem(
                occurrence, IsTerminologySuggestionIgnored(occurrence.element_id, occurrence.text));
            if (problem.has_value())
                impl_->problems_manager.AddOrUpdateProblem(*problem);
        }
    }
}

void AppRuntime::SyncReviewVisualStatesFromReviews() {
    ui::UiState& ui_state = ui::GetUiState();
    std::unordered_map<std::string, ui::ElementReviewVisualState> next_states;

    for (const auto& [element_id, stored_state] : impl_->review_controller->ElementReviewStates()) {
        ui::ElementReviewVisualState visual;
        visual.manual_ok = stored_state.manual_ok;
        visual.ai_ok = stored_state.ai_ok;
        visual.failed = stored_state.failed;
        visual.review_profile_id = stored_state.review_profile_id;
        visual.review_profile_name = stored_state.review_profile_name;
        visual.last_review_message = stored_state.last_review_message;
        next_states[element_id] = std::move(visual);
    }

    for (const core::reviews::ReviewItem& item : impl_->review_controller->Items()) {
        if (item.element_id.empty() || item.status != core::reviews::ReviewItemStatus::Open)
            continue;
        ui::ElementReviewVisualState& visual = next_states[item.element_id];
        visual.failed = true;
        visual.last_review_message = "Open review items require attention.";
    }

    for (const core::ProblemItem& problem : impl_->problems_manager.GetProblems()) {
        if (problem.element_id.empty() || IsReviewDerivedProblem(problem))
            continue;
        ui::ElementReviewVisualState& visual = next_states[problem.element_id];
        visual.failed = true;
        visual.last_review_message = "Open problems require attention.";
    }

    for (const auto& [element_id, existing_state] : ui_state.review_visual_states) {
        if (!existing_state.ai_running)
            continue;
        ui::ElementReviewVisualState& visual = next_states[element_id];
        visual.ai_running = true;
        if (!existing_state.review_profile_id.empty())
            visual.review_profile_id = existing_state.review_profile_id;
        if (!existing_state.review_profile_name.empty())
            visual.review_profile_name = existing_state.review_profile_name;
        visual.last_review_message = existing_state.last_review_message;
    }

    ui_state.review_visual_states = std::move(next_states);
}

bool AppRuntime::SetManualReviewOk(const std::string& element_id, bool manual_ok) {
    if (element_id.empty()) {
        SetStatus("Select an element before changing manual review status.");
        return false;
    }
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus("Open or create a project before changing review status.");
        return false;
    }
    if (!EnsureReviewItemStorage())
        return false;
    if (impl_->reviewer_name.empty()) {
        impl_->modal_coordinator->show_reviewer_name_prompt = true;
        SetStatus("Enter a reviewer name before changing review status.");
        return false;
    }
    if (!impl_->review_controller->SetManualReviewOk(element_id, manual_ok, impl_->reviewer_name, NowUtcString())) {
        SetStatus("Could not update manual review status.");
        return false;
    }
    SyncReviewVisualStatesFromReviews();
    return true;
}

void AppRuntime::RenderProposalElementEditor() {
    auto& proposals = *impl_->proposal_controller;
    if (!proposals.creator_active)
        return;

    ImGui::TextUnformatted("Proposal Creator");
    ImGui::TextDisabled("Edits are recorded in the proposal draft only.");
    ImGui::Separator();

    const std::string selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        ImGui::TextWrapped("Select a proposal preview element to edit its proposed properties.");
        return;
    }

    const parser::SacmElement* element = FindParserElement(proposals.preview_model, selected_id);
    if (!element) {
        ImGui::TextWrapped("The selected proposal preview element no longer exists.");
        return;
    }

    static std::string active_editor_key;
    static char name_buf[256] = "";
    static char text_buf[2048] = "";
    const std::string editor_key = proposals.draft.id + ":" + selected_id;
    if (active_editor_key != editor_key) {
        active_editor_key = editor_key;
        CopyToBuffer(name_buf, sizeof(name_buf), element->name);
        CopyToBuffer(text_buf, sizeof(text_buf), EditableTextFor(*element));
    }

    const parser::SacmElement element_snapshot = *element;
    std::optional<core::reviews::ElementRef> ref =
        ProposalRefForPreviewId(selected_id, proposals.creator_generated_ids);
    if (!ref.has_value()) {
        ImGui::TextWrapped("Could not resolve this preview element for proposal edits.");
        return;
    }

    std::string old_name = element_snapshot.name;
    std::string old_text = EditableTextFor(element_snapshot);
    bool old_undeveloped = element_snapshot.undeveloped;
    if (ref->existing_id.has_value() && impl_->app_state.loaded_case.has_value()) {
        if (const parser::SacmElement* base =
                FindParserElement(impl_->app_state.loaded_case.value(), ref->existing_id.value())) {
            old_name = base->name;
            old_text = EditableTextFor(*base);
            old_undeveloped = base->undeveloped;
        }
    }

    ImGui::TextDisabled("%s  %s", ref->existing_id.has_value() ? "Existing" : "New", selected_id.c_str());
    if (ImGui::Button("Remove")) {
        RemoveProposalSelected(core::RemoveMode::NodeOnly);
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove Subtree")) {
        RemoveProposalSelected(core::RemoveMode::NodeAndDescendants);
        return;
    }
    ImGui::Separator();

    ImGui::PushID(editor_key.c_str());
    ImGui::SetNextItemWidth(-1.0f);
    const bool name_changed = ImGui::InputText("Name", name_buf, sizeof(name_buf));
    ImGui::SetNextItemWidth(-1.0f);
    const bool text_changed =
        ImGui::InputTextMultiline("Text", text_buf, sizeof(text_buf), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 5.0f));
    bool undeveloped_value = element_snapshot.undeveloped;
    const bool undeveloped_changed = ImGui::Checkbox("Undeveloped", &undeveloped_value);
    ImGui::PopID();

    if (!name_changed && !text_changed && !undeveloped_changed)
        return;

    TrackAffectedRef(proposals.draft, impl_->app_state.loaded_case.value(), ref.value());
    if (name_changed) {
        UpsertElementUpdate(proposals.draft,
                            core::reviews::PatchOperationType::UpdateElementName,
                            ref.value(),
                            "name",
                            old_name,
                            name_buf);
    }
    if (text_changed) {
        UpsertElementUpdate(proposals.draft,
                            core::reviews::PatchOperationType::UpdateElementText,
                            ref.value(),
                            EditableTextFieldFor(element_snapshot),
                            old_text,
                            text_buf);
    }
    if (undeveloped_changed) {
        UpsertUndevelopedUpdate(proposals.draft, ref.value(), old_undeveloped, undeveloped_value);
    }

    if (RefreshProposalCreatorPreview()) {
        SetStatus("Recorded proposal property change.");
    }
}

void AppRuntime::ApplyReviewProposal(const core::reviews::ReviewItem& item) {
    if (impl_->proposal_controller->creator_active) {
        SetStatus("Save or discard the active proposal before applying another proposal.");
        return;
    }
    if (!item.proposal_id.has_value()) {
        SetStatus("This review comment has no proposed change to apply.");
        return;
    }
    if (!impl_->app_state.current_project.has_value() || !impl_->app_state.loaded_case.has_value()) {
        SetStatus("Open a project and SACM file before applying proposed changes.");
        return;
    }

    std::string error;
    std::optional<core::reviews::ReviewProposal> proposal =
        impl_->proposal_controller->manager.LoadProposal(item.proposal_id.value(), error);
    if (!proposal.has_value()) {
        SetStatus("Proposal apply failed: " + error);
        return;
    }

    core::reviews::ProposalValidityResult validity =
        core::reviews::EvaluateReviewProposalValidity(*proposal, impl_->app_state.loaded_case.value());
    if (validity.validity != core::reviews::ProposalValidity::Valid) {
        SetStatus("Proposal is broken: " + validity.reason);
        return;
    }

    core::reviews::ReviewProposalPatchService patch_service;
    core::reviews::ApplyProposalResult apply_result =
        patch_service.ApplyProposal(*proposal, impl_->app_state.loaded_case.value());
    if (!apply_result.success) {
        SetStatus("Proposal apply failed: " + apply_result.error);
        return;
    }

    impl_->proposal_controller->ClosePreviewIfOpen(item.proposal_id.value());
    ClearProposalHighlightState(ui::GetUiState());
    if (!impl_->app_state.sacm_package.has_value())
        impl_->app_state.sacm_package.emplace();
    RebuildSacmArgumentPackageFromParser(impl_->app_state.loaded_case.value(), impl_->app_state.sacm_package.value());
    impl_->document_dirty = true;
    impl_->app_state.mark_dirty();

    core::AssuranceProject& project = impl_->app_state.current_project.value();
    if (!DeleteProposalPatchFile(item.proposal_id.value(), error)) {
        SetStatus("Proposal applied in memory, but proposal file removal failed: " + error);
        return;
    }

    core::reviews::ReviewItem updated = item;
    updated.proposal_id.reset();
    updated.status = core::reviews::ReviewItemStatus::Resolved;
    updated.applied_note = "Proposal applied at " + NowUtcString() + ".";
    updated.updated_utc = NowUtcString();
    if (!impl_->review_controller->AddOrUpdateItem(std::move(updated))) {
        SetStatus("Proposal applied, but review item update failed.");
        return;
    }

    if (!SaveProject()) {
        SetStatus("Proposal applied, but project save failed: " + impl_->app_state.status_message);
        return;
    }

    core::ProjectService::RefreshFileStatus(project);
    impl_->tree_needs_rebuild = true;
    SetStatus("Applied proposal " + proposal->id + ".");
}

void AppRuntime::RequestExit(bool& done) {
    if (impl_->app_state.has_unsaved_changes) {
        impl_->modal_coordinator->show_save_before_exit_modal = true;
        return;
    }
    done = true;
}

const parser::AssuranceCase* AppRuntime::GetLoadedCase() const {
    if (!impl_->app_state.loaded_case.has_value())
        return nullptr;
    return &impl_->app_state.loaded_case.value();
}

void AppRuntime::LoadRecentProjectsPreference(const std::string& content) {
    impl_->project_controller->LoadRecentProjectsPreference(content);
}

std::string AppRuntime::RecentProjectsPreferenceJson() const {
    return impl_->project_controller->RecentProjectsPreferenceJson();
}

void AppRuntime::LoadReviewerNamePreference(const std::string& content) {
    impl_->reviewer_name = TrimWhitespace(content);
    CopyToBuffer(impl_->reviewer_name_buf, sizeof(impl_->reviewer_name_buf), impl_->reviewer_name);
    impl_->modal_coordinator->show_reviewer_name_prompt = impl_->reviewer_name.empty();
}

std::string AppRuntime::ReviewerNamePreference() const {
    return impl_->reviewer_name;
}

void AppRuntime::RenderFrame(bool& done) {
    if (impl_->modal_coordinator->ConsumeCloseRequest()) {
        RequestExit(done);
    }

    frame::AppMenuBarCallbacks menu_callbacks;
    menu_callbacks.begin_create_project = [this]() { BeginCreateProject(); };
    menu_callbacks.begin_open_project = [this]() { BeginOpenProject(); };
    menu_callbacks.save_project = [this]() { return SaveProject(); };
    menu_callbacks.request_exit = [this](bool& done_ref) { RequestExit(done_ref); };
    menu_callbacks.begin_create_project_sacm_file = [this]() { BeginCreateProjectSacmFile(); };
    menu_callbacks.begin_create_project_evidence_register = [this]() { BeginCreateProjectEvidenceRegister(); };
    menu_callbacks.begin_create_project_j3377_cae_register = [this]() { BeginCreateProjectJ3377CaeRegister(); };
    const float menu_height = frame::RenderAppMenuBar(*impl_, done, menu_callbacks);

    RebuildDerivedViewsIfNeeded();
    ProcessPendingProposalCreatorPreviewRefresh();
    PollAiReviewTask();

    const frame::AppLayoutRegions regions = frame::RenderAppShell(*impl_, menu_height, kPanelFlags);

    areas::ProjectExplorerAreaCallbacks project_explorer_callbacks{
        [this]() { RefreshSacmPackageTreeCache(); },
        [this]() { BeginCreateProjectSacmFile(); },
        [this]() { BeginCreateProjectEvidenceRegister(); },
        [this]() { BeginCreateProjectJ3377CaeRegister(); },
        [this](const core::ProjectFileEntry& entry) { OpenProjectFile(entry); },
        [this](const core::ProjectFileEntry& entry, const sacm::SacmPackageTreeNode& node) {
            OpenProjectPackageNode(entry, node);
        },
        [this](const core::ProjectFileEntry& entry, const sacm::SacmPackageTreeNode& node) {
            BeginAddTerminologyPackage(entry, node);
        },
    };

    areas::ArgumentNavigatorAreaCallbacks argument_navigator_callbacks{
        [this]() { return MakeElementContextActions(*this); },
        [this](core::NewElementKind kind) { AddProposalChildToSelected(kind); },
        [this]() { AddProposalTopGoal(); },
        [this](core::RemoveMode mode) { RemoveProposalSelected(mode); },
        [this](const char* feature) {
            if (feature)
                ShowNotImplementedModal(feature);
        },
        ui::TreeEditActions{
            [this](const std::string& dragged_id, const std::string& target_id, core::TreeDropMode drop_mode) {
                return ValidateTreeDrop(dragged_id, target_id, drop_mode);
            },
            [this](const std::string& dragged_id, const std::string& target_id, core::TreeDropMode drop_mode) {
                return PerformTreeDrop(dragged_id, target_id, drop_mode);
            },
        },
    };

    areas::RenderProjectExplorerArea(*impl_, regions.project_explorer, kPanelFlags, project_explorer_callbacks);
    areas::RenderArgumentNavigatorArea(*impl_, regions.argument_navigator, kPanelFlags, argument_navigator_callbacks);
    areas::RenderWorkbenchArea(*impl_, regions.workbench, kPanelFlags, MakeWorkbenchAreaCallbacks());

    areas::ReviewPanelAreaCallbacks review_panel_callbacks;
    review_panel_callbacks.ensure_review_item_storage = [this]() { return EnsureReviewItemStorage(); };
    review_panel_callbacks.set_status = [this](const std::string& message) { SetStatus(message); };
    review_panel_callbacks.create_proposed_change = [this](const core::reviews::ReviewItem& item) {
        return BeginProposalForReviewItem(item);
    };
    review_panel_callbacks.save_proposal = [this](const core::reviews::ReviewItem& item) {
        return SaveActiveProposal(item);
    };
    review_panel_callbacks.edit_proposal = [this](const core::reviews::ReviewItem& item) {
        return BeginEditProposalForReviewItem(item);
    };
    review_panel_callbacks.preview_proposal_by_id = [this](const std::string& proposal_id) {
        return PreviewProposalById(proposal_id);
    };
    review_panel_callbacks.apply_proposal = [this](const core::reviews::ReviewItem& item) {
        ApplyReviewProposal(item);
    };
    review_panel_callbacks.delete_proposal = [this](const core::reviews::ReviewItem& item) {
        actions::ReviewActions(*impl_).DeleteProposalForReviewItem(item);
    };
    review_panel_callbacks.resolve_review_item = [this](const core::reviews::ReviewItem& item) {
        return ResolveReviewItem(item);
    };
    review_panel_callbacks.delete_review_item = [this](const core::reviews::ReviewItem& item) {
        BeginDeleteReviewItem(item);
    };
    review_panel_callbacks.sync_review_visual_states = [this]() { SyncReviewVisualStatesFromReviews(); };
    review_panel_callbacks.set_manual_review_ok = [this](const std::string& element_id, bool manual_ok) {
        return SetManualReviewOk(element_id, manual_ok);
    };

    areas::FeedbackDockAreaCallbacks feedback_dock_callbacks;
    feedback_dock_callbacks.problems.activate_problem = [this](const core::ProblemItem& problem) {
        if (problem.type.rfind("TerminologyTerm", 0) == 0) {
            HandleProblemQuickFix(problem);
            return;
        }
        if (problem.element_id.empty())
            return;
        ui::GetUiState().selected_problem_element_id = problem.element_id;
        impl_->events.Emit(SelectionChangedEvent{problem.element_id, true});
        impl_->events.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
    };
    feedback_dock_callbacks.problems.quick_fix_problem = [this](const core::ProblemItem& problem) {
        HandleProblemQuickFix(problem);
    };
    feedback_dock_callbacks.term_usages.activate_usage = [this](std::size_t usage_index) {
        NavigateToTerminologyUsage(usage_index);
    };
    feedback_dock_callbacks.render_review_content = [this, &review_panel_callbacks]() {
        areas::RenderReviewPanelContent(*impl_, review_panel_callbacks);
    };
    feedback_dock_callbacks.render_ai_debug_content = [this]() { areas::RenderAiDebugPanelContent(*impl_); };
    areas::RenderFeedbackDockArea(*impl_, regions.feedback_dock, kPanelFlags, feedback_dock_callbacks);

    areas::InspectorAreaCallbacks inspector_callbacks{
        [this]() { RenderProposalElementEditor(); },
        [this](const std::string& element_id, const std::string& term_value) {
            BeginQuickDefineTerminologyTerm(element_id, term_value);
        },
        [this](const std::string& element_id, const std::string& term_value) {
            BeginLinkExistingTerminologyTerm(element_id, term_value);
        },
        [this](const std::string& element_id,
               const core::TerminologyPackageRef& package_ref,
               const core::TerminologyTermRef& term_ref) {
            AddTerminologyTermAsContextFromCanvas(element_id, package_ref, term_ref);
        },
        [this](const std::string& element_id, const std::string& term_value) {
            IgnoreTerminologySuggestion(element_id, term_value);
        },
        [this](const std::string& element_id, const std::string& term_value) {
            return IsTerminologySuggestionIgnored(element_id, term_value);
        },
        [this]() {
            impl_->events.Emit(TreeDirtyEvent{});
            impl_->events.Emit(DocumentDirtyEvent{});
        },
    };
    areas::RenderInspectorArea(*impl_, regions.inspector, kPanelFlags, inspector_callbacks);

    areas::ModalHostCallbacks modal_callbacks;
    modal_callbacks.begin_create_project = [this]() { BeginCreateProject(); };
    modal_callbacks.begin_open_project = [this]() { BeginOpenProject(); };
    modal_callbacks.try_open_project_manifest = [this](const std::string& selected_path) {
        return TryOpenProjectManifest(selected_path);
    };
    modal_callbacks.open_first_project_sacm_file = [this]() { return OpenFirstProjectSacmFile(); };
    modal_callbacks.ensure_review_item_storage = [this]() { return EnsureReviewItemStorage(); };
    modal_callbacks.touch_current_project_recent = [this]() { TouchCurrentProjectRecent(); };
    modal_callbacks.save_project = [this]() { return SaveProject(); };
    modal_callbacks.set_status = [this](const std::string& message) { SetStatus(message); };
    modal_callbacks.delete_review_item = [this](const core::reviews::ReviewItem& item) {
        return DeleteReviewItem(item);
    };
    modal_callbacks.confirm_add_terminology_package = [this]() { ConfirmAddTerminologyPackage(); };
    modal_callbacks.confirm_delete_terminology_package = [this]() { ConfirmDeleteTerminologyPackage(); };
    modal_callbacks.confirm_terminology_term_edit = [this]() { ConfirmTerminologyTermEdit(); };
    modal_callbacks.confirm_quick_define_terminology_term = [this](bool add_as_context) {
        ConfirmQuickDefineTerminologyTerm(add_as_context);
    };
    modal_callbacks.confirm_delete_terminology_term = [this]() { ConfirmDeleteTerminologyTerm(); };
    modal_callbacks.confirm_terminology_category_edit = [this]() { ConfirmTerminologyCategoryEdit(); };
    modal_callbacks.confirm_delete_terminology_category = [this]() { ConfirmDeleteTerminologyCategory(); };
    areas::RenderModalHost(*impl_, done, modal_callbacks);
}

} // namespace app
