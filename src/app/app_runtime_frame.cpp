#include "app/app_runtime.h"

#include "app/actions/proposal_actions.h"
#include "app/actions/review_actions.h"
#include "app/app_events.h"
#include "app/app_runtime_internal.h"
#include "app/app_runtime_state.h"
#include "app/areas/ai_debug_area.h"
#include "app/areas/argument_navigator_area.h"
#include "app/areas/feedback_dock_area.h"
#include "app/areas/inspector_area.h"
#include "app/areas/modal_host.h"
#include "app/areas/perf_overlay_area.h"
#include "app/areas/project_explorer_area.h"
#include "app/areas/proposal_editor_area.h"
#include "app/areas/review_panel_area.h"
#include "app/areas/workbench_area.h"
#include "app/frame/app_layout_regions.h"
#include "app/frame/app_menu_bar.h"
#include "app/frame/app_shell.h"
#include "core/perf/frame_profiler.h"
#include "core/problems/problem_attention.h"
#include "ui/ui_state.h"

#include <cstddef>
#include <string>

namespace app {

void AppRuntime::RenderFrame(bool& done) {
    core::perf::BeginFrame();
    if (impl_->modal_coordinator->ConsumeCloseRequest()) {
        RequestExit(done);
    }

    frame::AppMenuBarCallbacks menu_callbacks;
    menu_callbacks.begin_create_project = [this]() { BeginCreateProject(); };
    menu_callbacks.begin_open_project = [this]() { BeginOpenProject(); };
    menu_callbacks.save_project = [this]() { return SaveProject(); };
    menu_callbacks.export_gsn_svg = [this]() { ExportGsnSvg(); };
    menu_callbacks.request_exit = [this](bool& done_ref) { RequestExit(done_ref); };
    menu_callbacks.begin_create_project_sacm_file = [this]() { BeginCreateProjectSacmFile(); };
    menu_callbacks.begin_create_project_evidence_register = [this]() { BeginCreateProjectEvidenceRegister(); };
    menu_callbacks.begin_create_project_j3377_cae_register = [this]() { BeginCreateProjectJ3377CaeRegister(); };
    const float menu_height = frame::RenderAppMenuBar(*impl_, done, menu_callbacks);

    {
        core::perf::ScopedTimer s("app.derived_views");
        RebuildDerivedViewsIfNeeded();
    }
    {
        core::perf::ScopedTimer s("app.proposal_preview_refresh");
        ProcessPendingProposalCreatorPreviewRefresh();
    }
    {
        core::perf::ScopedTimer s("app.ai_poll");
        PollAiReviewTask();
    }

    {
        core::perf::ScopedTimer s("app.problem_badges");
        ui::GetUiState().element_badge_summaries =
            core::BuildElementBadgeSummaries(impl_->problems_manager.GetProblems());
    }

    const frame::AppLayoutRegions regions = frame::RenderAppShell(*impl_, menu_height, kPanelFlags);

    areas::ProjectExplorerAreaCallbacks project_explorer_callbacks{
        [this]() { RefreshSacmPackageTreeCache(); },
        [this]() { BeginCreateProjectSacmFile(); },
        [this]() { BeginCreateProjectEvidenceRegister(); },
        [this]() { BeginCreateProjectJ3377CaeRegister(); },
        [this](const core::ProjectFileEntry& entry) { OpenProjectFile(entry); },
        [this](const core::ProjectFileEntry& entry) { RemoveProjectFile(entry); },
        [this](const core::ProjectFileEntry& entry) { RevealProjectFileInExplorer(entry); },
        [this](const core::ProjectFileEntry& entry, const sacm::SacmPackageTreeNode& node) {
            OpenProjectPackageNode(entry, node);
        },
        [this](const core::ProjectFileEntry& entry, const sacm::SacmPackageTreeNode& node) {
            BeginAddTerminologyPackage(entry, node);
        },
        [this](const core::ProjectFileEntry& entry, const sacm::SacmPackageTreeNode& node) {
            RemoveProjectPackage(entry, node);
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

    {
        core::perf::ScopedTimer s("app.area.project_explorer");
        areas::RenderProjectExplorerArea(*impl_, regions.project_explorer, kPanelFlags, project_explorer_callbacks);
    }
    {
        core::perf::ScopedTimer s("app.area.argument_navigator");
        areas::RenderArgumentNavigatorArea(
            *impl_, regions.argument_navigator, kPanelFlags, argument_navigator_callbacks);
    }
    {
        core::perf::ScopedTimer s("app.area.workbench");
        areas::RenderWorkbenchArea(*impl_, regions.workbench, kPanelFlags, MakeWorkbenchAreaCallbacks());
    }

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
    review_panel_callbacks.quick_fix_problem = [this](const core::ProblemItem& problem) {
        HandleProblemQuickFix(problem);
    };
    review_panel_callbacks.set_manual_review_ok = [this](const std::string& element_id, bool manual_ok) {
        return SetManualReviewOk(element_id, manual_ok);
    };

    areas::FeedbackDockAreaCallbacks feedback_dock_callbacks;
    feedback_dock_callbacks.problems.activate_problem = [this](const core::ProblemItem& problem) {
        if (problem.type.rfind("Acp", 0) == 0) {
            HandleProblemQuickFix(problem);
            return;
        }
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
    feedback_dock_callbacks.problems.open_review_problem = [this](const core::ProblemItem& problem) {
        if (problem.element_id.empty())
            return;
        ui::UiState& ui_state = ui::GetUiState();
        ui_state.selected_problem_element_id = problem.element_id;
        impl_->workbench.focus_review_tab = true;
        impl_->workbench.focus_review_item_id = ReviewItemIdFromProblemId(problem.id);
        impl_->events.Emit(SelectionChangedEvent{problem.element_id, true});
        impl_->events.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
    };
    feedback_dock_callbacks.term_usages.activate_usage = [this](std::size_t usage_index) {
        NavigateToTerminologyUsage(usage_index);
    };
    feedback_dock_callbacks.render_review_content = [this, &review_panel_callbacks]() {
        areas::RenderReviewPanelContent(*impl_, review_panel_callbacks);
    };
    feedback_dock_callbacks.render_ai_debug_content = [this]() { areas::RenderAiDebugPanelContent(*impl_); };
    {
        core::perf::ScopedTimer s("app.area.feedback_dock");
        areas::RenderFeedbackDockArea(*impl_, regions.feedback_dock, kPanelFlags, feedback_dock_callbacks);
    }

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
        [this]() { impl_->workbench.focus_review_tab = true; },
        [this]() {
            impl_->events.Emit(TreeDirtyEvent{});
            impl_->events.Emit(DocumentDirtyEvent{});
        },
        [this](const std::string& element_id, const std::string& field_token, const std::string& language,
               const std::string& original_value, const std::string& new_value) {
            if (!impl_->app_state.loaded_case.has_value())
                return;
            sacm::AssuranceCasePackage* pkg =
                impl_->app_state.sacm_package.has_value() ? &impl_->app_state.sacm_package.value() : nullptr;
            const bool committed = impl_->element_edit_controller->CommitElementTextEdit(
                impl_->app_state.loaded_case.value(), pkg, element_id, field_token, language, original_value,
                new_value);
            if (committed)
                impl_->events.Emit(TreeDirtyEvent{});
        },
        [this](const std::string& element_id) {
            impl_->workbench.history_filter_element_id = element_id;
            impl_->workbench.focus_history_tab = true;
        },
    };
    {
        core::perf::ScopedTimer s("app.area.inspector");
        areas::RenderInspectorArea(*impl_, regions.inspector, kPanelFlags, inspector_callbacks);
    }

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
    modal_callbacks.confirm_pending_project_file_open = [this](bool save_current) {
        ConfirmPendingProjectFileOpen(save_current);
    };
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
    {
        core::perf::ScopedTimer s("app.modal_host");
        areas::RenderModalHost(*impl_, done, modal_callbacks);
    }

    if (ui::GetUiState().show_perf_overlay) {
        areas::RenderPerfOverlay(ui::GetUiState().show_perf_overlay);
    }

    core::perf::EndFrame();
}

} // namespace app
