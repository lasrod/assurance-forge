#include "app/app_runtime.h"

#include "ai/ai_service.h"
#include "ai/ai_task_runner.h"
#include "ai/secret_store.h"
#include "app/app_layout_controller.h"
#include "app/app_runtime_state.h"
#include "app/guideline_catalog.h"
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
#include "hello_imgui/hello_imgui.h"
#include "hello_imgui/hello_imgui_theme.h"
#include "imgui.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/gsn/gsn_canvas_renderer.h"
#include "ui/localization.h"
#include "ui/panels/element_panel.h"
#include "ui/panels/preferences_panel.h"
#include "ui/panels/problems_panel.h"
#include "ui/panels/project_files_panel.h"
#include "ui/panels/review_panel.h"
#include "ui/panels/sacm_viewer_panel.h"
#include "ui/register_views.h"
#include "ui/theme.h"
#include "ui/tree_view.h"
#include "ui/ui_state.h"
#include "ui/widgets/splitter.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
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

void RenderLanguageMenu() {
    if (!ImGui::BeginMenu(ui::Tr(ui::MessageId::Language)))
        return;

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
    if (!ImGui::BeginMenu(ui::Tr(ui::MessageId::Theme)))
        return;

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
    if (value.size() <= limit)
        return value;
    return value.substr(0, limit) + "...";
}

bool IsReviewDerivedProblem(const core::ProblemItem& problem) {
    return problem.id.rfind("review-comment:", 0) == 0 || problem.id.rfind("guideline-review:", 0) == 0;
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
    case core::NewElementKind::Goal:
        return core::reviews::PatchOperationType::CreateClaim;
    case core::NewElementKind::Strategy:
        return core::reviews::PatchOperationType::CreateStrategy;
    case core::NewElementKind::Solution:
        return core::reviews::PatchOperationType::CreateSolution;
    case core::NewElementKind::Context:
        return core::reviews::PatchOperationType::CreateContext;
    case core::NewElementKind::Assumption:
        return core::reviews::PatchOperationType::CreateAssumption;
    case core::NewElementKind::Justification:
        return core::reviews::PatchOperationType::CreateJustification;
    }
    return core::reviews::PatchOperationType::CreateClaim;
}

void EnsureGuidelineCatalogLoaded(AppRuntimeState& state) {
    if (state.guideline_catalog_load_attempted)
        return;

    GuidelineCatalog catalog;
    std::string error;
    if (LoadGuidelineCatalog(catalog, error)) {
        state.guideline_catalog = std::move(catalog);
        state.guideline_catalog_error.clear();
    } else {
        state.guideline_catalog.reset();
        state.guideline_catalog_error = error;
    }
    state.guideline_catalog_load_attempted = true;
}

const char* CreateRefPrefixFor(core::NewElementKind kind) {
    switch (kind) {
    case core::NewElementKind::Goal:
        return "$new_claim_";
    case core::NewElementKind::Strategy:
        return "$new_strategy_";
    case core::NewElementKind::Solution:
        return "$new_solution_";
    case core::NewElementKind::Context:
        return "$new_context_";
    case core::NewElementKind::Assumption:
        return "$new_assumption_";
    case core::NewElementKind::Justification:
        return "$new_justification_";
    }
    return "$new_element_";
}

bool IsContextLike(core::NewElementKind kind) {
    return kind == core::NewElementKind::Context || kind == core::NewElementKind::Assumption ||
           kind == core::NewElementKind::Justification;
}

const char* RemoveModeField(core::RemoveMode mode) {
    return mode == core::RemoveMode::NodeAndDescendants ? core::reviews::kReviewProposalRemoveModeNodeAndDescendants
                                                        : core::reviews::kReviewProposalRemoveModeNodeOnly;
}

std::string GenerateCreateRef(const core::reviews::ReviewProposal& proposal, core::NewElementKind kind) {
    std::unordered_set<std::string> used;
    for (const core::reviews::PatchOperation& operation : proposal.operations) {
        if (operation.create_ref.has_value())
            used.insert(operation.create_ref.value());
    }
    const std::string prefix = CreateRefPrefixFor(kind);
    for (int i = 1; i < 100000; ++i) {
        std::string candidate = prefix + std::to_string(i);
        if (used.count(candidate) == 0)
            return candidate;
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

std::string PreviewIdForProposalRef(const core::reviews::ElementRef& ref,
                                    const std::map<std::string, std::string>& generated_ids) {
    if (ref.existing_id.has_value())
        return ref.existing_id.value();
    if (ref.create_ref.has_value()) {
        auto found = generated_ids.find(ref.create_ref.value());
        if (found != generated_ids.end())
            return found->second;
    }
    return {};
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

void AddHighlightRef(std::unordered_set<std::string>& ids,
                     const core::reviews::ElementRef& ref,
                     const std::map<std::string, std::string>& generated_ids) {
    if (ref.existing_id.has_value() && !ref.existing_id->empty()) {
        ids.insert(ref.existing_id.value());
    }
    if (ref.create_ref.has_value()) {
        auto found = generated_ids.find(ref.create_ref.value());
        if (found != generated_ids.end() && !found->second.empty())
            ids.insert(found->second);
    }
}

bool IsPreviewRelationshipType(const std::string& type) {
    return type == "assertedinference" || type == "assertedcontext" || type == "assertedevidence";
}

bool RelationshipTouchesAny(const parser::SacmElement& relationship, const std::unordered_set<std::string>& ids) {
    if (!IsPreviewRelationshipType(relationship.type))
        return false;
    if (ids.count(relationship.reasoning_ref) > 0)
        return true;
    for (const std::string& source : relationship.source_refs) {
        if (ids.count(source) > 0)
            return true;
    }
    for (const std::string& target : relationship.target_refs) {
        if (ids.count(target) > 0)
            return true;
    }
    return false;
}

bool SameRelationship(const parser::SacmElement& lhs, const parser::SacmElement& rhs) {
    return lhs.type == rhs.type && lhs.reasoning_ref == rhs.reasoning_ref && lhs.source_refs == rhs.source_refs &&
           lhs.target_refs == rhs.target_refs;
}

bool RelationshipExists(const parser::AssuranceCase& model, const parser::SacmElement& relationship) {
    for (const parser::SacmElement& element : model.elements) {
        if (!IsPreviewRelationshipType(element.type))
            continue;
        if (!relationship.id.empty() && element.id == relationship.id)
            return true;
        if (SameRelationship(element, relationship))
            return true;
    }
    return false;
}

std::optional<core::RemoveMode> ProposalRemoveModeFromField(const std::string& field) {
    if (field == core::reviews::kReviewProposalRemoveModeNodeOnly)
        return core::RemoveMode::NodeOnly;
    if (field == core::reviews::kReviewProposalRemoveModeNodeAndDescendants)
        return core::RemoveMode::NodeAndDescendants;
    return std::nullopt;
}

std::unordered_set<std::string>
CollectProposalRemovedExistingIds(const core::reviews::ReviewProposal& proposal,
                                  const parser::AssuranceCase& base_model,
                                  const std::map<std::string, std::string>& generated_ids) {
    std::unordered_set<std::string> ids;
    for (const core::reviews::PatchOperation& operation : proposal.operations) {
        if (operation.type != core::reviews::PatchOperationType::RemoveElement || !operation.element.has_value())
            continue;
        const std::string element_id = PreviewIdForProposalRef(operation.element.value(), generated_ids);
        if (element_id.empty() || !FindParserElement(base_model, element_id))
            continue;

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

void RestoreRemovedExistingElementsForProposalPreview(parser::AssuranceCase& preview_model,
                                                      const parser::AssuranceCase& base_model,
                                                      const std::unordered_set<std::string>& removed_ids) {
    if (removed_ids.empty())
        return;

    for (const parser::SacmElement& element : base_model.elements) {
        if (IsPreviewRelationshipType(element.type))
            continue;
        if (removed_ids.count(element.id) == 0)
            continue;
        if (FindParserElement(preview_model, element.id))
            continue;
        preview_model.elements.push_back(element);
    }

    for (const parser::SacmElement& relationship : base_model.elements) {
        if (!RelationshipTouchesAny(relationship, removed_ids))
            continue;
        if (RelationshipExists(preview_model, relationship))
            continue;
        preview_model.elements.push_back(relationship);
    }
}

std::unordered_set<std::string> CollectProposalHighlightIds(const core::reviews::ReviewProposal& proposal,
                                                            const std::map<std::string, std::string>& generated_ids) {
    std::unordered_set<std::string> ids;
    if (!proposal.anchor_element_id.empty())
        ids.insert(proposal.anchor_element_id);
    for (const std::string& id : proposal.affected_existing_element_ids) {
        if (!id.empty())
            ids.insert(id);
    }
    for (const auto& generated : generated_ids) {
        if (!generated.second.empty())
            ids.insert(generated.second);
    }
    for (const core::reviews::PatchOperation& operation : proposal.operations) {
        if (operation.element.has_value())
            AddHighlightRef(ids, operation.element.value(), generated_ids);
        if (operation.source.has_value())
            AddHighlightRef(ids, operation.source.value(), generated_ids);
        if (operation.target.has_value())
            AddHighlightRef(ids, operation.target.value(), generated_ids);
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
    std::unordered_set<std::string> removed_ids =
        CollectProposalRemovedExistingIds(proposal, base_model, generated_ids);
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
    target.name = source.name;
    target.description = source.description;
    target.name_ml.texts = source.name_langs;
    target.description_ml.texts = source.description_langs;
    if (target.name_ml.texts.empty() && !source.name.empty())
        target.name_ml.set("en", source.name);
    if (target.description_ml.texts.empty() && !source.description.empty())
        target.description_ml.set("en", source.description);
}

void RebuildSacmArgumentPackageFromParser(const parser::AssuranceCase& model, sacm::AssuranceCasePackage& package) {
    if (package.argumentPackages.empty())
        package.argumentPackages.emplace_back();
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
        if (event.focus_root)
            impl_->pending_focus_root = true;
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
            impl_->force_center_tab_selection = true;
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
    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("No assurance case loaded.");
        return false;
    }
    const std::string& selected_id = ui::GetUiState().selected_element_id;

    parser::AssuranceCase& ac = impl_->app_state.loaded_case.value();
    sacm::AssuranceCasePackage* pkg =
        impl_->app_state.sacm_package.has_value() ? &impl_->app_state.sacm_package.value() : nullptr;
    return impl_->element_edit_controller->AddChildToSelected(ac, pkg, selected_id, kind);
}

bool AppRuntime::AddTopGoal() {
    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("No assurance case loaded.");
        return false;
    }

    parser::AssuranceCase& ac = impl_->app_state.loaded_case.value();
    sacm::AssuranceCasePackage* pkg =
        impl_->app_state.sacm_package.has_value() ? &impl_->app_state.sacm_package.value() : nullptr;
    return impl_->element_edit_controller->AddTopGoal(ac, pkg);
}

void AppRuntime::RemoveSelected(core::RemoveMode mode) {
    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("No assurance case loaded.");
        return;
    }
    const std::string& selected_id = ui::GetUiState().selected_element_id;

    parser::AssuranceCase& ac = impl_->app_state.loaded_case.value();
    sacm::AssuranceCasePackage* pkg =
        impl_->app_state.sacm_package.has_value() ? &impl_->app_state.sacm_package.value() : nullptr;
    if (!impl_->element_edit_controller->RemoveSelected(ac, pkg, selected_id, mode))
        return;

    if (impl_->element_edit_controller->ShouldShowRemoveConfirm()) {
        auto& s = ui::GetUiState();
        const auto& pending_ids = impl_->element_edit_controller->PendingRemoveIds();
        s.marked_for_removal = {pending_ids.begin(), pending_ids.end()};
        s.center_on_marked = true;
    }
}

core::TreeDropValidationResult AppRuntime::ValidateTreeDrop(const std::string& dragged_id,
                                                            const std::string& target_id,
                                                            core::TreeDropMode drop_mode) const {
    if (!impl_->app_state.loaded_case.has_value()) {
        core::TreeDropValidationResult result;
        result.reason = "No assurance case loaded.";
        return result;
    }

    return core::ValidateTreeDrop(
        impl_->app_state.loaded_case.value(), impl_->current_tree, dragged_id, target_id, drop_mode);
}

bool AppRuntime::PerformTreeDrop(const std::string& dragged_id,
                                 const std::string& target_id,
                                 core::TreeDropMode drop_mode) {
    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("No assurance case loaded.");
        return false;
    }

    parser::AssuranceCase& model = impl_->app_state.loaded_case.value();
    sacm::AssuranceCasePackage* package =
        impl_->app_state.sacm_package.has_value() ? &impl_->app_state.sacm_package.value() : nullptr;

    std::string error;
    bool changed = false;
    if (drop_mode == core::TreeDropMode::Before || drop_mode == core::TreeDropMode::After) {
        changed = core::ReorderSiblings(model,
                                        impl_->current_tree,
                                        impl_->tree_display_order,
                                        core::ReorderSiblingsCommand{dragged_id, target_id, drop_mode},
                                        error);
    } else {
        changed = core::MoveSubtree(
            model, package, impl_->current_tree, core::MoveSubtreeCommand{dragged_id, target_id}, error);
    }

    if (!changed) {
        SetStatus("Tree move failed: " + error);
        return false;
    }

    impl_->events.Emit(TreeDirtyEvent{});
    impl_->events.Emit(SelectionChangedEvent{dragged_id, true});
    impl_->events.Emit(DocumentDirtyEvent{});
    SetStatus(drop_mode == core::TreeDropMode::AsChild ? "Moved " + dragged_id : "Reordered " + dragged_id);
    return true;
}

void AppRuntime::SetStatus(const std::string& message) {
    impl_->events.Emit(StatusMessageEvent{message});
}

void AppRuntime::ShowNotImplementedModal(const std::string& feature) {
    impl_->events.Emit(ModalRequestEvent{ModalKind::NotImplemented, true, feature});
}

bool AppRuntime::RefreshProposalCreatorPreview() {
    auto& proposals = *impl_->proposal_controller;
    if (!proposals.creator_active)
        return false;
    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("Load a SACM model before editing proposal drafts.");
        return false;
    }

    core::reviews::ReviewProposalPatchService patch_service;
    core::reviews::ProposalPreviewResult preview =
        patch_service.BuildPreviewModel(proposals.draft, impl_->app_state.loaded_case.value());
    if (!preview.success) {
        SetStatus("Proposal draft preview failed: " + preview.error);
        return false;
    }

    proposals.preview_active = false;
    proposals.preview_id = proposals.draft.id;
    proposals.preview_model = std::move(preview.preview_model);
    proposals.creator_generated_ids = std::move(preview.generated_ids);
    ui::UiState& ui_state = ui::GetUiState();
    ApplyProposalPreviewVisualState(ui_state,
                                    proposals.preview_model,
                                    impl_->app_state.loaded_case.value(),
                                    proposals.draft,
                                    proposals.creator_generated_ids);
    impl_->current_tree = ui::gsn::BuildAssuranceTree(proposals.preview_model);
    ui::gsn::SetCanvasTree(impl_->current_tree);
    return true;
}

void AppRuntime::ProcessPendingProposalCreatorPreviewRefresh() {
    auto& proposals = *impl_->proposal_controller;
    if (!proposals.creator_preview_refresh_pending)
        return;

    proposals.creator_preview_refresh_pending = false;
    const std::optional<std::string> select_create_ref = proposals.creator_pending_select_create_ref;
    const bool clear_selection = proposals.creator_pending_clear_selection;
    proposals.creator_pending_select_create_ref.reset();
    proposals.creator_pending_clear_selection = false;

    if (!RefreshProposalCreatorPreview())
        return;

    ui::UiState& ui_state = ui::GetUiState();
    if (select_create_ref.has_value()) {
        ui_state.selected_element_id =
            PreviewIdForProposalRef(CreatedElementRef(select_create_ref.value()), proposals.creator_generated_ids);
        ui_state.center_on_selection = !ui_state.selected_element_id.empty();
    } else if (clear_selection) {
        ui_state.selected_element_id.clear();
    }
}

bool AppRuntime::BeginProposalForReviewItem(const core::reviews::ReviewItem& item) {
    auto& proposals = *impl_->proposal_controller;
    if (proposals.creator_active) {
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

    proposals.BeginDraft(item, impl_->app_state.loaded_case.value(), *anchor, impl_->reviewer_name);
    if (!RefreshProposalCreatorPreview()) {
        CancelActiveProposal();
        return false;
    }

    ui::UiState& ui_state = ui::GetUiState();
    ui_state.center_view = ui::CenterView::GsnCanvas;
    ui_state.selected_element_id = anchor->id;
    ui_state.center_on_selection = true;
    impl_->force_center_tab_selection = true;
    SetStatus("Building proposal " + proposals.draft.id + ". Use the GSN canvas and Save Proposal when ready.");
    return true;
}

bool AppRuntime::BeginEditProposalForReviewItem(const core::reviews::ReviewItem& item) {
    auto& proposals = *impl_->proposal_controller;
    if (proposals.creator_active) {
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
    std::optional<core::reviews::ReviewProposal> proposal =
        proposals.manager.LoadProposal(item.proposal_id.value(), error);
    if (!proposal.has_value()) {
        SetStatus("Proposal edit failed: " + error);
        return false;
    }
    if (proposal->review_item_id != item.id) {
        SetStatus("Proposal edit failed: the proposal belongs to a different review comment.");
        return false;
    }

    proposals.BeginEditDraft(std::move(proposal.value()), impl_->reviewer_name);
    if (!RefreshProposalCreatorPreview()) {
        CancelActiveProposal();
        return false;
    }

    ui::UiState& ui_state = ui::GetUiState();
    ui_state.center_view = ui::CenterView::GsnCanvas;
    ui_state.selected_element_id = proposals.draft.anchor_element_id;
    ui_state.center_on_selection = !ui_state.selected_element_id.empty();
    impl_->force_center_tab_selection = true;
    SetStatus("Editing proposal " + proposals.draft.id + ". Use Save Proposal to update it.");
    return true;
}

bool AppRuntime::BeginEditProposalById(const std::string& proposal_id) {
    if (proposal_id.empty()) {
        SetStatus("No proposal id was provided.");
        return false;
    }

    std::string error;
    std::optional<core::reviews::ReviewProposal> proposal =
        impl_->proposal_controller->manager.LoadProposal(proposal_id, error);
    if (!proposal.has_value()) {
        SetStatus("Proposal edit failed: " + error);
        return false;
    }

    std::optional<core::reviews::ReviewItem> item = impl_->review_controller->GetItemById(proposal->review_item_id);
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
    auto& proposals = *impl_->proposal_controller;
    if (proposals.creator_active) {
        SetStatus("Save or discard the active proposal before viewing another proposal.");
        return false;
    }
    if (proposal_id.empty()) {
        SetStatus("No proposal id was provided.");
        return false;
    }

    std::string error;
    std::optional<core::reviews::ReviewProposal> proposal = proposals.manager.LoadProposal(proposal_id, error);
    if (!proposal.has_value()) {
        SetStatus("Proposal preview failed: " + error);
        return false;
    }

    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("Load a SACM model before previewing proposals.");
        return false;
    }

    core::reviews::ReviewProposalPatchService patch_service;
    core::reviews::ProposalPreviewResult preview =
        patch_service.BuildPreviewModel(*proposal, impl_->app_state.loaded_case.value());
    if (!preview.success) {
        SetStatus("Proposal preview failed: " + preview.error);
        return false;
    }

    const std::map<std::string, std::string> generated_ids = preview.generated_ids;
    proposals.preview_active = true;
    proposals.preview_id = proposal->id;
    proposals.preview_model = std::move(preview.preview_model);

    ui::UiState& preview_ui_state = ui::GetUiState();
    ApplyProposalPreviewVisualState(
        preview_ui_state, proposals.preview_model, impl_->app_state.loaded_case.value(), *proposal, generated_ids);
    impl_->current_tree = ui::gsn::BuildAssuranceTree(proposals.preview_model);
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
    auto& proposals = *impl_->proposal_controller;
    if (!proposals.HasActiveDraftForItem(item.id)) {
        SetStatus("No active proposal draft for this review comment.");
        return false;
    }
    if (!proposals.CanSaveActiveDraft()) {
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
            project, proposals.draft.id, core::reviews::SerializeReviewProposal(proposals.draft), entry, error)) {
        SetStatus("Proposal save failed: " + error);
        return false;
    }

    if (!impl_->review_controller->SetProposal(item.id, proposals.draft.id)) {
        std::string cleanup_error;
        core::ProjectService::RemoveTrackedFile(project, entry.relativePath, true, cleanup_error);
        SetStatus("Proposal link update failed.");
        return false;
    }

    const std::string saved_id = proposals.draft.id;
    CancelActiveProposal();
    core::ProjectService::RefreshFileStatus(project);
    SetStatus("Saved proposal " + saved_id + ".");
    return true;
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
    impl_->proposal_controller->ClearActiveState();
    ClearProposalHighlightState(ui::GetUiState());

    if (impl_->app_state.loaded_case.has_value()) {
        impl_->current_tree = ui::gsn::BuildAssuranceTree(impl_->app_state.loaded_case.value());
        ui::gsn::SetCanvasTree(impl_->current_tree);
    } else {
        impl_->tree_needs_rebuild = true;
    }
}

void AppRuntime::MarkReviewItemsDirty() {
    impl_->review_controller->MarkDirty();
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
    return impl_->proposal_controller->manager.DeleteProposal(proposal_id, error);
}

void AppRuntime::CloseProposalPreviewIfOpen(const std::string& proposal_id) {
    if (!impl_->proposal_controller->ClosePreviewIfOpen(proposal_id))
        return;
    ClearProposalHighlightState(ui::GetUiState());
    if (impl_->app_state.loaded_case.has_value()) {
        impl_->current_tree = ui::gsn::BuildAssuranceTree(impl_->app_state.loaded_case.value());
        ui::gsn::SetCanvasTree(impl_->current_tree);
    } else {
        impl_->tree_needs_rebuild = true;
    }
}

void AppRuntime::BeginDeleteReviewItem(const core::reviews::ReviewItem& item) {
    const bool creator_active = impl_->proposal_controller->creator_active;
    impl_->review_controller->BeginDeleteReviewItem(item, creator_active);
    if (!item.proposal_id.has_value() && !creator_active)
        DeleteReviewItem(item);
}

bool AppRuntime::DeleteReviewItem(const core::reviews::ReviewItem& item) {
    const bool deleted = impl_->review_controller->DeleteReviewItem(
        item,
        impl_->proposal_controller->creator_active,
        impl_->app_state.current_project.has_value(),
        [this](const std::string& proposal_id, std::string& error) {
            return DeleteProposalPatchFile(proposal_id, error);
        },
        [this](const std::string& proposal_id) { CloseProposalPreviewIfOpen(proposal_id); });
    if (deleted && impl_->app_state.current_project.has_value()) {
        core::ProjectService::RefreshFileStatus(impl_->app_state.current_project.value());
    }
    return deleted;
}

bool AppRuntime::ResolveReviewItem(const core::reviews::ReviewItem& item) {
    return impl_->review_controller->ResolveReviewItem(
        item, impl_->proposal_controller->creator_active, impl_->app_state.current_project.has_value(), NowUtcString());
}

bool AppRuntime::AddProposalChildToSelected(core::NewElementKind kind) {
    auto& proposals = *impl_->proposal_controller;
    if (!proposals.creator_active || !impl_->app_state.loaded_case.has_value()) {
        SetStatus("Start a proposal draft before editing proposal changes.");
        return false;
    }

    const std::string selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        SetStatus("Select an element before adding proposal nodes.");
        return false;
    }

    const parser::SacmElement* parent = FindParserElement(proposals.preview_model, selected_id);
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

    std::optional<core::reviews::ElementRef> parent_ref =
        ProposalRefForPreviewId(selected_id, proposals.creator_generated_ids);
    if (!parent_ref.has_value()) {
        SetStatus("Could not resolve selected element for proposal operation.");
        return false;
    }

    const std::string create_ref = GenerateCreateRef(proposals.draft, kind);

    core::reviews::PatchOperation create;
    create.type = CreateOperationFor(kind);
    create.create_ref = create_ref;
    proposals.draft.operations.push_back(std::move(create));

    core::reviews::PatchOperation relationship;
    relationship.type = IsContextLike(kind) ? core::reviews::PatchOperationType::AddInContextOf
                                            : core::reviews::PatchOperationType::AddSupportedBy;
    relationship.source = CreatedElementRef(create_ref);
    relationship.target = parent_ref.value();
    proposals.draft.operations.push_back(std::move(relationship));

    TrackAffectedRef(proposals.draft, impl_->app_state.loaded_case.value(), parent_ref.value());

    proposals.creator_preview_refresh_pending = true;
    proposals.creator_pending_select_create_ref = create_ref;
    proposals.creator_pending_clear_selection = false;
    SetStatus("Recorded proposal add operation.");
    return true;
}

bool AppRuntime::AddProposalTopGoal() {
    auto& proposals = *impl_->proposal_controller;
    if (!proposals.creator_active || !impl_->app_state.loaded_case.has_value()) {
        SetStatus("Start a proposal draft before editing proposal changes.");
        return false;
    }

    const std::string create_ref = GenerateCreateRef(proposals.draft, core::NewElementKind::Goal);

    core::reviews::PatchOperation create;
    create.type = core::reviews::PatchOperationType::CreateClaim;
    create.create_ref = create_ref;
    proposals.draft.operations.push_back(std::move(create));

    proposals.creator_preview_refresh_pending = true;
    proposals.creator_pending_select_create_ref = create_ref;
    proposals.creator_pending_clear_selection = false;
    SetStatus("Recorded proposal top goal operation.");
    return true;
}

void AppRuntime::RemoveProposalSelected(core::RemoveMode mode) {
    auto& proposals = *impl_->proposal_controller;
    if (!proposals.creator_active || !impl_->app_state.loaded_case.has_value()) {
        SetStatus("Start a proposal draft before editing proposal changes.");
        return;
    }

    const std::string selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        SetStatus("Select an element before removing proposal nodes.");
        return;
    }

    std::vector<std::string> planned_ids;
    auto planned = core::PlanRemoval(proposals.preview_model, selected_id, mode);
    planned_ids.assign(planned.begin(), planned.end());
    std::sort(planned_ids.begin(), planned_ids.end());
    if (planned_ids.empty()) {
        SetStatus("Nothing to remove for this selection.");
        return;
    }

    for (const std::string& id : planned_ids) {
        std::optional<core::reviews::ElementRef> ref = ProposalRefForPreviewId(id, proposals.creator_generated_ids);
        if (!ref.has_value())
            continue;
        TrackAffectedRef(proposals.draft, impl_->app_state.loaded_case.value(), ref.value());
    }

    std::optional<core::reviews::ElementRef> selected_ref =
        ProposalRefForPreviewId(selected_id, proposals.creator_generated_ids);
    if (!selected_ref.has_value()) {
        SetStatus("Could not resolve selected element for proposal removal.");
        return;
    }

    proposals.draft.operations.erase(
        std::remove_if(proposals.draft.operations.begin(),
                       proposals.draft.operations.end(),
                       [&](const core::reviews::PatchOperation& operation) {
                           return operation.type == core::reviews::PatchOperationType::RemoveElement &&
                                  operation.element.has_value() &&
                                  SameElementRef(operation.element.value(), selected_ref.value());
                       }),
        proposals.draft.operations.end());

    core::reviews::PatchOperation remove;
    remove.type = core::reviews::PatchOperationType::RemoveElement;
    remove.element = selected_ref.value();
    remove.field = RemoveModeField(mode);
    proposals.draft.operations.push_back(std::move(remove));

    proposals.creator_preview_refresh_pending = true;
    proposals.creator_pending_select_create_ref.reset();
    proposals.creator_pending_clear_selection = true;
    SetStatus("Recorded proposal remove operation.");
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
        impl_->tree_needs_rebuild = false;
        return;
    }

    if (!impl_->app_state.loaded_case.has_value() || !impl_->tree_needs_rebuild) {
        return;
    }

    const auto& ac = impl_->app_state.loaded_case.value();
    impl_->current_tree = ui::gsn::BuildAssuranceTree(ac);
    core::ApplyTreeDisplayOrder(impl_->current_tree, impl_->tree_display_order);
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
        if (!has_project)
            ImGui::BeginDisabled();
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::SaveProject))) {
            SaveProject();
        }
        if (!has_project)
            ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::Exit))) {
            RequestExit(done);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ui::Tr(ui::MessageId::AddMenu))) {
        bool has_project = impl_->app_state.current_project.has_value();
        if (!has_project)
            ImGui::BeginDisabled();
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::NewGsnSacmFile))) {
            BeginCreateProjectSacmFile();
        }
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::NewEvidenceRegister))) {
            BeginCreateProjectEvidenceRegister();
        }
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::NewJ3377CaeRegister))) {
            BeginCreateProjectJ3377CaeRegister();
        }
        if (!has_project)
            ImGui::EndDisabled();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ui::Tr(ui::MessageId::EditMenu))) {
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::Preferences))) {
            impl_->modal_coordinator->show_preferences_window = true;
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
                impl_->modal_coordinator->show_theme_tweak_window = true;
            }
            RenderThemeMenu();
            RenderLanguageMenu();
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::WelcomeScreen))) {
            impl_->project_controller->show_startup_project_window = true;
        }

        ImGui::EndMenu();
    }

    HelloImGui::RunnerParams* runner_params = HelloImGui::GetRunnerParams();
    if (runner_params && runner_params->imGuiWindowParams.showStatus_Fps) {
        ui::gsn::CanvasRenderStats stats = ui::gsn::GetLastCanvasRenderStats();
        const int node_total = stats.nodes_drawn + stats.nodes_culled;
        const int edge_total = stats.edges_drawn + stats.edges_culled;
        const float node_ratio =
            node_total > 0 ? static_cast<float>(stats.nodes_culled) / static_cast<float>(node_total) : 0.0f;
        const float edge_ratio =
            edge_total > 0 ? static_cast<float>(stats.edges_culled) / static_cast<float>(edge_total) : 0.0f;

        char fps_text[32];
        char nodes_text[32];
        char edges_text[32];
        std::snprintf(fps_text, sizeof(fps_text), "FPS: %.1f", ImGui::GetIO().Framerate);
        std::snprintf(nodes_text, sizeof(nodes_text), "N %d/%d", stats.nodes_drawn, node_total);
        std::snprintf(edges_text, sizeof(edges_text), "E %d/%d", stats.edges_drawn, edge_total);

        const char* sep = "  ";
        const float total_width = ImGui::CalcTextSize(fps_text).x + ImGui::CalcTextSize(sep).x +
                                  ImGui::CalcTextSize(nodes_text).x + ImGui::CalcTextSize(sep).x +
                                  ImGui::CalcTextSize(edges_text).x;

        const float right_x = ImGui::GetWindowContentRegionMax().x - total_width;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right_x));

        ImGui::TextUnformatted(fps_text);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextUnformatted(sep);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextColored(ui::CullRatioColor(node_ratio), "%s", nodes_text);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextUnformatted(sep);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextColored(ui::CullRatioColor(edge_ratio), "%s", edges_text);
    }

    ImGui::EndMainMenuBar();
    return ImGui::GetFrameHeight();
}

void AppRuntime::RenderPreferencesWindow() {
    if (!impl_->modal_coordinator->show_preferences_window)
        return;

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
    if (HelloImGui::RunnerParams* runner_params = HelloImGui::GetRunnerParams()) {
        model.showFps = runner_params->imGuiWindowParams.showStatus_Fps;
    }

    ui::panels::PreferencesPanelCallbacks callbacks;
    callbacks.save_settings = [this](const ai::AiProviderSettings& settings) {
        impl_->ai_settings = settings;
        if (impl_->ai_settings.model.empty())
            impl_->ai_settings.model = ai::kDefaultOpenAiModel;
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
            impl_->ai_connection_status =
                ai::ErrorStatus(ai::AiErrorCode::MissingApiKey, "Enter an API key before saving.");
            return;
        }
        ai::SecretStoreResult result = impl_->ai_service->SaveApiKey(api_key);
        std::memset(impl_->ai_api_key_buf, 0, sizeof(impl_->ai_api_key_buf));
        impl_->RefreshStoredAiKeyState();
        impl_->ai_connection_status = result.success ? ai::SuccessStatus("API key saved securely.")
                                                     : ai::ErrorStatus(result.errorCode, result.errorMessage);
    };
    callbacks.remove_api_key = [this]() {
        ai::SecretStoreResult result = impl_->ai_service->DeleteApiKey();
        std::memset(impl_->ai_api_key_buf, 0, sizeof(impl_->ai_api_key_buf));
        impl_->RefreshStoredAiKeyState();
        impl_->ai_connection_status = result.success ? ai::SuccessStatus("API key removed.")
                                                     : ai::ErrorStatus(result.errorCode, result.errorMessage);
    };
    callbacks.test_connection = [this]() {
        if (impl_->ai_test_task && impl_->ai_test_task->IsRunning())
            return;
        impl_->ai_connection_status =
            ai::MakeStatus(ai::AiTaskState::Running, ai::AiErrorCode::None, "Testing connection...");
        impl_->ai_settings.model = impl_->ai_model_buf;
        if (impl_->ai_settings.model.empty())
            impl_->ai_settings.model = ai::kDefaultOpenAiModel;
        std::string error;
        if (!impl_->ai_service->SaveSettings(impl_->ai_settings, error)) {
            impl_->ai_connection_status = ai::ErrorStatus(ai::AiErrorCode::SettingsError, error);
            return;
        }
        std::shared_ptr<ai::AiService> service = impl_->ai_service;
        impl_->ai_test_task =
            impl_->ai_task_runner.RunConnectionTest([service]() { return service->TestConnection(); });
    };
    callbacks.set_language = [](ui::Language language) { ui::SetCurrentLanguage(language); };
    callbacks.set_show_fps = [](bool show_fps) {
        if (HelloImGui::RunnerParams* runner_params = HelloImGui::GetRunnerParams()) {
            runner_params->imGuiWindowParams.showStatus_Fps = show_fps;
        }
    };
    callbacks.save_reviewer_name = [this](const char* reviewer_name) {
        impl_->reviewer_name = TrimWhitespace(reviewer_name ? reviewer_name : "");
        CopyToBuffer(impl_->reviewer_name_buf, sizeof(impl_->reviewer_name_buf), impl_->reviewer_name);
        impl_->modal_coordinator->show_reviewer_name_prompt = impl_->reviewer_name.empty();
        SetStatus(impl_->reviewer_name.empty() ? "Reviewer name is required for new reviews." : "Reviewer name saved.");
    };

    ui::panels::ShowPreferencesWindow(impl_->modal_coordinator->show_preferences_window, model, callbacks);
}

void AppRuntime::RenderThemeTweaksWindow() {
    if (!impl_->modal_coordinator->show_theme_tweak_window)
        return;
    HelloImGui::ShowThemeTweakGuiWindow(&impl_->modal_coordinator->show_theme_tweak_window);
}

void AppRuntime::RenderTreePanel(float left_w, float safety_tree_h, float top_y) {
    ImGui::SetNextWindowPos(ImVec2(0, top_y));
    ImGui::SetNextWindowSize(ImVec2(left_w, safety_tree_h));
    ImGui::Begin("Safety Case Tree", nullptr, kPanelFlags);
    ui::UiState& ui_state = ui::GetUiState();
    ui::ElementContextActions actions;
    if (impl_->proposal_controller->preview_active) {
        actions = ui::ElementContextActions{};
    } else if (impl_->proposal_controller->creator_active) {
        actions = ui::ElementContextActions{
            [this](core::NewElementKind kind) { AddProposalChildToSelected(kind); },
            [this]() { AddProposalTopGoal(); },
            [this](core::RemoveMode mode) { RemoveProposalSelected(mode); },
            nullptr,
            [this](const char* feature) {
                if (feature)
                    ShowNotImplementedModal(feature);
            },
        };
    } else {
        actions = MakeElementContextActions(*this);
    }
    ui::TreeEditActions tree_edit_actions{
        [this](const std::string& dragged_id, const std::string& target_id, core::TreeDropMode drop_mode) {
            return ValidateTreeDrop(dragged_id, target_id, drop_mode);
        },
        [this](const std::string& dragged_id, const std::string& target_id, core::TreeDropMode drop_mode) {
            return PerformTreeDrop(dragged_id, target_id, drop_mode);
        },
    };
    const parser::AssuranceCase* visible_case =
        impl_->IsProposalCanvasActive() ? &impl_->proposal_controller->preview_model : GetLoadedCase();
    const ui::TreeEditActions* edit_actions = impl_->IsProposalCanvasActive() ? nullptr : &tree_edit_actions;
    ui::ShowTreeViewPanel(
        impl_->current_tree.root ? &impl_->current_tree : nullptr, visible_case, ui_state, actions, edit_actions);
    ImGui::End();
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
            ImGuiTabItemFlags gsn_flags =
                (impl_->force_center_tab_selection && ui_state.center_view == ui::CenterView::GsnCanvas)
                    ? ImGuiTabItemFlags_SetSelected
                    : 0;
            if (ImGui::BeginTabItem(ui::Tr(ui::MessageId::GsnCanvas), nullptr, gsn_flags)) {
                ui_state.center_view = ui::CenterView::GsnCanvas;
                if (impl_->IsProposalCanvasActive()) {
                    const float banner_h = ImGui::GetStyle().WindowPadding.y * 2.0f + ImGui::GetTextLineHeight() +
                                           ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeight() +
                                           2.0f; // border pixels
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(IM_COL32(42, 45, 30, 255)));
                    ImGui::BeginChild(
                        "##proposal_preview_banner", ImVec2(0.0f, banner_h), true, ImGuiWindowFlags_NoScrollbar);
                    auto& proposals = *impl_->proposal_controller;
                    ImGui::TextUnformatted(proposals.creator_active ? "PROPOSAL CREATOR" : "PROPOSAL PREVIEW");
                    if (proposals.creator_active) {
                        ImGui::TextDisabled(
                            "Changes are recorded in the proposal draft. Save it from the review panel.");
                    } else {
                        ImGui::TextDisabled("This is a preview. The project model has not been changed.");
                    }
                    ImGui::SameLine();
                    if (proposals.creator_active) {
                        ImGui::TextDisabled("%d operation(s)", static_cast<int>(proposals.ActiveOperationCount()));
                        ImGui::SameLine();
                    } else if (!proposals.preview_id.empty()) {
                        if (ImGui::Button("Edit Proposal")) {
                            BeginEditProposalById(proposals.preview_id);
                        }
                        ImGui::SameLine();
                    }
                    const char* exit_label = proposals.creator_active ? "Discard Draft" : "Exit Preview";
                    if (ImGui::Button(exit_label)) {
                        const bool was_creator = proposals.creator_active;
                        proposals.ClearActiveState();
                        ClearProposalHighlightState(ui::GetUiState());
                        if (impl_->app_state.loaded_case.has_value()) {
                            impl_->current_tree = ui::gsn::BuildAssuranceTree(impl_->app_state.loaded_case.value());
                            ui::gsn::SetCanvasTree(impl_->current_tree);
                        } else {
                            impl_->tree_needs_rebuild = true;
                        }
                        if (was_creator)
                            SetStatus("Discarded proposal draft.");
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }
                ui::ElementContextActions actions;
                if (impl_->proposal_controller->preview_active) {
                    actions = ui::ElementContextActions{};
                } else if (impl_->proposal_controller->creator_active) {
                    actions = ui::ElementContextActions{
                        [this](core::NewElementKind kind) { AddProposalChildToSelected(kind); },
                        [this]() { AddProposalTopGoal(); },
                        [this](core::RemoveMode mode) { RemoveProposalSelected(mode); },
                        nullptr,
                        [this](const char* feature) {
                            if (feature)
                                ShowNotImplementedModal(feature);
                        },
                    };
                } else {
                    actions = MakeElementContextActions(*this);
                }
                const parser::AssuranceCase* visible_case =
                    impl_->IsProposalCanvasActive() ? &impl_->proposal_controller->preview_model : GetLoadedCase();
                ui_state.proposal_canvas_active = impl_->IsProposalCanvasActive();
                ui_state.attention_element_ids =
                    core::CollectAttentionElementIds(impl_->problems_manager.GetProblems());
                SyncReviewVisualStatesFromReviews();
                ui::gsn::ShowGsnCanvasContent(ui_state, visible_case, actions);
                ImGui::EndTabItem();
            }
        }

        if (impl_->show_cse_tab) {
            ImGuiTabItemFlags cse_flags =
                (impl_->force_center_tab_selection && ui_state.center_view == ui::CenterView::CseRegister)
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
            ImGuiTabItemFlags evidence_flags =
                (impl_->force_center_tab_selection && ui_state.center_view == ui::CenterView::EvidenceRegister)
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
    };
    ui::panels::ProblemsPanelCallbacks callbacks{
        [this](const core::ProblemItem& problem) {
            if (problem.element_id.empty())
                return;
            ui::GetUiState().selected_problem_element_id = problem.element_id;
            impl_->events.Emit(SelectionChangedEvent{problem.element_id, true});
            impl_->events.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
        },
    };

    ImGui::SetNextWindowPos(ImVec2(center_x, top_y));
    ImGui::SetNextWindowSize(ImVec2(center_w, problems_h));
    ImGui::Begin("Problems and Review", nullptr, kPanelFlags | ImGuiWindowFlags_NoTitleBar);

    if (ImGui::BeginTabBar("##problems_review_tabs")) {
        if (ImGui::BeginTabItem("Problems")) {
            ui::panels::ShowProblemsPanelContent(model, callbacks, false);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Review")) {
            RenderReviewPanelContent();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("AI Debug")) {
            RenderAiDebugPanelContent();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void AppRuntime::SyncReviewProblems() {
    app::SyncReviewProblems(impl_->problems_manager, impl_->review_controller->Items());
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

void AppRuntime::RenderElementPropertiesPanel(
    float center_x, float center_w, float right_w, float content_h, float top_y) {
    float right_x = center_x + center_w + kSplitterThickness;

    ImGui::SetNextWindowPos(ImVec2(right_x, top_y));
    ImGui::SetNextWindowSize(ImVec2(right_w, content_h));
    ImGui::Begin("Element Properties", nullptr, kPanelFlags);

    if (impl_->proposal_controller->creator_active) {
        RenderProposalElementEditor();
    } else if (impl_->proposal_controller->preview_active) {
        ImGui::TextWrapped("Proposal preview is active. Exit preview before editing element properties.");
    } else {
        parser::AssuranceCase* ac_ptr =
            impl_->app_state.loaded_case.has_value() ? &impl_->app_state.loaded_case.value() : nullptr;
        sacm::AssuranceCasePackage* sacm_ptr =
            impl_->app_state.sacm_package.has_value() ? &impl_->app_state.sacm_package.value() : nullptr;
        if (ui::panels::ShowElementPanel(ac_ptr, sacm_ptr)) {
            impl_->events.Emit(TreeDirtyEvent{});
            impl_->events.Emit(DocumentDirtyEvent{});
        }
    }

    ImGui::End();
}

void AppRuntime::RenderReviewPanelContent() {
    const ui::UiState& ui_state = ui::GetUiState();
    ui::panels::ReviewPanelModel model;
    auto& proposals = *impl_->proposal_controller;
    model.selected_element_id =
        proposals.creator_active ? proposals.draft.anchor_element_id : ui_state.selected_element_id;
    model.has_project = impl_->app_state.current_project.has_value();
    EnsureGuidelineCatalogLoaded(*impl_);
    if (impl_->guideline_catalog.has_value()) {
        for (const GuidelineCatalogEntry& entry : impl_->guideline_catalog->entries) {
            model.guideline_options.push_back(ui::panels::ReviewGuidelineOption{
                entry.id,
                entry.category,
                entry.title,
            });
        }
    } else {
        model.guideline_status = impl_->guideline_catalog_error;
    }
    if (proposals.creator_active) {
        model.active_proposal_review_item_id = proposals.draft.review_item_id;
        model.active_proposal_operation_count = proposals.ActiveOperationCount();
        model.active_proposal_can_save = proposals.CanSaveActiveDraft();
    }
    if (!model.selected_element_id.empty()) {
        model.review_items = impl_->review_controller->ItemsForElement(model.selected_element_id);
        bool has_blocking_problem = false;
        for (const core::ProblemItem& problem : impl_->problems_manager.GetProblems()) {
            if (problem.element_id != model.selected_element_id)
                continue;
            if (IsReviewDerivedProblem(problem))
                continue;
            model.problem_items.push_back(problem);
            has_blocking_problem = true;
        }
        const core::reviews::ElementReviewState element_review_state =
            impl_->review_controller->ElementReviewStateForElement(model.selected_element_id);
        model.manual_review_ok = element_review_state.manual_ok;
        model.ai_review_ok = element_review_state.ai_ok;
        model.ai_review_failed = element_review_state.failed;
        const app::controllers::ElementReviewStatus review_status =
            impl_->review_controller->StatusForElement(model.selected_element_id, has_blocking_problem);
        model.review_status_passed = review_status == app::controllers::ElementReviewStatus::Passed;
        switch (review_status) {
        case app::controllers::ElementReviewStatus::Passed:
            model.review_status_text = "Passed";
            model.review_status_detail = element_review_state.manual_ok ? "Manual review OK." : "AI review OK.";
            break;
        case app::controllers::ElementReviewStatus::Failed:
            model.review_status_text = "Not OK";
            model.review_status_detail = element_review_state.last_review_message.empty()
                                             ? "AI review failed."
                                             : element_review_state.last_review_message;
            break;
        case app::controllers::ElementReviewStatus::OpenItems:
            model.review_status_text = "Not OK";
            model.review_status_detail =
                has_blocking_problem ? "Open problems require attention." : "Open review comments require attention.";
            break;
        case app::controllers::ElementReviewStatus::NotReviewed:
            model.review_status_text = "Not reviewed";
            model.review_status_detail = "Set Manual review OK or run an AI review with no findings.";
            break;
        }
        for (const core::reviews::ReviewItem& item : model.review_items) {
            if (!item.proposal_id.has_value())
                continue;
            std::string error;
            std::optional<core::reviews::ReviewProposal> proposal =
                proposals.manager.LoadProposal(item.proposal_id.value(), error);
            if (!proposal.has_value()) {
                model.proposal_validity[item.proposal_id.value()] = {core::reviews::ProposalValidity::Broken, error};
            } else if (impl_->app_state.loaded_case.has_value()) {
                model.proposal_validity[item.proposal_id.value()] = core::reviews::EvaluateReviewProposalValidity(
                    proposal.value(), impl_->app_state.loaded_case.value());
            } else {
                model.proposal_validity[item.proposal_id.value()] = {core::reviews::ProposalValidity::Broken,
                                                                     "No SACM model is loaded."};
            }
        }
    }

    ui::panels::ReviewPanelCallbacks callbacks;
    callbacks.add_review_item =
        [this](const std::string& title, const std::string& message, const std::vector<std::string>& guideline_ids) {
            if (impl_->proposal_controller->creator_active) {
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
                impl_->modal_coordinator->show_reviewer_name_prompt = true;
                SetStatus("Enter a reviewer name before adding review comments.");
                return;
            }
            const std::string element_id = ui::GetUiState().selected_element_id;
            if (element_id.empty()) {
                SetStatus("Select an element before adding a review comment.");
                return;
            }

            std::vector<std::string> validated_guideline_ids;
            if (!guideline_ids.empty()) {
                EnsureGuidelineCatalogLoaded(*impl_);
                if (!impl_->guideline_catalog.has_value()) {
                    SetStatus("SCCG guidelines are not available: " + impl_->guideline_catalog_error);
                    return;
                }

                std::unordered_set<std::string> seen_guideline_ids;
                for (const std::string& guideline_id : guideline_ids) {
                    if (guideline_id.empty() || seen_guideline_ids.count(guideline_id) > 0)
                        continue;
                    if (impl_->guideline_catalog->ids.count(guideline_id) == 0) {
                        SetStatus("Unknown SCCG guideline id: " + guideline_id);
                        return;
                    }
                    validated_guideline_ids.push_back(guideline_id);
                    seen_guideline_ids.insert(guideline_id);
                }
            }

            core::reviews::ReviewItem item;
            item.id = GenerateReviewItemId();
            item.element_id = element_id;
            item.title = title;
            item.message = message;
            item.severity = "warning";
            item.reviewer_name = impl_->reviewer_name;
            item.guideline_ids = std::move(validated_guideline_ids);
            item.source = core::reviews::ReviewItemSource::Manual;
            item.status = core::reviews::ReviewItemStatus::Open;
            item.created_utc = NowUtcString();
            item.updated_utc = item.created_utc;

            impl_->review_controller->AddManualItem(std::move(item));
        };
    callbacks.create_proposed_change = [this](const core::reviews::ReviewItem& item) {
        BeginProposalForReviewItem(item);
    };
    callbacks.save_proposal = [this](const core::reviews::ReviewItem& item) { SaveActiveProposal(item); };
    callbacks.edit_proposal = [this](const core::reviews::ReviewItem& item) { BeginEditProposalForReviewItem(item); };
    callbacks.preview_proposal = [this](const core::reviews::ReviewItem& item) {
        if (!item.proposal_id.has_value()) {
            SetStatus("This review comment has no proposed change to preview.");
            return;
        }
        PreviewProposalById(item.proposal_id.value());
    };
    callbacks.apply_proposal = [this](const core::reviews::ReviewItem& item) {
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
        RebuildSacmArgumentPackageFromParser(impl_->app_state.loaded_case.value(),
                                             impl_->app_state.sacm_package.value());
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
    };
    callbacks.delete_proposal = [this](const core::reviews::ReviewItem& item) {
        if (impl_->proposal_controller->creator_active) {
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

        if (!impl_->review_controller->ClearProposal(item.id)) {
            SetStatus("Proposal deleted, but review link update failed.");
            return;
        }
        CloseProposalPreviewIfOpen(item.proposal_id.value());

        core::ProjectService::RefreshFileStatus(project);
        SetStatus("Deleted proposed change " + item.proposal_id.value() + ".");
    };
    callbacks.resolve_review_item = [this](const core::reviews::ReviewItem& item) { ResolveReviewItem(item); };
    callbacks.delete_review_item = [this](const core::reviews::ReviewItem& item) { BeginDeleteReviewItem(item); };
    callbacks.delete_problem = [this](const core::ProblemItem& problem) {
        impl_->problems_manager.RemoveProblem(problem.id);
        SyncReviewVisualStatesFromReviews();
        SetStatus("Problem deleted.");
    };
    callbacks.set_manual_review_ok = [this, element_id = model.selected_element_id](bool manual_ok) {
        SetManualReviewOk(element_id, manual_ok);
    };
    ui::panels::ShowReviewPanel(model, callbacks);
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
    project_model.project =
        impl_->app_state.current_project.has_value() ? &impl_->app_state.current_project.value() : nullptr;
    if (project_model.project) {
        const parser::AssuranceCase* loaded_case =
            impl_->app_state.loaded_case.has_value() ? &impl_->app_state.loaded_case.value() : nullptr;
        for (const core::reviews::ReviewProposalSummary& summary :
             impl_->proposal_controller->manager.ListProposals(loaded_case)) {
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
    RenderRemoveConfirmModal();
    RenderDeleteReviewItemConfirmModal();
    RenderCreateProjectModal();
    RenderProjectFileNameModal();
    RenderProjectLoadReportModal();
    RenderSaveBeforeExitModal(done);
    RenderStartupProjectWindow();
    RenderNotImplementedModal();
    RenderReviewerNamePromptModal();
}

} // namespace app
