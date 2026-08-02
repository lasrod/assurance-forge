#include "app/app_runtime.h"

#include "app/actions/element_actions.h"
#include "app/actions/proposal_actions.h"
#include "app/actions/review_actions.h"
#include "app/actions/terminology_actions.h"
#include "app/app_runtime_internal.h"
#include "app/areas/ai_debug_area.h"
#include "core/audit/audit_snapshot.h"
#include "app/areas/argument_navigator_area.h"
#include "app/areas/feedback_dock_area.h"
#include "app/areas/inspector_area.h"
#include "app/areas/modal_host.h"
#include "app/areas/proposal_editor_area.h"
#include "app/areas/project_explorer_area.h"
#include "app/areas/review_panel_area.h"
#include "app/acp_problem_sync.h"
#include "app/areas/workbench_area.h"
#include "app/app_runtime_state.h"
#include "app/confidence_problem_sync.h"
#include "app/frame/app_menu_bar.h"
#include "app/frame/app_shell.h"
#include "app/proposal_ui_state.h"
#include "app/project_workflow.h"
#include "app/recent_projects.h"
#include "app/register_problem_sync.h"
#include "app/review_problem_sync.h"
#include "app/terminology_problem_sync.h"
#include "app/structure_problem_sync.h"
#include "core/acp/assurance_claim_point.h"
#include "core/app_state.h"
#include "core/derived_views.h"
#include "core/element_factory.h"
#include "core/problems/problem_attention.h"
#include "core/problems/problems_manager.h"
#include "core/reviews/review_proposal_manager.h"
#include "core/string_utils.h"
#include "core/terminology_package_service.h"
#include "core/terminology_scope_service.h"
#include "core/time_utils.h"
#include "imgui.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/imgui_buffer_utils.h"
#include "ui/panels/sacm_viewer_panel.h"
#include "ui/register_views.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace app {
namespace {

// Collect every element id/gid that belongs to a confidence argument package. These elements
// (the confidence top-goal claim and any other claims/reasonings/relationships authored inside
// the separate confidence tree) must be hidden from the main GSN tree; they only belong on the
// confidence argument package canvas tab.
void CollectConfidencePackageElementIdentities(const sacm::AssuranceCasePackage& package,
                                               std::unordered_set<std::string>& ids,
                                               std::unordered_set<std::string>& gids) {
    auto add = [&](const sacm::SacmElement& element) {
        if (!element.id.empty())
            ids.insert(element.id);
        if (!element.gid.empty())
            gids.insert(element.gid);
    };
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        if (!core::acp::IsConfidenceArgumentPackage(argument_package))
            continue;
        for (const sacm::Claim& claim : argument_package.claims)
            add(claim);
        for (const sacm::ArgumentReasoning& reasoning : argument_package.argumentReasonings)
            add(reasoning);
        for (const sacm::AssertedInference& inference : argument_package.assertedInferences)
            add(inference);
        for (const sacm::AssertedContext& context : argument_package.assertedContexts)
            add(context);
        for (const sacm::AssertedEvidence& evidence : argument_package.assertedEvidences)
            add(evidence);
        for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences)
            add(artifact_reference);
    }
}

std::optional<parser::AssuranceCase>
FilterConfidencePackageElementsFromMainTree(const parser::AssuranceCase& source,
                                            const sacm::AssuranceCasePackage* package) {
    if (!package)
        return std::nullopt;
    std::unordered_set<std::string> hidden_ids;
    std::unordered_set<std::string> hidden_gids;
    CollectConfidencePackageElementIdentities(*package, hidden_ids, hidden_gids);
    if (hidden_ids.empty() && hidden_gids.empty())
        return std::nullopt;

    parser::AssuranceCase filtered;
    filtered.id = source.id;
    filtered.name = source.name;
    filtered.description = source.description;
    filtered.elements.reserve(source.elements.size());
    for (const parser::SacmElement& element : source.elements) {
        if (hidden_ids.count(element.id) > 0)
            continue;
        if (!element.gid.empty() && hidden_gids.count(element.gid) > 0)
            continue;
        filtered.elements.push_back(element);
    }
    filtered.acps = source.acps;
    if (filtered.elements.size() == source.elements.size())
        return std::nullopt;
    return filtered;
}

} // namespace

std::string ReviewItemIdFromProblemId(const std::string& problem_id) {
    constexpr const char* kReviewPrefix = "review-comment:";
    constexpr const char* kGuidelinePrefix = "guideline-review:";

    if (problem_id.rfind(kReviewPrefix, 0) == 0)
        return problem_id.substr(std::char_traits<char>::length(kReviewPrefix));

    if (problem_id.rfind(kGuidelinePrefix, 0) == 0) {
        const size_t start = std::char_traits<char>::length(kGuidelinePrefix);
        const size_t end = problem_id.find(':', start);
        if (end == std::string::npos)
            return problem_id.substr(start);
        return problem_id.substr(start, end - start);
    }

    return {};
}

ui::ElementContextActions MakeElementContextActions(AppRuntime& runtime) {
    ui::ElementContextActions actions{
        [&runtime](core::NewElementKind kind) { runtime.AddChildToSelected(kind); },
        [&runtime]() { runtime.AddTopGoal(); },
        [&runtime]() { runtime.AddAcpToSelectedElement(); },
        [&runtime](const std::string& relationship_id) { runtime.AddAcpToRelationship(relationship_id); },
        [&runtime](const std::string& acp_id) { runtime.RemoveAcp(acp_id); },
        [&runtime](core::RemoveMode mode) { runtime.RemoveSelected(mode); },
        [&runtime]() { runtime.RenderAiReviewContextMenuForSelected(); },
        [&runtime](const char* feature) {
            if (feature)
                runtime.ShowNotImplementedModal(feature);
        },
    };
    actions.set_status = [&runtime](const std::string& message) { runtime.SetStatus(message); };
    actions.focus_problem = [](const std::string& problem_id, const std::string& element_id) {
        ui::FocusProblemInPanel(ui::GetUiState(), problem_id, element_id);
    };
    actions.add_counter_argument = [&runtime]() { runtime.AddCounterArgumentToSelected(); };
    actions.add_counter_evidence = [&runtime]() { runtime.AddCounterEvidenceToSelected(); };
    actions.add_counter_argument_to_relationship = [&runtime](const std::string& relationship_id) {
        runtime.AddCounterArgumentToRelationship(relationship_id);
    };
    actions.add_counter_evidence_to_relationship = [&runtime](const std::string& relationship_id) {
        runtime.AddCounterEvidenceToRelationship(relationship_id);
    };
    actions.remove_relationship = [&runtime](const std::string& relationship_id) {
        runtime.RemoveRelationship(relationship_id);
    };
    return actions;
}

AppRuntime::AppRuntime() : impl_(std::make_unique<AppRuntimeState>()) {
    RegisterAppEventListeners();

    impl_->current_tree = core::AssuranceTree();
    ui::gsn::SetCanvasTree(impl_->current_tree);
    ui::RebuildRegisterViews(nullptr, impl_->register_controller->Store());

    ui::UiState& ui_state = ui::GetUiState();
    ui_state.center_view = ui::CenterView::GsnCanvas;
    ui_state.selected_element_id.clear();
    ui_state.selected_acp_id.clear();
    ui_state.selected_relationship_id.clear();
    ui_state.selected_relationship_edge_key.clear();
    ui_state.selected_problem_id.clear();
    ui_state.selected_problem_element_id.clear();
}

AppRuntime::~AppRuntime() = default;

void AppRuntime::RegisterAppEventListeners() {
    impl_->events.Subscribe<StatusMessageEvent>(
        [this](const StatusMessageEvent& event) { impl_->app_state.status_message = event.message; });
    impl_->events.Subscribe<AutosaveFailedEvent>(
        [this](const AutosaveFailedEvent& event) { impl_->last_autosave_error = event.error; });
    impl_->events.Subscribe<TreeDirtyEvent>([this](const TreeDirtyEvent& event) {
        impl_->tree_needs_rebuild = event.dirty;
        impl_->tree_edit_index_valid = false;
        if (event.focus_root)
            impl_->workbench.pending_focus_root = true;
    });
    impl_->events.Subscribe<DocumentDirtyEvent>([this](const DocumentDirtyEvent& event) {
        // The command bus autosaves, but callers emit this event afterwards and
        // cannot know that. Honouring `mark_app_dirty` unconditionally is what
        // made the app report unsaved work that was already on disk. The
        // revision still bumps, so derived views rebuild either way.
        const bool already_persisted = impl_->autosave_persisted_pending_edit;
        impl_->autosave_persisted_pending_edit = false;
        // document_dirty gates the save-before-open/close prompts and the
        // needs_sacm_save check, so it has to respect the autosave too --
        // otherwise the user is asked to save work the bus already wrote.
        impl_->document_dirty = already_persisted ? false : event.dirty;
        if (event.mark_app_dirty && !already_persisted)
            impl_->app_state.mark_dirty();
        else
            impl_->app_state.bump_case_revision();
    });
    impl_->events.Subscribe<ProjectFilesChangedEvent>(
        [this](const ProjectFilesChangedEvent&) { impl_->sacm_package_tree_cache.clear(); });
    impl_->events.Subscribe<ReviewItemsDirtyEvent>([this](const ReviewItemsDirtyEvent& event) {
        if (event.mark_app_dirty)
            impl_->app_state.mark_dirty();
        impl_->problems_dirty.review = true;
    });
    impl_->events.Subscribe<ConfidenceDirtyEvent>([this](const ConfidenceDirtyEvent& event) {
        if (event.mark_app_dirty)
            impl_->app_state.mark_dirty();
        impl_->problems_dirty.confidence = true;
    });
    impl_->events.Subscribe<RegisterAssessmentsDirtyEvent>([this](const RegisterAssessmentsDirtyEvent& event) {
        if (event.mark_app_dirty)
            impl_->app_state.mark_dirty();
        impl_->problems_dirty.registers = true;
    });
    impl_->events.Subscribe<SelectionChangedEvent>([](const SelectionChangedEvent& event) {
        ui::UiState& ui_state = ui::GetUiState();
        ui_state.selected_element_id = event.element_id;
        ui_state.selected_acp_id.clear();
        ui_state.selected_relationship_id.clear();
        ui_state.selected_relationship_edge_key.clear();
        ui_state.center_on_selection = event.center_on_selection;
    });
    impl_->events.Subscribe<ElementReviewVisualEvent>([](const ElementReviewVisualEvent& event) {
        ui::UiState& ui_state = ui::GetUiState();
        switch (event.kind) {
        case ElementReviewVisualEventKind::AiStarted:
            ui::BeginAiReviewSpinner(ui_state, event.element_id, event.review_scope_element_ids);
            break;
        case ElementReviewVisualEventKind::AiNoFindings:
        case ElementReviewVisualEventKind::AiFindings:
        case ElementReviewVisualEventKind::AiFailed:
            ui::EndAiReviewSpinner(ui_state, event.element_id);
            break;
        case ElementReviewVisualEventKind::ManualOk:
            // No badge change required - problems panel reflects status via
            // the underlying ProblemsManager state, which is the single source
            // of truth for badges.
            break;
        }
    });
    impl_->events.Subscribe<AiReviewProposalSuggestionsEvent>([this](const AiReviewProposalSuggestionsEvent& event) {
        actions::ProposalActions(*impl_).CreateAiGenerated(event.suggestions);
    });
    impl_->events.Subscribe<ArgumentPackageCanvasRequestEvent>([this](const ArgumentPackageCanvasRequestEvent& event) {
        OpenArgumentPackageCanvas(event.package_id, event.package_gid, event.display_name, event.focus_element_id);
    });
    impl_->events.Subscribe<CenterRequestEvent>([this](const CenterRequestEvent& event) {
        ui::UiState& ui_state = ui::GetUiState();
        switch (event.view) {
        case CenterViewRequest::Preserve:
            break;
        case CenterViewRequest::ProjectOverview:
            ui_state.center_view = ui::CenterView::ProjectOverview;
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
        ui_state.proposal_text_changes.clear();
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

bool AppRuntime::AddAcpToSelectedElement() {
    return actions::ElementActions(*impl_).AddAcpToSelectedElement();
}

bool AppRuntime::AddAcpToRelationship(const std::string& relationship_id) {
    return actions::ElementActions(*impl_).AddAcpToRelationship(relationship_id);
}

bool AppRuntime::RemoveAcp(const std::string& acp_id) {
    return actions::ElementActions(*impl_).RemoveAcp(acp_id);
}

bool AppRuntime::AddCounterArgumentToSelected() {
    return actions::ElementActions(*impl_).AddChallengeToSelectedElement(core::ChallengeSourceType::CounterArgument);
}

bool AppRuntime::AddCounterEvidenceToSelected() {
    return actions::ElementActions(*impl_).AddChallengeToSelectedElement(core::ChallengeSourceType::CounterEvidence);
}

bool AppRuntime::AddCounterArgumentToRelationship(const std::string& relationship_id) {
    return actions::ElementActions(*impl_).AddChallengeToRelationship(relationship_id,
                                                                      core::ChallengeSourceType::CounterArgument);
}

bool AppRuntime::AddCounterEvidenceToRelationship(const std::string& relationship_id) {
    return actions::ElementActions(*impl_).AddChallengeToRelationship(relationship_id,
                                                                      core::ChallengeSourceType::CounterEvidence);
}

void AppRuntime::RemoveSelected(core::RemoveMode mode) {
    actions::ElementActions(*impl_).RemoveSelected(mode);
}

bool AppRuntime::RemoveRelationship(const std::string& relationship_id) {
    return impl_->element_edit_controller->RemoveRelationship(*impl_, relationship_id);
}

bool AppRuntime::DropRelationshipReference(const std::string& relationship_id, const std::string& reference) {
    return impl_->element_edit_controller->DropRelationshipReference(*impl_, relationship_id, reference);
}

bool AppRuntime::MoveStrategyToReasoning(const std::string& relationship_id, const std::string& strategy_id) {
    return impl_->element_edit_controller->MoveStrategyToReasoning(*impl_, relationship_id, strategy_id);
}

bool AppRuntime::SetElementUndeveloped(const std::string& element_id, bool undeveloped) {
    return impl_->element_edit_controller->SetElementUndeveloped(*impl_, element_id, undeveloped);
}

bool AppRuntime::RenumberGsnIdentifier(const std::string& element_id) {
    return impl_->element_edit_controller->RenumberGsnIdentifier(*impl_, element_id);
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
    return actions::ReviewActions(*impl_).ResolveReviewItem(item, core::NowUtcString());
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

const parser::AssuranceCase& AppRuntime::RefreshAgentChangePreview(const parser::AssuranceCase& committed) {
    ui::UiState& ui_state = ui::GetUiState();

    // Only change sets written against the argument that is open. Element ids
    // repeat across a project's arguments, and without this filter a change set
    // built against `main2.sacm` decorated `main.sacm`'s G1 -- the overlay
    // pointing at an element the agent had never looked at.
    std::vector<const core::changesets::ChangeSet*> open;
    for (const core::changesets::ChangeSet* candidate : impl_->agent_change_sets.Open()) {
        if (core::changesets::ChangeSetTargetsArgumentFile(*candidate, impl_->app_state.loaded_file_path)) {
            open.push_back(candidate);
        }
    }
    if (open.empty()) {
        ui_state.agent_change_status.clear();
        ui_state.agent_change_set_id.clear();
        ui_state.agent_change_set_title.clear();
        impl_->agent_preview_case.reset();
        impl_->agent_preview_added_ids.clear();
        return committed;
    }

    // One canvas can only show one proposal legibly. Two connected clients are
    // supported, but the newest open change set is the one drawn; the Review
    // panel lists them all so the other is not hidden.
    const core::changesets::ChangeSet& shown = *open.back();
    const core::changesets::ChangeSetDiff diff = core::changesets::ComputeChangeSetDiff(shown, committed);
    if (!diff.success) {
        // The argument moved under the change set. Showing a preview that cannot
        // be applied would invite the user to accept something that will be
        // refused, so fall back to the committed model; the Review panel reports
        // why.
        ui_state.agent_change_status.clear();
        impl_->agent_preview_case.reset();
        impl_->agent_preview_added_ids.clear();
        // Nothing of this change set is on the canvas, so nothing may claim it
        // is. These two fields are how the Review panel decides to print "Shown
        // on the canvas as you watch it build", and setting them here told the
        // reviewer a change set was being drawn while the canvas showed the
        // committed argument. The panel says why it does not apply from the
        // acceptability check, which does not go through here.
        ui_state.agent_change_set_id.clear();
        ui_state.agent_change_set_title.clear();
        return committed;
    }

    ui_state.agent_change_set_id = shown.id;
    ui_state.agent_change_set_title = shown.title;
    ui_state.agent_change_status.clear();
    impl_->agent_preview_added_ids.clear();
    for (const std::pair<const std::string, core::changesets::ElementChange>& entry : diff.status_by_id) {
        if (entry.second != core::changesets::ElementChange::Unchanged) {
            ui_state.agent_change_status[entry.first] = entry.second;
        }
        if (entry.second == core::changesets::ElementChange::Added) {
            impl_->agent_preview_added_ids.push_back(entry.first);
        }
    }

    impl_->agent_preview_case = diff.preview_model;
    // A removed element is absent from the preview by definition, so it is put
    // back for display only. A reviewer being asked to approve a deletion should
    // be able to see what is being deleted, in place, rather than infer it from
    // a gap.
    for (const parser::SacmElement& removed : diff.removed) {
        impl_->agent_preview_case->elements.push_back(removed);
    }
    return impl_->agent_preview_case.value();
}

const core::drafts::DraftWorkspace* AppRuntime::CurrentDraftWorkspace() const {
    return impl_->draft_workspace.workspace();
}

void AppRuntime::SyncDraftWorkspace() {
    const std::filesystem::path root = impl_->app_state.current_project.has_value()
                                           ? impl_->app_state.current_project->rootPath
                                           : std::filesystem::path{};
    const std::filesystem::path argument =
        impl_->app_state.loaded_case.has_value() ? impl_->app_state.loaded_file_path : std::filesystem::path{};

    if (root == impl_->draft_workspace_root && argument == impl_->draft_workspace_argument) {
        return;
    }

    impl_->draft_workspace_root = root;
    impl_->draft_workspace_argument = argument;
    impl_->draft_workspace.SetProjectRoot(root);

    if (argument.empty() || !impl_->app_state.loaded_case.has_value()) {
        // Forgets the workspace without touching what is on disk, so the draft
        // is still there when the argument is opened again. Closing the
        // application is not a decision about unaccepted work.
        impl_->draft_workspace.Close();
        return;
    }

    std::string error;
    if (!impl_->draft_workspace.Open(argument, impl_->app_state.loaded_case.value(), error)) {
        // Recovery data that cannot be read is reported rather than deleted. The
        // work in it may be hours of an agent's conversation, and the file is the
        // only copy.
        impl_->app_state.status_message = "Warning: could not read the draft for this argument: " + error;
    }
}

void AppRuntime::RebuildDerivedViewsIfNeeded() {
    // A library-primary (flipped) command committed its edit to the library but
    // deliberately did NOT rebuild the live loaded_case/sacm_package inside the
    // command bus -- doing so mid-dispatch replaces containers the canvas is still
    // rendering from this frame (it holds &loaded_case across the frame, and a
    // context-menu edit dispatches mid-render). Re-derive them here, at the top of
    // the frame before any panel renders -- the same deferred-to-next-frame remedy
    // `pending_reconcile_audit_store` uses for the same class of hazard. This runs
    // even when the proposal canvas is active (the edit is committed regardless).
    if (impl_->rederive_views_from_library) {
        impl_->rederive_views_from_library = false;
        if (impl_->app_state.library_document && impl_->app_state.loaded_case.has_value() &&
            impl_->app_state.sacm_package.has_value()) {
            core::RebuildDerivedViewsFromLibrary(*impl_->app_state.library_document,
                                                 impl_->app_state.loaded_case.value(),
                                                 impl_->app_state.sacm_package.value());
        }
    }

    if (impl_->IsProposalCanvasActive()) {
        return;
    }

    if (impl_->tree_needs_rebuild && !impl_->app_state.loaded_case.has_value()) {
        ui::RebuildRegisterViews(nullptr, impl_->register_controller->Store());
        // No argument means nothing to be orphaned against; drop the warnings
        // rather than leave them standing against a model that is gone.
        impl_->problems_dirty.registers = true;
        impl_->tree_edit_index = core::TreeEditIndex();
        impl_->tree_edit_index_valid = false;
        impl_->tree_needs_rebuild = false;
        return;
    }

    if (!impl_->app_state.loaded_case.has_value() || !impl_->tree_needs_rebuild) {
        return;
    }

    // While a connected AI client has a change set open, the canvas draws the
    // *preview* -- the argument as it would be if the user accepted -- with each
    // touched node marked. That is what makes the agent's work visible where it
    // lands, rather than as a list of operations beside the diagram. The
    // committed model is untouched; nothing here is saved.
    const parser::AssuranceCase& committed = *impl_->app_state.loaded_case;
    const parser::AssuranceCase& ac = RefreshAgentChangePreview(committed);

    const sacm::AssuranceCasePackage* sacm_package =
        impl_->app_state.has_projected_package() ? &impl_->app_state.projected_package() : nullptr;
    const std::optional<parser::AssuranceCase> filtered_case =
        FilterConfidencePackageElementsFromMainTree(ac, sacm_package);
    impl_->current_tree =
        ui::gsn::BuildAssuranceTree(filtered_case ? *filtered_case : ac, ui::GetUiState().active_secondary_lang);
    core::ApplyTreeDisplayOrder(impl_->current_tree, impl_->tree_display_order);
    impl_->tree_edit_index = core::BuildTreeEditIndex(ac);
    impl_->tree_edit_index_valid = true;
    ui::gsn::SetCanvasTree(impl_->current_tree);
    ui::RebuildRegisterViews(&ac, impl_->register_controller->Store());
    ui::GetUiState().model_has_translations = ui::ModelHasTranslations(ac);
    impl_->problems_dirty.terminology = true;
    impl_->problems_dirty.acp = true;
    impl_->problems_dirty.translation = true;
    // Any edit to the support structure can create or break a cycle, and this
    // rebuild runs on exactly those edits.
    impl_->problems_dirty.structure = true;
    // The same edits move claim-evidence pairings in and out of the argument,
    // which is what makes a stored assessment orphaned or whole again.
    impl_->problems_dirty.registers = true;

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
            ui::RebuildRegisterViews(nullptr, impl_->register_controller->Store());
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
                impl_->current_tree = ui::gsn::BuildAssuranceTree(impl_->app_state.loaded_case.value(),
                                                                  ui::GetUiState().active_secondary_lang);
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
        [this]() {
            std::vector<ui::panels::IgnoredTerminologyEntry> entries;
            for (const actions::IgnoredSuggestionView& view : ListIgnoredTerminologySuggestions())
                entries.push_back(ui::panels::IgnoredTerminologyEntry{view.element_id, view.term});
            return entries;
        },
        [this](const std::string& element_id, const std::string& term) {
            RestoreTerminologySuggestion(element_id, term);
        },
        [this]() { impl_->pending_reconcile_audit_store = true; },
    };
}

void AppRuntime::RefreshDirtyProblems() {
    ProblemSyncDirty& dirty = impl_->problems_dirty;
    if (dirty.review) {
        SyncReviewProblems();
        dirty.review = false;
    }
    if (dirty.terminology) {
        SyncTerminologyProblems();
        dirty.terminology = false;
    }
    if (dirty.confidence) {
        SyncConfidenceProblems();
        dirty.confidence = false;
    }
    if (dirty.acp) {
        SyncAcpProblems();
        dirty.acp = false;
    }
    if (dirty.translation) {
        SyncTranslationReviewProblems();
        dirty.translation = false;
    }
    if (dirty.structure) {
        SyncStructureProblems();
        dirty.structure = false;
    }
    if (dirty.registers) {
        SyncRegisterProblems();
        dirty.registers = false;
    }
}

void AppRuntime::SyncReviewProblems() {
    app::SyncReviewProblems(impl_->problems_manager, impl_->review_controller->Items());
}

void AppRuntime::SyncTerminologyProblems() {
    const parser::AssuranceCase* model =
        impl_->app_state.loaded_case.has_value() ? &impl_->app_state.loaded_case.value() : nullptr;
    const sacm::AssuranceCasePackage* package =
        impl_->app_state.has_projected_package() ? &impl_->app_state.projected_package() : nullptr;
    app::SyncTerminologyProblems(
        impl_->problems_manager, model, package, [this](const std::string& element_id, const std::string& term_value) {
            return IsTerminologySuggestionIgnored(element_id, term_value);
        });
}

void AppRuntime::SyncConfidenceProblems() {
    const parser::AssuranceCase* model =
        impl_->app_state.loaded_case.has_value() ? &impl_->app_state.loaded_case.value() : nullptr;
    const core::confidence::ConfidenceStore* store =
        impl_->confidence_controller ? &impl_->confidence_controller->Store() : nullptr;
    const std::string source_id =
        impl_->confidence_controller ? impl_->confidence_controller->ActiveSourceId() : std::string{};
    app::SyncConfidenceProblems(impl_->problems_manager, model, store, source_id);
}

void AppRuntime::SyncAcpProblems() {
    const parser::AssuranceCase* model =
        impl_->app_state.loaded_case.has_value() ? &impl_->app_state.loaded_case.value() : nullptr;
    const sacm::AssuranceCasePackage* package =
        impl_->app_state.has_projected_package() ? &impl_->app_state.projected_package() : nullptr;
    app::SyncAcpProblems(impl_->problems_manager, model, package);
}

void AppRuntime::SyncStructureProblems() {
    const parser::AssuranceCase* model =
        impl_->app_state.loaded_case.has_value() ? &impl_->app_state.loaded_case.value() : nullptr;
    app::SyncStructureProblems(impl_->problems_manager, model);
}

void AppRuntime::SyncRegisterProblems() {
    const parser::AssuranceCase* model =
        impl_->app_state.loaded_case.has_value() ? &impl_->app_state.loaded_case.value() : nullptr;
    const core::registers::RegisterStore* store =
        impl_->register_controller ? &impl_->register_controller->Store() : nullptr;
    app::SyncRegisterProblems(impl_->problems_manager, model, store);
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
    if (!impl_->review_controller->SetManualReviewOk(
            element_id, manual_ok, impl_->reviewer_name, core::NowUtcString())) {
        SetStatus("Could not update manual review status.");
        return false;
    }
    return true;
}

void AppRuntime::RequestExit(bool& done) {
    // Commit any in-progress inspector text edit as an audited transaction
    // before anything else. A focused field that never saw ImGui's
    // deactivation transition (the user closed the window mid-edit, or
    // navigated away programmatically) has already mutated the model; if it
    // only reached disk via the un-audited SaveProject path below it would be
    // flagged as an audit-log divergence on the next open. Doing it here also
    // ensures the edit is persisted even when nothing else marked the project
    // dirty.
    FlushPendingTextEdits();

    // Stop serving AI clients before anything is flushed or torn down. An
    // operation arriving mid-shutdown would run against half-closed state, and
    // the endpoint record must not outlive the process that published it -- a
    // stale one sends the next adapter to a pipe nobody is listening on.
    if (impl_->agent_bridge != nullptr) {
        impl_->agent_bridge->Stop();
    }

    // Autosave on close: best-effort flush before exiting so the on-disk SACM
    // matches the user's latest in-memory edits. CommandBus already autosaves
    // per-transaction; SaveProject covers the remaining bypass paths (review
    // controller, confidence controller, and any mutation that only sets
    // document_dirty / has_unsaved_changes). If the flush fails, fall back to
    // the legacy save-before-exit modal so the user can decide what to do
    // instead of silently losing data.
    if (impl_->app_state.has_unsaved_changes) {
        if (!SaveProject()) {
            impl_->last_autosave_error = "Auto-flush on close failed: " + impl_->app_state.status_message;
            impl_->modal_coordinator->show_save_before_exit_modal = true;
            return;
        }
    }

    // Take a close-time snapshot so the next session has a natural boundary
    // for undo / history navigation at "this session's close." CreateUserSnapshot
    // derives its id from the latest transaction sequence, so a session that
    // produced no new transactions reuses the existing snapshot id and the call
    // is naturally a no-op. Failures here are non-fatal — close still proceeds.
    if (impl_->app_state.current_project.has_value()) {
        core::audit::SnapshotMetadata snapshot;
        std::string snapshot_error;
        const std::string created_by = impl_->reviewer_name.empty() ? std::string("system") : impl_->reviewer_name;
        (void)core::audit::CreateUserSnapshot(impl_->app_state.current_project->rootPath,
                                              "automatic close snapshot",
                                              created_by,
                                              snapshot,
                                              snapshot_error);
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
    impl_->reviewer_name = core::TrimWhitespace(content);
    ui::CopyToBuffer(impl_->reviewer_name_buf, sizeof(impl_->reviewer_name_buf), impl_->reviewer_name);
    impl_->modal_coordinator->show_reviewer_name_prompt = impl_->reviewer_name.empty();
}

std::string AppRuntime::ReviewerNamePreference() const {
    return impl_->reviewer_name;
}

} // namespace app
