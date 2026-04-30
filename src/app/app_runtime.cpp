#include "app/app_runtime.h"
#include "app/app_layout_controller.h"
#include "app/app_runtime_state.h"

#include "app/project_workflow.h"
#include "app/recent_projects.h"

#include "ai/ai_service.h"
#include "ai/ai_task_runner.h"
#include "ai/secret_store.h"
#include "hello_imgui/hello_imgui.h"
#include "hello_imgui/hello_imgui_theme.h"
#include "imgui.h"

#include "core/app_state.h"
#include "core/element_factory.h"
#include "core/problems/problems_manager.h"
#include "core/project_service.h"
#include "core/reviews/review_item_manager.h"
#include "core/reviews/review_proposal_manager.h"
#include "core/reviews/review_proposal_patch_service.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/localization.h"
#include "ui/panels/element_panel.h"
#include "ui/panels/problems_panel.h"
#include "ui/panels/preferences_panel.h"
#include "ui/panels/project_files_panel.h"
#include "ui/panels/review_panel.h"
#include "ui/panels/sacm_viewer_panel.h"
#include "ui/register_views.h"
#include "ui/tree_view.h"
#include "ui/ui_state.h"
#include "ui/widgets/splitter.h"

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

constexpr float kSplitterThickness = 4.0f;

const ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoMove
                                   | ImGuiWindowFlags_NoResize
                                   | ImGuiWindowFlags_NoCollapse
                                   | ImGuiWindowFlags_NoBringToFrontOnFocus
                                   | ImGuiWindowFlags_NoSavedSettings;

void CopyToBuffer(char* buffer, size_t buffer_size, const std::string& value) {
    if (!buffer || buffer_size == 0) return;
    size_t count = std::min(buffer_size - 1, value.size());
    std::memcpy(buffer, value.data(), count);
    buffer[count] = '\0';
}

std::string TrimWhitespace(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) ++begin;
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) --end;
    return std::string(begin, end);
}

void RenderLanguageMenu() {
    if (!ImGui::BeginMenu(ui::Tr(ui::MessageId::Language))) return;

    const ui::Language current = ui::CurrentLanguage();
    if (ImGui::MenuItem(ui::Tr(ui::MessageId::English), nullptr, current == ui::Language::English)) {
        ui::SetCurrentLanguage(ui::Language::English);
    }
    if (ImGui::MenuItem(ui::Tr(ui::MessageId::Japanese), nullptr, current == ui::Language::Japanese)) {
        ui::SetCurrentLanguage(ui::Language::Japanese);
    }

    ImGui::EndMenu();
}

void RenderThemeMenu() {
    if (!ImGui::BeginMenu(ui::Tr(ui::MessageId::Theme))) return;

    HelloImGui::RunnerParams* runner_params = HelloImGui::GetRunnerParams();
    if (!runner_params) {
        ImGui::EndMenu();
        return;
    }

    for (int i = 0; i < ImGuiTheme::ImGuiTheme_Count; ++i) {
        auto theme = static_cast<ImGuiTheme::ImGuiTheme_>(i);
        bool selected = runner_params->imGuiWindowParams.tweakedTheme.Theme == theme;
        if (ImGui::MenuItem(ImGuiTheme::ImGuiTheme_Name(theme), nullptr, selected)) {
            runner_params->imGuiWindowParams.tweakedTheme.Theme = theme;
            ImGuiTheme::ApplyTweakedTheme(runner_params->imGuiWindowParams.tweakedTheme);
        }
    }

    ImGui::EndMenu();
}

std::string NowUtcString() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    if (gmtime_s(&utc, &time) != 0) return "1970-01-01T00:00:00Z";
#else
    if (!gmtime_r(&time, &utc)) return "1970-01-01T00:00:00Z";
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string GenerateReviewItemId() {
    static unsigned long long counter = 0;
    auto ticks = std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream out;
    out << "review-" << std::hex << ticks << "-" << ++counter;
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
    if (value.size() <= limit) return value;
    return value.substr(0, limit) + "...";
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

core::reviews::PatchOperationType CreateOperationFor(core::NewElementKind kind) {
    switch (kind) {
        case core::NewElementKind::Goal: return core::reviews::PatchOperationType::CreateClaim;
        case core::NewElementKind::Strategy: return core::reviews::PatchOperationType::CreateStrategy;
        case core::NewElementKind::Solution: return core::reviews::PatchOperationType::CreateSolution;
        case core::NewElementKind::Context: return core::reviews::PatchOperationType::CreateContext;
        case core::NewElementKind::Assumption: return core::reviews::PatchOperationType::CreateAssumption;
        case core::NewElementKind::Justification: return core::reviews::PatchOperationType::CreateJustification;
    }
    return core::reviews::PatchOperationType::CreateClaim;
}

const char* CreateRefPrefixFor(core::NewElementKind kind) {
    switch (kind) {
        case core::NewElementKind::Goal: return "$new_claim_";
        case core::NewElementKind::Strategy: return "$new_strategy_";
        case core::NewElementKind::Solution: return "$new_solution_";
        case core::NewElementKind::Context: return "$new_context_";
        case core::NewElementKind::Assumption: return "$new_assumption_";
        case core::NewElementKind::Justification: return "$new_justification_";
    }
    return "$new_element_";
}

bool IsContextLike(core::NewElementKind kind) {
    return kind == core::NewElementKind::Context ||
           kind == core::NewElementKind::Assumption ||
           kind == core::NewElementKind::Justification;
}

const char* RemoveModeField(core::RemoveMode mode) {
    return mode == core::RemoveMode::NodeAndDescendants
        ? core::reviews::kReviewProposalRemoveModeNodeAndDescendants
        : core::reviews::kReviewProposalRemoveModeNodeOnly;
}

std::string GenerateCreateRef(const core::reviews::ReviewProposal& proposal, core::NewElementKind kind) {
    std::unordered_set<std::string> used;
    for (const core::reviews::PatchOperation& operation : proposal.operations) {
        if (operation.create_ref.has_value()) used.insert(operation.create_ref.value());
    }
    const std::string prefix = CreateRefPrefixFor(kind);
    for (int i = 1; i < 100000; ++i) {
        std::string candidate = prefix + std::to_string(i);
        if (used.count(candidate) == 0) return candidate;
    }
    return prefix + std::to_string(used.size() + 1);
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

std::optional<core::reviews::ElementRef> ProposalRefForPreviewId(const std::string& preview_id,
                                                        const std::map<std::string, std::string>& generated_ids) {
    for (const auto& generated : generated_ids) {
        if (generated.second == preview_id) return CreatedElementRef(generated.first);
    }
    if (!preview_id.empty()) return ExistingElementRef(preview_id);
    return std::nullopt;
}

std::string PreviewIdForProposalRef(const core::reviews::ElementRef& ref,
                                    const std::map<std::string, std::string>& generated_ids) {
    if (ref.existing_id.has_value()) return ref.existing_id.value();
    if (ref.create_ref.has_value()) {
        auto found = generated_ids.find(ref.create_ref.value());
        if (found != generated_ids.end()) return found->second;
    }
    return {};
}

void TrackAffectedExistingElement(core::reviews::ReviewProposal& proposal,
                                  const parser::AssuranceCase& base_model,
                                  const std::string& element_id) {
    if (element_id.empty()) return;
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
    if (ref.existing_id.has_value()) TrackAffectedExistingElement(proposal, base_model, ref.existing_id.value());
}

void AddHighlightRef(std::unordered_set<std::string>& ids,
                     const core::reviews::ElementRef& ref,
                     const std::map<std::string, std::string>& generated_ids) {
    if (ref.existing_id.has_value() && !ref.existing_id->empty()) {
        ids.insert(ref.existing_id.value());
    }
    if (ref.create_ref.has_value()) {
        auto found = generated_ids.find(ref.create_ref.value());
        if (found != generated_ids.end() && !found->second.empty()) ids.insert(found->second);
    }
}

bool IsPreviewRelationshipType(const std::string& type) {
    return type == "assertedinference" || type == "assertedcontext" || type == "assertedevidence";
}

bool RelationshipTouchesAny(const parser::SacmElement& relationship,
                            const std::unordered_set<std::string>& ids) {
    if (!IsPreviewRelationshipType(relationship.type)) return false;
    if (ids.count(relationship.reasoning_ref) > 0) return true;
    for (const std::string& source : relationship.source_refs) {
        if (ids.count(source) > 0) return true;
    }
    for (const std::string& target : relationship.target_refs) {
        if (ids.count(target) > 0) return true;
    }
    return false;
}

bool SameRelationship(const parser::SacmElement& lhs, const parser::SacmElement& rhs) {
    return lhs.type == rhs.type &&
           lhs.reasoning_ref == rhs.reasoning_ref &&
           lhs.source_refs == rhs.source_refs &&
           lhs.target_refs == rhs.target_refs;
}

bool RelationshipExists(const parser::AssuranceCase& model, const parser::SacmElement& relationship) {
    for (const parser::SacmElement& element : model.elements) {
        if (!IsPreviewRelationshipType(element.type)) continue;
        if (!relationship.id.empty() && element.id == relationship.id) return true;
        if (SameRelationship(element, relationship)) return true;
    }
    return false;
}

std::optional<core::RemoveMode> ProposalRemoveModeFromField(const std::string& field) {
    if (field == core::reviews::kReviewProposalRemoveModeNodeOnly) return core::RemoveMode::NodeOnly;
    if (field == core::reviews::kReviewProposalRemoveModeNodeAndDescendants) return core::RemoveMode::NodeAndDescendants;
    return std::nullopt;
}

std::unordered_set<std::string> CollectProposalRemovedExistingIds(
    const core::reviews::ReviewProposal& proposal,
    const parser::AssuranceCase& base_model,
    const std::map<std::string, std::string>& generated_ids) {
    std::unordered_set<std::string> ids;
    for (const core::reviews::PatchOperation& operation : proposal.operations) {
        if (operation.type != core::reviews::PatchOperationType::RemoveElement || !operation.element.has_value()) continue;
        const std::string element_id = PreviewIdForProposalRef(operation.element.value(), generated_ids);
        if (element_id.empty() || !FindParserElement(base_model, element_id)) continue;

        std::optional<core::RemoveMode> mode = ProposalRemoveModeFromField(operation.field);
        if (mode.has_value()) {
            std::unordered_set<std::string> planned = core::PlanRemoval(base_model, element_id, mode.value());
            ids.insert(planned.begin(), planned.end());
        } else {
            ids.insert(element_id);
        }
    }
    return ids;
}

void RestoreRemovedExistingElementsForProposalPreview(
    parser::AssuranceCase& preview_model,
    const parser::AssuranceCase& base_model,
    const std::unordered_set<std::string>& removed_ids) {
    if (removed_ids.empty()) return;

    for (const parser::SacmElement& element : base_model.elements) {
        if (IsPreviewRelationshipType(element.type)) continue;
        if (removed_ids.count(element.id) == 0) continue;
        if (FindParserElement(preview_model, element.id)) continue;
        preview_model.elements.push_back(element);
    }

    for (const parser::SacmElement& relationship : base_model.elements) {
        if (!RelationshipTouchesAny(relationship, removed_ids)) continue;
        if (RelationshipExists(preview_model, relationship)) continue;
        preview_model.elements.push_back(relationship);
    }
}

std::unordered_set<std::string> CollectProposalHighlightIds(const core::reviews::ReviewProposal& proposal,
                                                           const std::map<std::string, std::string>& generated_ids) {
    std::unordered_set<std::string> ids;
    if (!proposal.anchor_element_id.empty()) ids.insert(proposal.anchor_element_id);
    for (const std::string& id : proposal.affected_existing_element_ids) {
        if (!id.empty()) ids.insert(id);
    }
    for (const auto& generated : generated_ids) {
        if (!generated.second.empty()) ids.insert(generated.second);
    }
    for (const core::reviews::PatchOperation& operation : proposal.operations) {
        if (operation.element.has_value()) AddHighlightRef(ids, operation.element.value(), generated_ids);
        if (operation.source.has_value()) AddHighlightRef(ids, operation.source.value(), generated_ids);
        if (operation.target.has_value()) AddHighlightRef(ids, operation.target.value(), generated_ids);
    }
    return ids;
}

void ClearProposalHighlightState(ui::UiState& ui_state) {
    ui_state.proposal_highlight_ids.clear();
    ui_state.marked_for_removal.clear();
    ui_state.center_on_marked = false;
    ui_state.dim_non_proposal_nodes = false;
}

void ApplyProposalPreviewVisualState(ui::UiState& ui_state,
                                     parser::AssuranceCase& preview_model,
                                     const parser::AssuranceCase& base_model,
                                     const core::reviews::ReviewProposal& proposal,
                                     const std::map<std::string, std::string>& generated_ids) {
    std::unordered_set<std::string> removed_ids = CollectProposalRemovedExistingIds(proposal, base_model, generated_ids);
                                    // The patch service removes nodes from the preview model; restore them so the canvas can mark removals explicitly.
    RestoreRemovedExistingElementsForProposalPreview(preview_model, base_model, removed_ids);

    ui_state.proposal_highlight_ids = CollectProposalHighlightIds(proposal, generated_ids);
    ui_state.proposal_highlight_ids.insert(removed_ids.begin(), removed_ids.end());
    ui_state.marked_for_removal = std::move(removed_ids);
    ui_state.dim_non_proposal_nodes = !ui_state.proposal_highlight_ids.empty();
}

bool IsUpdateForElement(const core::reviews::PatchOperation& operation,
                        core::reviews::PatchOperationType type,
                        const core::reviews::ElementRef& ref,
                        const std::string& field) {
    return operation.type == type &&
           operation.element.has_value() &&
           SameElementRef(operation.element.value(), ref) &&
           operation.field == field;
}

void UpsertElementUpdate(core::reviews::ReviewProposal& proposal,
                         core::reviews::PatchOperationType type,
                         const core::reviews::ElementRef& ref,
                         const std::string& field,
                         const std::string& old_value,
                         const std::string& new_value) {
    proposal.operations.erase(
        std::remove_if(proposal.operations.begin(), proposal.operations.end(), [&](const core::reviews::PatchOperation& operation) {
            return IsUpdateForElement(operation, type, ref, field);
        }),
        proposal.operations.end());

    if (old_value == new_value) return;

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
        std::remove_if(proposal.operations.begin(), proposal.operations.end(), [&](const core::reviews::PatchOperation& operation) {
            if (operation.type != core::reviews::PatchOperationType::SetUndeveloped &&
                operation.type != core::reviews::PatchOperationType::ClearUndeveloped) {
                return false;
            }
            return operation.element.has_value() && SameElementRef(operation.element.value(), ref);
        }),
        proposal.operations.end());

    if (old_value == new_value) return;

    core::reviews::PatchOperation operation;
    operation.type = new_value ? core::reviews::PatchOperationType::SetUndeveloped : core::reviews::PatchOperationType::ClearUndeveloped;
    operation.element = ref;
    proposal.operations.push_back(std::move(operation));
}

std::string EditableTextFor(const parser::SacmElement& element) {
    return (element.type == "claim" || element.type == "argumentreasoning")
               ? element.content
               : element.description;
}

const char* EditableTextFieldFor(const parser::SacmElement& element) {
    return (element.type == "claim" || element.type == "argumentreasoning") ? "content" : "description";
}

void CopyCommonSacmFields(sacm::SacmElement& target, const parser::SacmElement& source) {
    target.id = source.id;
    target.name = source.name;
    target.description = source.description;
    target.name_ml.texts = source.name_langs;
    target.description_ml.texts = source.description_langs;
    if (target.name_ml.texts.empty() && !source.name.empty()) target.name_ml.set("en", source.name);
    if (target.description_ml.texts.empty() && !source.description.empty()) target.description_ml.set("en", source.description);
}

void RebuildSacmArgumentPackageFromParser(const parser::AssuranceCase& model, sacm::AssuranceCasePackage& package) {
    if (package.argumentPackages.empty()) package.argumentPackages.emplace_back();
    for (sacm::ArgumentPackage& argument_package : package.argumentPackages) {
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
            if (claim.content_ml.texts.empty() && !element.content.empty()) claim.content_ml.set("en", element.content);
            claim.assertionDeclaration = element.assertion_declaration;
            claim.undeveloped = element.undeveloped;
            argument_package.claims.push_back(std::move(claim));
        } else if (element.type == "argumentreasoning") {
            sacm::ArgumentReasoning reasoning;
            CopyCommonSacmFields(reasoning, element);
            reasoning.content = element.content;
            reasoning.content_ml.texts = element.content_langs;
            if (reasoning.content_ml.texts.empty() && !element.content.empty()) reasoning.content_ml.set("en", element.content);
            reasoning.undeveloped = element.undeveloped;
            argument_package.argumentReasonings.push_back(std::move(reasoning));
        } else if (element.type == "artifactreference" || element.type == "artifact") {
            sacm::ArtifactReference artifact_reference;
            CopyCommonSacmFields(artifact_reference, element);
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
}

}  // namespace

ui::ElementContextActions MakeElementContextActions(AppRuntime& runtime) {
    return ui::ElementContextActions{
        [&runtime](core::NewElementKind kind) { runtime.AddChildToSelected(kind); },
        [&runtime]() { runtime.AddTopGoal(); },
        [&runtime](core::RemoveMode mode) { runtime.RemoveSelected(mode); },
        [&runtime](const char* feature) {
            if (feature) runtime.ShowNotImplementedModal(feature);
        },
    };
}

AppRuntime::AppRuntime() : impl_(new AppRuntimeState()) {
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

void AppRuntime::RequestClose() {
    impl_->close_requested = true;
}

bool AppRuntime::AddChildToSelected(core::NewElementKind kind) {
    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("No assurance case loaded.");
        return false;
    }
    const std::string& selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        SetStatus("No element selected.");
        return false;
    }

    parser::AssuranceCase& ac = impl_->app_state.loaded_case.value();
    sacm::AssuranceCasePackage* pkg = impl_->app_state.sacm_package.has_value()
                                          ? &impl_->app_state.sacm_package.value()
                                          : nullptr;

    std::string new_id;
    std::string error;
    if (!core::AddChildElement(ac, pkg, selected_id, kind, new_id, error)) {
        SetStatus("Add failed: " + error);
        return false;
    }

    impl_->tree_needs_rebuild = true;
    ui::UiState& s = ui::GetUiState();
    s.selected_element_id = new_id;
    s.center_on_selection = true;
    impl_->document_dirty = true;
    impl_->app_state.mark_dirty();
    SetStatus("Added " + new_id);
    return true;
}

bool AppRuntime::AddTopGoal() {
    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("No assurance case loaded.");
        return false;
    }

    parser::AssuranceCase& ac = impl_->app_state.loaded_case.value();
    sacm::AssuranceCasePackage* pkg = impl_->app_state.sacm_package.has_value()
                                          ? &impl_->app_state.sacm_package.value()
                                          : nullptr;

    std::string new_id;
    std::string error;
    if (!core::AddTopGoal(ac, pkg, new_id, error)) {
        SetStatus("Add failed: " + error);
        return false;
    }

    impl_->tree_needs_rebuild = true;
    ui::UiState& s = ui::GetUiState();
    s.selected_element_id = new_id;
    s.center_on_selection = true;
    impl_->document_dirty = true;
    impl_->app_state.mark_dirty();
    SetStatus("Added " + new_id);
    return true;
}

void AppRuntime::RemoveSelected(core::RemoveMode mode) {
    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("No assurance case loaded.");
        return;
    }
    const std::string& selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        SetStatus("No element selected.");
        return;
    }

    parser::AssuranceCase& ac = impl_->app_state.loaded_case.value();
    auto planned = core::PlanRemoval(ac, selected_id, mode);
    if (planned.empty()) {
        SetStatus("Nothing to remove for this selection.");
        return;
    }

    sacm::AssuranceCasePackage* pkg = impl_->app_state.sacm_package.has_value()
                                          ? &impl_->app_state.sacm_package.value()
                                          : nullptr;

    // Single-element removal: act immediately, no confirmation.
    if (planned.size() == 1) {
        std::string error;
        if (!core::RemoveElement(ac, pkg, selected_id, mode, error)) {
            SetStatus("Remove failed: " + error);
            return;
        }
        impl_->tree_needs_rebuild = true;
        ui::GetUiState().selected_element_id.clear();
        impl_->document_dirty = true;
        impl_->app_state.mark_dirty();
        SetStatus("Removed " + selected_id);
        return;
    }

    // Multi-element removal: stash the plan, mark nodes on the canvas, request
    // a fit-to-view of the marked set, and open the confirmation modal.
    impl_->show_remove_confirm = true;
    impl_->pending_remove_id = selected_id;
    impl_->pending_remove_mode = mode;
    impl_->pending_remove_ids.assign(planned.begin(), planned.end());

    auto& s = ui::GetUiState();
    s.marked_for_removal = std::move(planned);
    s.center_on_marked = true;
}

void AppRuntime::SetStatus(const std::string& message) {
    impl_->app_state.status_message = message;
}

void AppRuntime::ShowNotImplementedModal(const std::string& feature) {
    impl_->show_not_implemented_modal = true;
    impl_->not_implemented_feature = feature;
}

bool AppRuntime::RefreshProposalCreatorPreview() {
    if (!impl_->proposal_creator_active) return false;
    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("Load a SACM model before editing proposal drafts.");
        return false;
    }

    core::reviews::ReviewProposalPatchService patch_service;
    core::reviews::ProposalPreviewResult preview = patch_service.BuildPreviewModel(
        impl_->proposal_draft,
        impl_->app_state.loaded_case.value());
    if (!preview.success) {
        SetStatus("Proposal draft preview failed: " + preview.error);
        return false;
    }

    impl_->proposal_preview_active = false;
    impl_->proposal_preview_id = impl_->proposal_draft.id;
    impl_->proposal_preview_model = std::move(preview.preview_model);
    impl_->proposal_creator_generated_ids = std::move(preview.generated_ids);
    ui::UiState& ui_state = ui::GetUiState();
    ApplyProposalPreviewVisualState(ui_state,
                                    impl_->proposal_preview_model,
                                    impl_->app_state.loaded_case.value(),
                                    impl_->proposal_draft,
                                    impl_->proposal_creator_generated_ids);
    impl_->current_tree = ui::gsn::BuildAssuranceTree(impl_->proposal_preview_model);
    ui::gsn::SetCanvasTree(impl_->current_tree);
    return true;
}

void AppRuntime::ProcessPendingProposalCreatorPreviewRefresh() {
    if (!impl_->proposal_creator_preview_refresh_pending) return;

    impl_->proposal_creator_preview_refresh_pending = false;
    const std::optional<std::string> select_create_ref = impl_->proposal_creator_pending_select_create_ref;
    const bool clear_selection = impl_->proposal_creator_pending_clear_selection;
    impl_->proposal_creator_pending_select_create_ref.reset();
    impl_->proposal_creator_pending_clear_selection = false;

    if (!RefreshProposalCreatorPreview()) return;

    ui::UiState& ui_state = ui::GetUiState();
    if (select_create_ref.has_value()) {
        ui_state.selected_element_id = PreviewIdForProposalRef(CreatedElementRef(select_create_ref.value()),
                                                               impl_->proposal_creator_generated_ids);
        ui_state.center_on_selection = !ui_state.selected_element_id.empty();
    } else if (clear_selection) {
        ui_state.selected_element_id.clear();
    }
}

bool AppRuntime::BeginProposalForReviewItem(const core::reviews::ReviewItem& item) {
    if (impl_->proposal_creator_active) {
        SetStatus("Save or discard the active proposal before creating another one.");
        return false;
    }
    if (item.status != core::reviews::ReviewItemStatus::Open) {
        SetStatus("Resolved review comments cannot create proposed changes.");
        return false;
    }
    if (item.proposal_id.has_value()) {
        SetStatus("This review comment already has a proposed change.");
        return false;
    }
    if (!impl_->app_state.current_project.has_value() || !impl_->app_state.loaded_case.has_value()) {
        SetStatus("Open a project and SACM file before creating proposed changes.");
        return false;
    }

    const parser::SacmElement* anchor = FindParserElement(impl_->app_state.loaded_case.value(), item.element_id);
    if (!anchor) {
        SetStatus("The reviewed element no longer exists in the loaded model.");
        return false;
    }

    impl_->proposal_draft = BuildDraftReviewProposal(item, impl_->app_state.loaded_case.value(), *anchor);
    if (!impl_->reviewer_name.empty()) impl_->proposal_draft.author_name = impl_->reviewer_name;
    impl_->proposal_creator_active = true;
    impl_->proposal_creator_generated_ids.clear();
    if (!RefreshProposalCreatorPreview()) {
        CancelActiveProposal();
        return false;
    }

    ui::UiState& ui_state = ui::GetUiState();
    ui_state.center_view = ui::CenterView::GsnCanvas;
    ui_state.selected_element_id = anchor->id;
    ui_state.center_on_selection = true;
    impl_->force_center_tab_selection = true;
    SetStatus("Building proposal " + impl_->proposal_draft.id + ". Use the GSN canvas and Save Proposal when ready.");
    return true;
}

bool AppRuntime::BeginEditProposalForReviewItem(const core::reviews::ReviewItem& item) {
    if (impl_->proposal_creator_active) {
        SetStatus("Save or discard the active proposal before editing another one.");
        return false;
    }
    if (item.status != core::reviews::ReviewItemStatus::Open) {
        SetStatus("Resolved review comments cannot edit proposed changes.");
        return false;
    }
    if (!item.proposal_id.has_value()) {
        SetStatus("This review comment has no proposed change to edit.");
        return false;
    }
    if (!impl_->app_state.current_project.has_value() || !impl_->app_state.loaded_case.has_value()) {
        SetStatus("Open a project and SACM file before editing proposed changes.");
        return false;
    }

    std::string error;
    std::optional<core::reviews::ReviewProposal> proposal = impl_->review_proposal_manager.LoadProposal(item.proposal_id.value(), error);
    if (!proposal.has_value()) {
        SetStatus("Proposal edit failed: " + error);
        return false;
    }
    if (proposal->review_item_id != item.id) {
        SetStatus("Proposal edit failed: the proposal belongs to a different review comment.");
        return false;
    }

    impl_->proposal_draft = std::move(proposal.value());
    if (!impl_->reviewer_name.empty()) impl_->proposal_draft.author_name = impl_->reviewer_name;
    impl_->proposal_creator_active = true;
    impl_->proposal_creator_generated_ids.clear();
    if (!RefreshProposalCreatorPreview()) {
        CancelActiveProposal();
        return false;
    }

    ui::UiState& ui_state = ui::GetUiState();
    ui_state.center_view = ui::CenterView::GsnCanvas;
    ui_state.selected_element_id = impl_->proposal_draft.anchor_element_id;
    ui_state.center_on_selection = !ui_state.selected_element_id.empty();
    impl_->force_center_tab_selection = true;
    SetStatus("Editing proposal " + impl_->proposal_draft.id + ". Use Save Proposal to update it.");
    return true;
}

bool AppRuntime::BeginEditProposalById(const std::string& proposal_id) {
    if (proposal_id.empty()) {
        SetStatus("No proposal id was provided.");
        return false;
    }

    std::string error;
    std::optional<core::reviews::ReviewProposal> proposal = impl_->review_proposal_manager.LoadProposal(proposal_id, error);
    if (!proposal.has_value()) {
        SetStatus("Proposal edit failed: " + error);
        return false;
    }

    std::optional<core::reviews::ReviewItem> item = impl_->review_item_manager.GetItemById(proposal->review_item_id);
    if (!item.has_value()) {
        SetStatus("Proposal edit failed: the owning review comment was not found.");
        return false;
    }
    if (!item->proposal_id.has_value() || item->proposal_id.value() != proposal_id) {
        SetStatus("Proposal edit failed: the owning review comment no longer points to this proposal.");
        return false;
    }

    return BeginEditProposalForReviewItem(item.value());
}

bool AppRuntime::PreviewProposalById(const std::string& proposal_id) {
    if (impl_->proposal_creator_active) {
        SetStatus("Save or discard the active proposal before viewing another proposal.");
        return false;
    }
    if (proposal_id.empty()) {
        SetStatus("No proposal id was provided.");
        return false;
    }

    std::string error;
    std::optional<core::reviews::ReviewProposal> proposal = impl_->review_proposal_manager.LoadProposal(proposal_id, error);
    if (!proposal.has_value()) {
        SetStatus("Proposal preview failed: " + error);
        return false;
    }

    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("Load a SACM model before previewing proposals.");
        return false;
    }

    core::reviews::ReviewProposalPatchService patch_service;
    core::reviews::ProposalPreviewResult preview = patch_service.BuildPreviewModel(*proposal, impl_->app_state.loaded_case.value());
    if (!preview.success) {
        SetStatus("Proposal preview failed: " + preview.error);
        return false;
    }

    const std::map<std::string, std::string> generated_ids = preview.generated_ids;
    impl_->proposal_preview_active = true;
    impl_->proposal_preview_id = proposal->id;
    impl_->proposal_preview_model = std::move(preview.preview_model);

    ui::UiState& preview_ui_state = ui::GetUiState();
    ApplyProposalPreviewVisualState(preview_ui_state,
                                    impl_->proposal_preview_model,
                                    impl_->app_state.loaded_case.value(),
                                    *proposal,
                                    generated_ids);
    impl_->current_tree = ui::gsn::BuildAssuranceTree(impl_->proposal_preview_model);
    ui::gsn::SetCanvasTree(impl_->current_tree);
    preview_ui_state.center_view = ui::CenterView::GsnCanvas;
    preview_ui_state.selected_element_id = proposal->anchor_element_id;
    preview_ui_state.center_on_selection = true;
    impl_->show_gsn_tab = true;
    impl_->force_center_tab_selection = true;

    std::ostringstream status;
    status << "Previewing proposal " << proposal->id << " with " << proposal->operations.size() << " operation(s). ";
    status << "The project model has not been changed.";
    SetStatus(status.str());
    return true;
}

bool AppRuntime::SaveActiveProposal(const core::reviews::ReviewItem& item) {
    if (!impl_->proposal_creator_active || impl_->proposal_draft.review_item_id != item.id) {
        SetStatus("No active proposal draft for this review comment.");
        return false;
    }
    if (impl_->proposal_draft.operations.empty()) {
        SetStatus("Add at least one proposal operation before saving.");
        return false;
    }
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus("Open a project before saving proposals.");
        return false;
    }

    core::AssuranceProject& project = impl_->app_state.current_project.value();
    core::ProjectFileEntry entry;
    std::string error;
    if (!core::ProjectService::SaveReviewProposalFile(
            project,
            impl_->proposal_draft.id,
            core::reviews::SerializeReviewProposal(impl_->proposal_draft),
            entry,
            error)) {
        SetStatus("Proposal save failed: " + error);
        return false;
    }

    if (!impl_->review_item_manager.SetProposal(item.id, impl_->proposal_draft.id)) {
        std::string cleanup_error;
        core::ProjectService::RemoveTrackedFile(project, entry.relativePath, true, cleanup_error);
        SetStatus("Proposal link update failed.");
        return false;
    }
    MarkReviewItemsDirty();

    const std::string saved_id = impl_->proposal_draft.id;
    CancelActiveProposal();
    core::ProjectService::RefreshFileStatus(project);
    SetStatus("Saved proposal " + saved_id + ".");
    return true;
}

void AppRuntime::CancelActiveProposal() {
    impl_->proposal_creator_active = false;
    impl_->proposal_draft = {};
    impl_->proposal_creator_generated_ids.clear();
    impl_->proposal_creator_preview_refresh_pending = false;
    impl_->proposal_creator_pending_select_create_ref.reset();
    impl_->proposal_creator_pending_clear_selection = false;
    impl_->proposal_preview_active = false;
    impl_->proposal_preview_id.clear();
    impl_->proposal_preview_model = {};
    ClearProposalHighlightState(ui::GetUiState());

    if (impl_->app_state.loaded_case.has_value()) {
        impl_->current_tree = ui::gsn::BuildAssuranceTree(impl_->app_state.loaded_case.value());
        ui::gsn::SetCanvasTree(impl_->current_tree);
    } else {
        impl_->tree_needs_rebuild = true;
    }
}

void AppRuntime::MarkReviewItemsDirty() {
    impl_->review_items_dirty = true;
    impl_->app_state.mark_dirty();
}

bool AppRuntime::DeleteProposalPatchFile(const std::string& proposal_id, std::string& error) {
    if (!impl_->app_state.current_project.has_value()) {
        error = "Open a project before deleting proposed changes.";
        return false;
    }

    core::AssuranceProject& project = impl_->app_state.current_project.value();
    const std::filesystem::path relative_path = ReviewProposalRelativePath(proposal_id);
    if (ProjectTracksFile(project, relative_path)) {
        return core::ProjectService::RemoveTrackedFile(project, relative_path, true, error);
    }
    return impl_->review_proposal_manager.DeleteProposal(proposal_id, error);
}

void AppRuntime::CloseProposalPreviewIfOpen(const std::string& proposal_id) {
    if (!impl_->proposal_preview_active || impl_->proposal_preview_id != proposal_id) return;

    impl_->proposal_preview_active = false;
    impl_->proposal_preview_id.clear();
    impl_->proposal_preview_model = {};
    ClearProposalHighlightState(ui::GetUiState());
    if (impl_->app_state.loaded_case.has_value()) {
        impl_->current_tree = ui::gsn::BuildAssuranceTree(impl_->app_state.loaded_case.value());
        ui::gsn::SetCanvasTree(impl_->current_tree);
    } else {
        impl_->tree_needs_rebuild = true;
    }
}

void AppRuntime::BeginDeleteReviewItem(const core::reviews::ReviewItem& item) {
    if (impl_->proposal_creator_active) {
        SetStatus("Save or discard the active proposal before deleting review comments.");
        return;
    }
    if (item.proposal_id.has_value()) {
        impl_->pending_delete_review_item = item;
        impl_->show_delete_review_item_confirm = true;
        return;
    }
    DeleteReviewItem(item);
}

bool AppRuntime::DeleteReviewItem(const core::reviews::ReviewItem& item) {
    if (impl_->proposal_creator_active) {
        SetStatus("Save or discard the active proposal before deleting review comments.");
        return false;
    }
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus("Open a project before deleting review comments.");
        return false;
    }

    core::AssuranceProject& project = impl_->app_state.current_project.value();
    if (item.proposal_id.has_value()) {
        std::string error;
        if (!DeleteProposalPatchFile(item.proposal_id.value(), error)) {
            SetStatus("Review comment delete failed while deleting proposal: " + error);
            return false;
        }
        CloseProposalPreviewIfOpen(item.proposal_id.value());
    }

    if (!impl_->review_item_manager.RemoveItem(item.id)) {
        SetStatus("Review comment was already removed.");
        return false;
    }

    MarkReviewItemsDirty();
    core::ProjectService::RefreshFileStatus(project);
    SetStatus(item.proposal_id.has_value()
        ? "Deleted review comment and proposed change."
        : "Deleted review comment.");
    return true;
}

bool AppRuntime::ResolveReviewItem(const core::reviews::ReviewItem& item) {
    if (impl_->proposal_creator_active) {
        SetStatus("Save or discard the active proposal before resolving review comments.");
        return false;
    }
    if (item.status == core::reviews::ReviewItemStatus::Resolved) {
        SetStatus("Review comment is already resolved.");
        return true;
    }
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus("Open a project before resolving review comments.");
        return false;
    }

    core::reviews::ReviewItem updated = item;
    updated.status = core::reviews::ReviewItemStatus::Resolved;
    updated.updated_utc = NowUtcString();
    if (!impl_->review_item_manager.AddOrUpdateItem(std::move(updated))) {
        SetStatus("Could not resolve review comment.");
        return false;
    }

    MarkReviewItemsDirty();
    SetStatus("Review comment resolved.");
    return true;
}

bool AppRuntime::AddProposalChildToSelected(core::NewElementKind kind) {
    if (!impl_->proposal_creator_active || !impl_->app_state.loaded_case.has_value()) {
        SetStatus("Start a proposal draft before editing proposal changes.");
        return false;
    }

    const std::string selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        SetStatus("Select an element before adding proposal nodes.");
        return false;
    }

    const parser::SacmElement* parent = FindParserElement(impl_->proposal_preview_model, selected_id);
    if (!parent) {
        SetStatus("The selected proposal preview element no longer exists.");
        return false;
    }
    const bool parent_is_container = parent->type == "claim" || parent->type == "argumentreasoning";
    if (!parent_is_container) {
        SetStatus("Cannot add a child to a leaf element (" + parent->type + ").");
        return false;
    }
    if (kind == core::NewElementKind::Strategy && parent->type != "claim") {
        SetStatus("Strategy can only be added under a Claim.");
        return false;
    }

    std::optional<core::reviews::ElementRef> parent_ref = ProposalRefForPreviewId(selected_id, impl_->proposal_creator_generated_ids);
    if (!parent_ref.has_value()) {
        SetStatus("Could not resolve selected element for proposal operation.");
        return false;
    }

    const std::string create_ref = GenerateCreateRef(impl_->proposal_draft, kind);

    core::reviews::PatchOperation create;
    create.type = CreateOperationFor(kind);
    create.create_ref = create_ref;
    impl_->proposal_draft.operations.push_back(std::move(create));

    core::reviews::PatchOperation relationship;
    relationship.type = IsContextLike(kind) ? core::reviews::PatchOperationType::AddInContextOf
                                            : core::reviews::PatchOperationType::AddSupportedBy;
    relationship.source = CreatedElementRef(create_ref);
    relationship.target = parent_ref.value();
    impl_->proposal_draft.operations.push_back(std::move(relationship));

    TrackAffectedRef(impl_->proposal_draft, impl_->app_state.loaded_case.value(), parent_ref.value());

    impl_->proposal_creator_preview_refresh_pending = true;
    impl_->proposal_creator_pending_select_create_ref = create_ref;
    impl_->proposal_creator_pending_clear_selection = false;
    SetStatus("Recorded proposal add operation.");
    return true;
}

bool AppRuntime::AddProposalTopGoal() {
    if (!impl_->proposal_creator_active || !impl_->app_state.loaded_case.has_value()) {
        SetStatus("Start a proposal draft before editing proposal changes.");
        return false;
    }

    const std::string create_ref = GenerateCreateRef(impl_->proposal_draft, core::NewElementKind::Goal);

    core::reviews::PatchOperation create;
    create.type = core::reviews::PatchOperationType::CreateClaim;
    create.create_ref = create_ref;
    impl_->proposal_draft.operations.push_back(std::move(create));

    impl_->proposal_creator_preview_refresh_pending = true;
    impl_->proposal_creator_pending_select_create_ref = create_ref;
    impl_->proposal_creator_pending_clear_selection = false;
    SetStatus("Recorded proposal top goal operation.");
    return true;
}

void AppRuntime::RemoveProposalSelected(core::RemoveMode mode) {
    if (!impl_->proposal_creator_active || !impl_->app_state.loaded_case.has_value()) {
        SetStatus("Start a proposal draft before editing proposal changes.");
        return;
    }

    const std::string selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        SetStatus("Select an element before removing proposal nodes.");
        return;
    }

    std::vector<std::string> planned_ids;
    auto planned = core::PlanRemoval(impl_->proposal_preview_model, selected_id, mode);
    planned_ids.assign(planned.begin(), planned.end());
    std::sort(planned_ids.begin(), planned_ids.end());
    if (planned_ids.empty()) {
        SetStatus("Nothing to remove for this selection.");
        return;
    }

    for (const std::string& id : planned_ids) {
        std::optional<core::reviews::ElementRef> ref = ProposalRefForPreviewId(id, impl_->proposal_creator_generated_ids);
        if (!ref.has_value()) continue;
        TrackAffectedRef(impl_->proposal_draft, impl_->app_state.loaded_case.value(), ref.value());
    }

    std::optional<core::reviews::ElementRef> selected_ref = ProposalRefForPreviewId(selected_id, impl_->proposal_creator_generated_ids);
    if (!selected_ref.has_value()) {
        SetStatus("Could not resolve selected element for proposal removal.");
        return;
    }

    impl_->proposal_draft.operations.erase(
        std::remove_if(impl_->proposal_draft.operations.begin(), impl_->proposal_draft.operations.end(), [&](const core::reviews::PatchOperation& operation) {
            return operation.type == core::reviews::PatchOperationType::RemoveElement &&
                   operation.element.has_value() &&
                   SameElementRef(operation.element.value(), selected_ref.value());
        }),
        impl_->proposal_draft.operations.end());

    core::reviews::PatchOperation remove;
    remove.type = core::reviews::PatchOperationType::RemoveElement;
    remove.element = selected_ref.value();
    remove.field = RemoveModeField(mode);
    impl_->proposal_draft.operations.push_back(std::move(remove));

    impl_->proposal_creator_preview_refresh_pending = true;
    impl_->proposal_creator_pending_select_create_ref.reset();
    impl_->proposal_creator_pending_clear_selection = true;
    SetStatus("Recorded proposal remove operation.");
}

void AppRuntime::ScanDirectory() {
    impl_->xml_files.clear();
    impl_->selected_file_idx = -1;

    std::error_code ec;
    if (!std::filesystem::is_directory(impl_->dir_path_buf, ec)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(impl_->dir_path_buf, ec)) {
        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".xml") {
            impl_->xml_files.push_back(entry.path().string());
        }
    }

    std::sort(impl_->xml_files.begin(), impl_->xml_files.end());

    std::error_code path_ec;
    std::filesystem::path selected_path = std::filesystem::weakly_canonical(std::filesystem::path(impl_->file_path_buf), path_ec);
    if (path_ec) {
        selected_path = std::filesystem::path(impl_->file_path_buf).lexically_normal();
    }

    for (int i = 0; i < static_cast<int>(impl_->xml_files.size()); ++i) {
        std::filesystem::path candidate_path = std::filesystem::weakly_canonical(std::filesystem::path(impl_->xml_files[i]), path_ec);
        if (path_ec) {
            path_ec.clear();
            candidate_path = std::filesystem::path(impl_->xml_files[i]).lexically_normal();
        }

        if (candidate_path == selected_path) {
            impl_->selected_file_idx = i;
            break;
        }
    }
}

void AppRuntime::RebuildDerivedViewsIfNeeded() {
    if (impl_->IsProposalCanvasActive()) {
        return;
    }

    if (impl_->tree_needs_rebuild && !impl_->app_state.loaded_case.has_value()) {
        ui::RebuildRegisterViews(nullptr);
        impl_->tree_needs_rebuild = false;
        return;
    }

    if (!impl_->app_state.loaded_case.has_value() || !impl_->tree_needs_rebuild) {
        return;
    }

    const auto& ac = impl_->app_state.loaded_case.value();
    impl_->current_tree = ui::gsn::BuildAssuranceTree(ac);
    ui::gsn::SetCanvasTree(impl_->current_tree);
    ui::RebuildRegisterViews(&ac);
    ui::GetUiState().model_has_translations = ui::ModelHasTranslations(ac);

    if (impl_->pending_focus_root && impl_->current_tree.root) {
        ui::UiState& ui_state = ui::GetUiState();
        ui_state.selected_element_id = impl_->current_tree.root->id;
        ui_state.center_on_selection = true;
        ui_state.center_view = ui::CenterView::GsnCanvas;
        impl_->force_center_tab_selection = true;
        impl_->pending_focus_root = false;
    }

    impl_->tree_needs_rebuild = false;
}

float AppRuntime::RenderMainMenuBar(bool& done) {
    if (!ImGui::BeginMainMenuBar()) {
        return 0.0f;
    }

    if (ImGui::BeginMenu(ui::Tr(ui::MessageId::FileMenu))) {
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::CreateEmptyProject))) {
            BeginCreateProject();
        }
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::OpenProject))) {
            BeginOpenProject();
        }
        ImGui::Separator();
        bool has_project = impl_->app_state.current_project.has_value();
        if (!has_project) ImGui::BeginDisabled();
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::SaveProject))) {
            SaveProject();
        }
        if (!has_project) ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::Exit))) {
            RequestExit(done);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ui::Tr(ui::MessageId::AddMenu))) {
        bool has_project = impl_->app_state.current_project.has_value();
        if (!has_project) ImGui::BeginDisabled();
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::NewGsnSacmFile))) {
            BeginCreateProjectSacmFile();
        }
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::NewEvidenceRegister))) {
            BeginCreateProjectEvidenceRegister();
        }
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::NewJ3377CaeRegister))) {
            BeginCreateProjectJ3377CaeRegister();
        }
        if (!has_project) ImGui::EndDisabled();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ui::Tr(ui::MessageId::EditMenu))) {
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::Preferences))) {
            impl_->show_preferences_window = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ui::Tr(ui::MessageId::ViewMenu))) {
        ui::UiState& ui_state = ui::GetUiState();
        ImGui::MenuItem(ui::Tr(ui::MessageId::GsnCanvas), nullptr, &impl_->show_gsn_tab);
        ImGui::MenuItem(ui::Tr(ui::MessageId::CseRegister), nullptr, &impl_->show_cse_tab);
        ImGui::MenuItem(ui::Tr(ui::MessageId::EvidenceRegister), nullptr, &impl_->show_evidence_tab);
        NormalizeCenterViewSelection(*impl_, ui_state.center_view);

        ImGui::Separator();
        if (ImGui::BeginMenu(ui::Tr(ui::MessageId::Appearance))) {
            if (ImGui::MenuItem(ui::Tr(ui::MessageId::ThemeTweaks))) {
                impl_->show_theme_tweak_window = true;
            }
            RenderThemeMenu();
            RenderLanguageMenu();
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::WelcomeScreen))) {
            impl_->show_startup_project_window = true;
        }

        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
    return ImGui::GetFrameHeight();
}

void AppRuntime::RenderPreferencesWindow() {
    if (!impl_->show_preferences_window) return;

    bool test_running = false;
    if (impl_->ai_test_task) {
        ai::AiTaskSnapshot snapshot = impl_->ai_test_task->Snapshot();
        test_running = snapshot.state == ai::AiTaskState::Running;
        impl_->ai_connection_status = snapshot.status;
        if (!test_running) {
            impl_->ai_test_task.reset();
            impl_->RefreshStoredAiKeyState();
        }
    }

    ui::panels::PreferencesPanelModel model;
    model.settings = &impl_->ai_settings;
    model.keyStored = impl_->ai_key_stored;
    model.secureStoreAvailable = impl_->ai_secure_store_available;
    model.testRunning = test_running;
    model.connectionStatus = impl_->ai_connection_status;
    model.apiKeyBuffer = impl_->ai_api_key_buf;
    model.apiKeyBufferSize = sizeof(impl_->ai_api_key_buf);
    model.modelBuffer = impl_->ai_model_buf;
    model.modelBufferSize = sizeof(impl_->ai_model_buf);
    model.reviewerNameBuffer = impl_->reviewer_name_buf;
    model.reviewerNameBufferSize = sizeof(impl_->reviewer_name_buf);
    model.language = ui::CurrentLanguage();

    ui::panels::PreferencesPanelCallbacks callbacks;
    callbacks.save_settings = [this](const ai::AiProviderSettings& settings) {
        impl_->ai_settings = settings;
        if (impl_->ai_settings.model.empty()) impl_->ai_settings.model = ai::kDefaultOpenAiModel;
        std::string error;
        if (!impl_->ai_service->SaveSettings(impl_->ai_settings, error)) {
            impl_->ai_connection_status = ai::ErrorStatus(ai::AiErrorCode::SettingsError, error);
            return;
        }
        CopyToBuffer(impl_->ai_model_buf, sizeof(impl_->ai_model_buf), impl_->ai_settings.model);
        impl_->ai_connection_status = ai::SuccessStatus("AI settings saved.");
    };
    callbacks.save_api_key = [this](const char* api_key) {
        if (!api_key || api_key[0] == '\0') {
            impl_->ai_connection_status = ai::ErrorStatus(ai::AiErrorCode::MissingApiKey, "Enter an API key before saving.");
            return;
        }
        ai::SecretStoreResult result = impl_->ai_service->SaveApiKey(api_key);
        std::memset(impl_->ai_api_key_buf, 0, sizeof(impl_->ai_api_key_buf));
        impl_->RefreshStoredAiKeyState();
        impl_->ai_connection_status = result.success
            ? ai::SuccessStatus("API key saved securely.")
            : ai::ErrorStatus(result.errorCode, result.errorMessage);
    };
    callbacks.remove_api_key = [this]() {
        ai::SecretStoreResult result = impl_->ai_service->DeleteApiKey();
        std::memset(impl_->ai_api_key_buf, 0, sizeof(impl_->ai_api_key_buf));
        impl_->RefreshStoredAiKeyState();
        impl_->ai_connection_status = result.success
            ? ai::SuccessStatus("API key removed.")
            : ai::ErrorStatus(result.errorCode, result.errorMessage);
    };
    callbacks.test_connection = [this]() {
        if (impl_->ai_test_task && impl_->ai_test_task->IsRunning()) return;
        impl_->ai_connection_status = ai::MakeStatus(ai::AiTaskState::Running, ai::AiErrorCode::None, "Testing connection...");
        impl_->ai_settings.model = impl_->ai_model_buf;
        if (impl_->ai_settings.model.empty()) impl_->ai_settings.model = ai::kDefaultOpenAiModel;
        std::string error;
        if (!impl_->ai_service->SaveSettings(impl_->ai_settings, error)) {
            impl_->ai_connection_status = ai::ErrorStatus(ai::AiErrorCode::SettingsError, error);
            return;
        }
        std::shared_ptr<ai::AiService> service = impl_->ai_service;
        impl_->ai_test_task = impl_->ai_task_runner.RunConnectionTest([service]() {
            return service->TestConnection();
        });
    };
    callbacks.set_language = [](ui::Language language) {
        ui::SetCurrentLanguage(language);
    };
    callbacks.save_reviewer_name = [this](const char* reviewer_name) {
        impl_->reviewer_name = TrimWhitespace(reviewer_name ? reviewer_name : "");
        CopyToBuffer(impl_->reviewer_name_buf, sizeof(impl_->reviewer_name_buf), impl_->reviewer_name);
        impl_->show_reviewer_name_prompt = impl_->reviewer_name.empty();
        SetStatus(impl_->reviewer_name.empty() ? "Reviewer name is required for new reviews." : "Reviewer name saved.");
    };

    ui::panels::ShowPreferencesWindow(impl_->show_preferences_window, model, callbacks);
}

void AppRuntime::RenderThemeTweaksWindow() {
    if (!impl_->show_theme_tweak_window) return;
    HelloImGui::ShowThemeTweakGuiWindow(&impl_->show_theme_tweak_window);
}

void AppRuntime::RenderTreePanel(float left_w, float safety_tree_h, float top_y) {
    ImGui::SetNextWindowPos(ImVec2(0, top_y));
    ImGui::SetNextWindowSize(ImVec2(left_w, safety_tree_h));
    ImGui::Begin("Safety Case Tree", nullptr, kPanelFlags);
    ui::UiState& ui_state = ui::GetUiState();
    ui::ElementContextActions actions;
    if (impl_->proposal_preview_active) {
        actions = ui::ElementContextActions{};
    } else if (impl_->proposal_creator_active) {
        actions = ui::ElementContextActions{
            [this](core::NewElementKind kind) { AddProposalChildToSelected(kind); },
            [this]() { AddProposalTopGoal(); },
            [this](core::RemoveMode mode) { RemoveProposalSelected(mode); },
            [this](const char* feature) {
                if (feature) ShowNotImplementedModal(feature);
            },
        };
    } else {
        actions = MakeElementContextActions(*this);
    }
    const parser::AssuranceCase* visible_case = impl_->IsProposalCanvasActive()
        ? &impl_->proposal_preview_model
        : GetLoadedCase();
    ui::ShowTreeViewPanel(impl_->current_tree.root ? &impl_->current_tree : nullptr,
                          visible_case,
                          ui_state,
                          actions);
    ImGui::End();
}

void AppRuntime::RenderSacmViewerPanel(float left_w, float sacm_h, float top_y) {
    ui::panels::SacmViewerPanelModel model{
        impl_->app_state,
        impl_->dir_path_buf,
        sizeof(impl_->dir_path_buf),
        impl_->file_path_buf,
        sizeof(impl_->file_path_buf),
        impl_->xml_files,
        impl_->selected_file_idx,
        impl_->show_overwrite_confirm,
    };
    ui::panels::SacmViewerPanelCallbacks callbacks{
        [this]() { ScanDirectory(); },
        [this]() {
            impl_->tree_needs_rebuild = true;
            impl_->pending_focus_root = true;
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

void AppRuntime::RenderCenterPanel(float center_x, float center_w, float content_h, float top_y) {
    ImGui::SetNextWindowPos(ImVec2(center_x, top_y));
    ImGui::SetNextWindowSize(ImVec2(center_w, content_h));
    ImGui::Begin("Center View", nullptr, kPanelFlags | ImGuiWindowFlags_NoTitleBar);

    ui::UiState& ui_state = ui::GetUiState();
    NormalizeCenterViewSelection(*impl_, ui_state.center_view);

    if (ImGui::BeginTabBar("##center_tabs")) {
        if (impl_->show_gsn_tab) {
            ImGuiTabItemFlags gsn_flags = (impl_->force_center_tab_selection && ui_state.center_view == ui::CenterView::GsnCanvas)
                                          ? ImGuiTabItemFlags_SetSelected
                                          : 0;
            if (ImGui::BeginTabItem(ui::Tr(ui::MessageId::GsnCanvas), nullptr, gsn_flags)) {
                ui_state.center_view = ui::CenterView::GsnCanvas;
                if (impl_->IsProposalCanvasActive()) {
                    const float banner_h = ImGui::GetStyle().WindowPadding.y * 2.0f
                                         + ImGui::GetTextLineHeight()
                                         + ImGui::GetStyle().ItemSpacing.y
                                         + ImGui::GetFrameHeight()
                                         + 2.0f;  // border pixels
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(IM_COL32(42, 45, 30, 255)));
                    ImGui::BeginChild("##proposal_preview_banner", ImVec2(0.0f, banner_h), true, ImGuiWindowFlags_NoScrollbar);
                    ImGui::TextUnformatted(impl_->proposal_creator_active ? "PROPOSAL CREATOR" : "PROPOSAL PREVIEW");
                    if (impl_->proposal_creator_active) {
                        ImGui::TextDisabled("Changes are recorded in the proposal draft. Save it from the review panel.");
                    } else {
                        ImGui::TextDisabled("This is a preview. The project model has not been changed.");
                    }
                    ImGui::SameLine();
                    if (impl_->proposal_creator_active) {
                        ImGui::TextDisabled("%d operation(s)", static_cast<int>(impl_->proposal_draft.operations.size()));
                        ImGui::SameLine();
                    } else if (!impl_->proposal_preview_id.empty()) {
                        if (ImGui::Button("Edit Proposal")) {
                            BeginEditProposalById(impl_->proposal_preview_id);
                        }
                        ImGui::SameLine();
                    }
                    const char* exit_label = impl_->proposal_creator_active ? "Discard Draft" : "Exit Preview";
                    if (ImGui::Button(exit_label)) {
                        const bool was_creator = impl_->proposal_creator_active;
                        impl_->proposal_preview_active = false;
                        impl_->proposal_creator_active = false;
                        impl_->proposal_preview_id.clear();
                        impl_->proposal_preview_model = {};
                        impl_->proposal_draft = {};
                        impl_->proposal_creator_generated_ids.clear();
                        ClearProposalHighlightState(ui::GetUiState());
                        if (impl_->app_state.loaded_case.has_value()) {
                            impl_->current_tree = ui::gsn::BuildAssuranceTree(impl_->app_state.loaded_case.value());
                            ui::gsn::SetCanvasTree(impl_->current_tree);
                        } else {
                            impl_->tree_needs_rebuild = true;
                        }
                        if (was_creator) SetStatus("Discarded proposal draft.");
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }
                ui::ElementContextActions actions;
                if (impl_->proposal_preview_active) {
                    actions = ui::ElementContextActions{};
                } else if (impl_->proposal_creator_active) {
                    actions = ui::ElementContextActions{
                        [this](core::NewElementKind kind) { AddProposalChildToSelected(kind); },
                        [this]() { AddProposalTopGoal(); },
                        [this](core::RemoveMode mode) { RemoveProposalSelected(mode); },
                        [this](const char* feature) {
                            if (feature) ShowNotImplementedModal(feature);
                        },
                    };
                } else {
                    actions = MakeElementContextActions(*this);
                }
                const parser::AssuranceCase* visible_case = impl_->IsProposalCanvasActive()
                    ? &impl_->proposal_preview_model
                    : GetLoadedCase();
                ui_state.proposal_canvas_active = impl_->IsProposalCanvasActive();
                ui::gsn::ShowGsnCanvasContent(ui_state, visible_case, actions);
                ImGui::EndTabItem();
            }
        }

        if (impl_->show_cse_tab) {
            ImGuiTabItemFlags cse_flags = (impl_->force_center_tab_selection && ui_state.center_view == ui::CenterView::CseRegister)
                                          ? ImGuiTabItemFlags_SetSelected
                                          : 0;
            if (ImGui::BeginTabItem(ui::Tr(ui::MessageId::CseRegister), nullptr, cse_flags)) {
                ui_state.center_view = ui::CenterView::CseRegister;
                if (impl_->app_state.active_project_file_role == core::ProjectFileRole::J3377CaeRegister) {
                    ImGui::TextWrapped("J3377 CAE register file: %s",
                                       impl_->app_state.active_project_file_path.string().c_str());
                    ImGui::TextDisabled("Editable CAE register content will be implemented in a later workflow.");
                    ImGui::Separator();
                }
                ui::ShowCseRegisterView();
                ImGui::EndTabItem();
            }
        }

        if (impl_->show_evidence_tab) {
            ImGuiTabItemFlags evidence_flags = (impl_->force_center_tab_selection && ui_state.center_view == ui::CenterView::EvidenceRegister)
                                               ? ImGuiTabItemFlags_SetSelected
                                               : 0;
            if (ImGui::BeginTabItem(ui::Tr(ui::MessageId::EvidenceRegister), nullptr, evidence_flags)) {
                ui_state.center_view = ui::CenterView::EvidenceRegister;
                if (impl_->app_state.active_project_file_role == core::ProjectFileRole::EvidenceRegister) {
                    ImGui::TextWrapped("Evidence register file: %s",
                                       impl_->app_state.active_project_file_path.string().c_str());
                    ImGui::TextDisabled("Editable evidence register content will be implemented in a later workflow.");
                    ImGui::Separator();
                }
                ui::ShowEvidenceRegisterView();
                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
        impl_->force_center_tab_selection = false;
    }

    ImGui::End();
}

void AppRuntime::RenderProblemsPanel(float center_x, float center_w, float problems_h, float top_y) {
    ui::panels::ProblemsPanelModel model{
        impl_->problems_manager,
        ui::GetUiState(),
        impl_->ai_review_task && impl_->ai_review_task->IsRunning() && !impl_->pending_ai_review.prompt.empty(),
    };
    ui::panels::ProblemsPanelCallbacks callbacks{
        [this](const core::ProblemItem& problem) {
            if (problem.element_id.empty()) return;
            ui::GetUiState().selected_problem_element_id = problem.element_id;
            SetStatus("Problem targets element " + problem.element_id + ". Element focus will be added in a later workflow.");
        },
        [this]() { BeginAiReviewForSelection(); },
    };
    ui::panels::ShowProblemsPanel(center_x, center_w, problems_h, top_y, kPanelFlags, model, callbacks);
}

void AppRuntime::RenderProposalElementEditor() {
    if (!impl_->proposal_creator_active) return;

    ImGui::TextUnformatted("Proposal Creator");
    ImGui::TextDisabled("Edits are recorded in the proposal draft only.");
    ImGui::Separator();

    const std::string selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        ImGui::TextWrapped("Select a proposal preview element to edit its proposed properties.");
        return;
    }

    const parser::SacmElement* element = FindParserElement(impl_->proposal_preview_model, selected_id);
    if (!element) {
        ImGui::TextWrapped("The selected proposal preview element no longer exists.");
        return;
    }

    static std::string active_editor_key;
    static char name_buf[256] = "";
    static char text_buf[2048] = "";
    const std::string editor_key = impl_->proposal_draft.id + ":" + selected_id;
    if (active_editor_key != editor_key) {
        active_editor_key = editor_key;
        CopyToBuffer(name_buf, sizeof(name_buf), element->name);
        CopyToBuffer(text_buf, sizeof(text_buf), EditableTextFor(*element));
    }

    const parser::SacmElement element_snapshot = *element;
    std::optional<core::reviews::ElementRef> ref = ProposalRefForPreviewId(selected_id, impl_->proposal_creator_generated_ids);
    if (!ref.has_value()) {
        ImGui::TextWrapped("Could not resolve this preview element for proposal edits.");
        return;
    }

    std::string old_name = element_snapshot.name;
    std::string old_text = EditableTextFor(element_snapshot);
    bool old_undeveloped = element_snapshot.undeveloped;
    if (ref->existing_id.has_value() && impl_->app_state.loaded_case.has_value()) {
        if (const parser::SacmElement* base = FindParserElement(impl_->app_state.loaded_case.value(), ref->existing_id.value())) {
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
    const bool text_changed = ImGui::InputTextMultiline("Text", text_buf, sizeof(text_buf),
                                                        ImVec2(-1.0f, ImGui::GetTextLineHeight() * 5.0f));
    bool undeveloped_value = element_snapshot.undeveloped;
    const bool undeveloped_changed = ImGui::Checkbox("Undeveloped", &undeveloped_value);
    ImGui::PopID();

    if (!name_changed && !text_changed && !undeveloped_changed) return;

    TrackAffectedRef(impl_->proposal_draft, impl_->app_state.loaded_case.value(), ref.value());
    if (name_changed) {
        UpsertElementUpdate(impl_->proposal_draft,
                            core::reviews::PatchOperationType::UpdateElementName,
                            ref.value(),
                            "name",
                            old_name,
                            name_buf);
    }
    if (text_changed) {
        UpsertElementUpdate(impl_->proposal_draft,
                            core::reviews::PatchOperationType::UpdateElementText,
                            ref.value(),
                            EditableTextFieldFor(element_snapshot),
                            old_text,
                            text_buf);
    }
    if (undeveloped_changed) {
        UpsertUndevelopedUpdate(impl_->proposal_draft,
                                ref.value(),
                                old_undeveloped,
                                undeveloped_value);
    }

    if (RefreshProposalCreatorPreview()) {
        SetStatus("Recorded proposal property change.");
    }
}

void AppRuntime::RenderElementPropertiesPanel(float center_x, float center_w, float right_w, float content_h, float top_y) {
    float right_x = center_x + center_w + kSplitterThickness;
    const float min_element_h = 150.0f;
    const float min_review_h = 150.0f;
    const float split_available_h = std::max(0.0f, content_h - kSplitterThickness);
    if (split_available_h > min_element_h + min_review_h) {
        const float min_ratio = min_element_h / split_available_h;
        const float max_ratio = 1.0f - (min_review_h / split_available_h);
        impl_->right_panel_split_ratio = std::clamp(impl_->right_panel_split_ratio, min_ratio, max_ratio);
    } else {
        impl_->right_panel_split_ratio = 0.5f;
    }
    float element_h = split_available_h * impl_->right_panel_split_ratio;
    element_h = std::max(0.0f, element_h);
    float review_y = top_y + element_h + kSplitterThickness;
    float review_h = std::max(0.0f, split_available_h - element_h);

    ImGui::SetNextWindowPos(ImVec2(right_x, top_y));
    ImGui::SetNextWindowSize(ImVec2(right_w, element_h));
    ImGui::Begin("Element Properties", nullptr, kPanelFlags);

    if (impl_->proposal_creator_active) {
        RenderProposalElementEditor();
    } else if (impl_->proposal_preview_active) {
        ImGui::TextWrapped("Proposal preview is active. Exit preview before editing element properties.");
    } else {
        parser::AssuranceCase* ac_ptr = impl_->app_state.loaded_case.has_value() ? &impl_->app_state.loaded_case.value() : nullptr;
        sacm::AssuranceCasePackage* sacm_ptr = impl_->app_state.sacm_package.has_value() ? &impl_->app_state.sacm_package.value() : nullptr;
        if (ui::panels::ShowElementPanel(ac_ptr, sacm_ptr)) {
            impl_->tree_needs_rebuild = true;
            impl_->document_dirty = true;
            impl_->app_state.mark_dirty();
        }
    }

    ImGui::End();

    float delta = ui::widgets::DrawHorizontalSplitter(
        "##element_review_splitter",
        right_x,
        top_y + element_h,
        right_w,
        kSplitterThickness,
        kPanelFlags);
    if (delta != 0.0f && split_available_h > min_element_h + min_review_h) {
        impl_->right_panel_split_ratio += delta / split_available_h;
        const float min_ratio = min_element_h / split_available_h;
        const float max_ratio = 1.0f - (min_review_h / split_available_h);
        impl_->right_panel_split_ratio = std::clamp(impl_->right_panel_split_ratio, min_ratio, max_ratio);
    }

    ImGui::SetNextWindowPos(ImVec2(right_x, review_y));
    ImGui::SetNextWindowSize(ImVec2(right_w, review_h));
    ImGui::Begin("Review", nullptr, kPanelFlags);

    const ui::UiState& ui_state = ui::GetUiState();
    ui::panels::ReviewPanelModel model;
    model.selected_element_id = impl_->proposal_creator_active ? impl_->proposal_draft.anchor_element_id
                                                               : ui_state.selected_element_id;
    model.has_project = impl_->app_state.current_project.has_value();
    if (impl_->proposal_creator_active) {
        model.active_proposal_review_item_id = impl_->proposal_draft.review_item_id;
        model.active_proposal_operation_count = impl_->proposal_draft.operations.size();
        model.active_proposal_can_save = !impl_->proposal_draft.operations.empty();
    }
    if (!model.selected_element_id.empty()) {
        model.review_items = impl_->review_item_manager.GetItemsForElement(model.selected_element_id);
        for (const core::reviews::ReviewItem& item : model.review_items) {
            if (!item.proposal_id.has_value()) continue;
            std::string error;
            std::optional<core::reviews::ReviewProposal> proposal = impl_->review_proposal_manager.LoadProposal(item.proposal_id.value(), error);
            if (!proposal.has_value()) {
                model.proposal_validity[item.proposal_id.value()] = {core::reviews::ProposalValidity::Broken, error};
            } else if (impl_->app_state.loaded_case.has_value()) {
                model.proposal_validity[item.proposal_id.value()] =
                    core::reviews::EvaluateReviewProposalValidity(proposal.value(), impl_->app_state.loaded_case.value());
            } else {
                model.proposal_validity[item.proposal_id.value()] = {core::reviews::ProposalValidity::Broken, "No SACM model is loaded."};
            }
        }
    }

    ui::panels::ReviewPanelCallbacks callbacks;
    callbacks.add_review_item = [this](const std::string& title, const std::string& message) {
        if (impl_->proposal_creator_active) {
            SetStatus("Save or discard the active proposal before adding more review comments.");
            return;
        }
        if (!impl_->app_state.current_project.has_value()) {
            SetStatus("Open or create a project before adding review comments.");
            return;
        }
        if (!EnsureReviewItemStorage()) {
            return;
        }
        if (impl_->reviewer_name.empty()) {
            impl_->show_reviewer_name_prompt = true;
            SetStatus("Enter a reviewer name before adding review comments.");
            return;
        }
        const std::string element_id = ui::GetUiState().selected_element_id;
        if (element_id.empty()) {
            SetStatus("Select an element before adding a review comment.");
            return;
        }

        core::reviews::ReviewItem item;
        item.id = GenerateReviewItemId();
        item.element_id = element_id;
        item.title = title;
        item.message = message;
        item.severity = "warning";
        item.reviewer_name = impl_->reviewer_name;
        item.source = core::reviews::ReviewItemSource::Manual;
        item.status = core::reviews::ReviewItemStatus::Open;
        item.created_utc = NowUtcString();
        item.updated_utc = item.created_utc;

        if (!impl_->review_item_manager.AddOrUpdateItem(std::move(item))) {
            SetStatus("Could not add review comment.");
            return;
        }
        MarkReviewItemsDirty();
        SetStatus("Review comment added.");
    };
    callbacks.create_proposed_change = [this](const core::reviews::ReviewItem& item) {
        BeginProposalForReviewItem(item);
    };
    callbacks.save_proposal = [this](const core::reviews::ReviewItem& item) {
        SaveActiveProposal(item);
    };
    callbacks.edit_proposal = [this](const core::reviews::ReviewItem& item) {
        BeginEditProposalForReviewItem(item);
    };
    callbacks.preview_proposal = [this](const core::reviews::ReviewItem& item) {
        if (!item.proposal_id.has_value()) {
            SetStatus("This review comment has no proposed change to preview.");
            return;
        }
        PreviewProposalById(item.proposal_id.value());
    };
    callbacks.apply_proposal = [this](const core::reviews::ReviewItem& item) {
        if (impl_->proposal_creator_active) {
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
        std::optional<core::reviews::ReviewProposal> proposal = impl_->review_proposal_manager.LoadProposal(item.proposal_id.value(), error);
        if (!proposal.has_value()) {
            SetStatus("Proposal apply failed: " + error);
            return;
        }

        core::reviews::ProposalValidityResult validity = core::reviews::EvaluateReviewProposalValidity(*proposal, impl_->app_state.loaded_case.value());
        if (validity.validity != core::reviews::ProposalValidity::Valid) {
            SetStatus("Proposal is broken: " + validity.reason);
            return;
        }

        core::reviews::ReviewProposalPatchService patch_service;
        core::reviews::ApplyProposalResult apply_result = patch_service.ApplyProposal(*proposal, impl_->app_state.loaded_case.value());
        if (!apply_result.success) {
            SetStatus("Proposal apply failed: " + apply_result.error);
            return;
        }

        impl_->proposal_preview_active = false;
        impl_->proposal_preview_id.clear();
        impl_->proposal_preview_model = {};
        ClearProposalHighlightState(ui::GetUiState());
        if (!impl_->app_state.sacm_package.has_value()) impl_->app_state.sacm_package.emplace();
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
        if (!impl_->review_item_manager.AddOrUpdateItem(std::move(updated))) {
            SetStatus("Proposal applied, but review item update failed.");
            return;
        }
        MarkReviewItemsDirty();

        if (!SaveProject()) {
            SetStatus("Proposal applied, but project save failed: " + impl_->app_state.status_message);
            return;
        }

        core::ProjectService::RefreshFileStatus(project);
        impl_->tree_needs_rebuild = true;
        SetStatus("Applied proposal " + proposal->id + ".");
    };
    callbacks.delete_proposal = [this](const core::reviews::ReviewItem& item) {
        if (impl_->proposal_creator_active) {
            SetStatus("Save or discard the active proposal before deleting another proposal.");
            return;
        }
        if (!item.proposal_id.has_value()) {
            SetStatus("This review comment has no proposed change to delete.");
            return;
        }
        if (!impl_->app_state.current_project.has_value()) {
            SetStatus("Open a project before deleting proposed changes.");
            return;
        }

        core::AssuranceProject& project = impl_->app_state.current_project.value();
        std::string error;
        if (!DeleteProposalPatchFile(item.proposal_id.value(), error)) {
            SetStatus("Proposal delete failed: " + error);
            return;
        }

        if (!impl_->review_item_manager.ClearProposal(item.id)) {
            SetStatus("Proposal deleted, but review link update failed.");
            return;
        }
        MarkReviewItemsDirty();
        CloseProposalPreviewIfOpen(item.proposal_id.value());

        core::ProjectService::RefreshFileStatus(project);
        SetStatus("Deleted proposed change " + item.proposal_id.value() + ".");
    };
    callbacks.resolve_review_item = [this](const core::reviews::ReviewItem& item) {
        ResolveReviewItem(item);
    };
    callbacks.delete_review_item = [this](const core::reviews::ReviewItem& item) {
        BeginDeleteReviewItem(item);
    };
    ui::panels::ShowReviewPanel(model, callbacks);

    ImGui::End();
}

void AppRuntime::RequestExit(bool& done) {
    if (impl_->app_state.has_unsaved_changes) {
        impl_->show_save_before_exit_modal = true;
        return;
    }
    done = true;
}

const parser::AssuranceCase* AppRuntime::GetLoadedCase() const {
    if (!impl_->app_state.loaded_case.has_value()) return nullptr;
    return &impl_->app_state.loaded_case.value();
}

void AppRuntime::LoadRecentProjectsPreference(const std::string& content) {
    impl_->recent_projects = app::LoadRecentProjectsPreference(content);
}

std::string AppRuntime::RecentProjectsPreferenceJson() const {
    return app::SaveRecentProjectsPreference(impl_->recent_projects);
}

void AppRuntime::LoadReviewerNamePreference(const std::string& content) {
    impl_->reviewer_name = TrimWhitespace(content);
    CopyToBuffer(impl_->reviewer_name_buf, sizeof(impl_->reviewer_name_buf), impl_->reviewer_name);
    impl_->show_reviewer_name_prompt = impl_->reviewer_name.empty();
}

std::string AppRuntime::ReviewerNamePreference() const {
    return impl_->reviewer_name;
}

void AppRuntime::RenderFrame(bool& done) {
    if (impl_->close_requested) {
        impl_->close_requested = false;
        RequestExit(done);
    }

    ImVec2 display = ImGui::GetIO().DisplaySize;
    float top_y = RenderMainMenuBar(done);

    float content_h = std::max(0.0f, display.y - top_y);

    float left_w = display.x * impl_->left_ratio;
    float right_w = display.x * impl_->right_ratio;
    float center_w = display.x - left_w - right_w - kSplitterThickness * 2.0f;

    RebuildDerivedViewsIfNeeded();
    ProcessPendingProposalCreatorPreviewRefresh();
    PollAiReviewTask();

    RenderAppSplitters(*impl_, display.x, content_h, left_w, center_w, top_y, kPanelFlags);

    left_w = display.x * impl_->left_ratio;
    right_w = display.x * impl_->right_ratio;
    center_w = display.x - left_w - right_w - kSplitterThickness * 2.0f;

    float available_h = std::max(0.0f, content_h - kSplitterThickness);
    float project_h = available_h * impl_->project_boundary_ratio;
    float safety_tree_h = std::max(0.0f, available_h - project_h);

    float project_y = top_y;
    float safety_y = project_y + project_h + kSplitterThickness;

    ui::panels::ProjectFilesPanelModel project_model;
    project_model.project = impl_->app_state.current_project.has_value() ? &impl_->app_state.current_project.value() : nullptr;
    if (project_model.project) {
        const parser::AssuranceCase* loaded_case = impl_->app_state.loaded_case.has_value()
            ? &impl_->app_state.loaded_case.value()
            : nullptr;
        for (const core::reviews::ReviewProposalSummary& summary : impl_->review_proposal_manager.ListProposals(loaded_case)) {
            project_model.proposal_validity_by_path[summary.relative_path.generic_string()] = summary.validity;
        }
    }
    ui::panels::ProjectFilesPanelCallbacks project_callbacks{
        [this]() { BeginCreateProjectSacmFile(); },
        [this]() { BeginCreateProjectEvidenceRegister(); },
        [this]() { BeginCreateProjectJ3377CaeRegister(); },
        [this](const core::ProjectFileEntry& entry) { OpenProjectFile(entry); },
    };
    ui::panels::ShowProjectFilesPanel(left_w, project_h, project_y, kPanelFlags, project_model, project_callbacks);
    RenderTreePanel(left_w, safety_tree_h, safety_y);

    float center_x = left_w + kSplitterThickness;
    float center_available_h = std::max(0.0f, content_h - kSplitterThickness);
    float problems_h = std::min(impl_->problems_panel_height, center_available_h);
    float center_panel_h = std::max(0.0f, center_available_h - problems_h);
    float problems_y = top_y + center_panel_h + kSplitterThickness;

    RenderCenterPanel(center_x, center_w, center_panel_h, top_y);
    RenderProblemsPanel(center_x, center_w, problems_h, problems_y);
    RenderElementPropertiesPanel(center_x, center_w, right_w, content_h, top_y);

    RenderPreferencesWindow();
    RenderThemeTweaksWindow();
    RenderAiReviewDebugModal();

    RenderNotImplementedModal();
    RenderRemoveConfirmModal();
    RenderDeleteReviewItemConfirmModal();
    RenderCreateProjectModal();
    RenderProjectFileNameModal();
    RenderProjectLoadReportModal();
    RenderSaveBeforeExitModal(done);
    RenderStartupProjectWindow();
    RenderReviewerNamePromptModal();
}

}  // namespace app
