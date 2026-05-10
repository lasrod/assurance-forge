#include "app/app_runtime.h"

#include "app/actions/element_actions.h"
#include "app/actions/proposal_actions.h"
#include "app/actions/review_actions.h"
#include "app/areas/ai_debug_area.h"
#include "app/areas/argument_navigator_area.h"
#include "app/areas/feedback_dock_area.h"
#include "app/areas/inspector_area.h"
#include "app/areas/modal_host.h"
#include "app/areas/proposal_editor_area.h"
#include "app/areas/project_explorer_area.h"
#include "app/areas/review_panel_area.h"
#include "app/areas/workbench_area.h"
#include "app/app_runtime_state.h"
#include "app/frame/app_menu_bar.h"
#include "app/frame/app_shell.h"
#include "app/proposal_ui_state.h"
#include "app/project_workflow.h"
#include "app/recent_projects.h"
#include "app/review_problem_sync.h"
#include "app/terminology_problem_sync.h"
#include "core/app_state.h"
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

const ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;

bool IsReviewDerivedProblem(const core::ProblemItem& problem) {
    return problem.id.rfind("review-comment:", 0) == 0 || problem.id.rfind("guideline-review:", 0) == 0;
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
    impl_->events.Subscribe<AiReviewProposalSuggestionsEvent>([this](const AiReviewProposalSuggestionsEvent& event) {
        actions::ProposalActions(*impl_).CreateAiGenerated(event.suggestions);
    });
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
    const parser::AssuranceCase* model =
        impl_->app_state.loaded_case.has_value() ? &impl_->app_state.loaded_case.value() : nullptr;
    const sacm::AssuranceCasePackage* package =
        impl_->app_state.sacm_package.has_value() ? &impl_->app_state.sacm_package.value() : nullptr;
    app::SyncTerminologyProblems(
        impl_->problems_manager, model, package, [this](const std::string& element_id, const std::string& term_value) {
            return IsTerminologySuggestionIgnored(element_id, term_value);
        });
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
    if (!impl_->review_controller->SetManualReviewOk(element_id, manual_ok, impl_->reviewer_name, core::NowUtcString())) {
        SetStatus("Could not update manual review status.");
        return false;
    }
    SyncReviewVisualStatesFromReviews();
    return true;
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
    impl_->reviewer_name = core::TrimWhitespace(content);
    ui::CopyToBuffer(impl_->reviewer_name_buf, sizeof(impl_->reviewer_name_buf), impl_->reviewer_name);
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
        actions::ProposalActions(*impl_).ApplyReviewProposal(item);
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

    areas::ProposalEditorAreaCallbacks proposal_editor_callbacks{
        [this](core::RemoveMode mode) { RemoveProposalSelected(mode); },
        [this]() { return RefreshProposalCreatorPreview(); },
        [this](const std::string& message) { SetStatus(message); },
    };

    areas::InspectorAreaCallbacks inspector_callbacks{
        [this, &proposal_editor_callbacks]() { areas::RenderProposalElementEditor(*impl_, proposal_editor_callbacks); },
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
