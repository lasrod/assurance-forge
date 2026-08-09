#include "app/app_runtime.h"

#include "app/actions/terminology_actions.h"
#include "app/agent_request_handler.h"
#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "app/commands/dispatch.h"
#include "app/confidence_problem_sync.h"
#include "app/native_file_dialogs.h"
#include "app/proposal_ui_state.h"
#include "app/project_workflow.h"
#include "app/recent_projects.h"
#include "app/register_problem_sync.h"
#include "app/structure_problem_sync.h"
#include "app/translation_review_sync.h"
#include "core/translation_review_store.h"
#include "core/drafts/draft_dependency_graph.h"
#include "core/drafts/draft_promotion_service.h"
#include "core/element_factory.h"
#include "core/acp/assurance_claim_point.h"
#include "core/audit/replay_verifier.h"
#include "core/audit/audit_store.h"
#include "core/audit/strategy_migration.h"
#include "core/commands/command_bus.h"
#include "core/commands/proposal_commands.h"
#include "core/commands/package_commands.h"
#include "core/problems/problem_utils.h"
#include "core/project_service.h"
#include "core/reviews/review_item.h"
#include "core/reviews/review_proposal.h"
#include "core/terminology_package_service.h"
#include "core/terminology_text_utils.h"
#include "export/gsn_svg_exporter.h"
#include "imgui.h"
#include "parser/model_utils.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"
#include "legacy_sacm/sacm_package_tree.h"
#include "legacy_sacm/sacm_serializer.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/i18n/localization.h"
#include "ui/imgui_buffer_utils.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace app {
namespace {

using ui::CopyToBuffer;

bool CanSwitchProjectSacmFile(const core::AppState& app_state, const core::ProjectFileEntry& entry) {
    if (!app_state.current_project.has_value() || !app_state.has_unsaved_changes)
        return true;
    const std::filesystem::path target_path = app_state.current_project->rootPath / entry.relativePath;
    const std::filesystem::path current_sacm_path =
        !app_state.loaded_file_path.empty() ? app_state.loaded_file_path : app_state.active_project_file_path;
    return current_sacm_path.empty() || current_sacm_path == target_path;
}

std::filesystem::path ProjectFilePath(const core::AppState& app_state, const core::ProjectFileEntry& entry) {
    if (!app_state.current_project.has_value())
        return {};
    return app_state.current_project->rootPath / entry.relativePath;
}

bool IsLoadedProjectSacmFile(const core::AppState& app_state, const core::ProjectFileEntry& entry) {
    if (entry.role != core::ProjectFileRole::SacmArgument || app_state.loaded_file_path.empty())
        return false;
    return app_state.loaded_file_path == ProjectFilePath(app_state, entry);
}

sacm::SacmPackageTreeResult BuildLoadedSacmPackageTree(const core::AppState& app_state,
                                                       const core::AssuranceProject& project,
                                                       const core::ProjectFileEntry& entry) {
    sacm::SacmPackageTreeResult result = sacm::build_sacm_package_tree_from_string(
        sacm::serialize_sacm(app_state.projected_package()), entry.relativePath.filename().generic_string());
    result.source_path = project.rootPath / entry.relativePath;
    result.root.id = result.source_path.generic_string();
    return result;
}

std::string ArgumentPackageCanvasKey(const std::filesystem::path& source_file_path,
                                     const std::string& package_id,
                                     const std::string& package_gid) {
    return source_file_path.generic_string() + "\x1f" + package_id + "\x1f" + package_gid;
}

std::filesystem::path ConfidenceItemsPath(const core::AssuranceProject& project) {
    for (const core::ProjectFileEntry& entry : project.files) {
        if (entry.role == core::ProjectFileRole::ConfidenceAssessments)
            return project.rootPath / entry.relativePath;
    }
    return {};
}

std::filesystem::path RegisterAssessmentsPath(const core::AssuranceProject& project) {
    for (const core::ProjectFileEntry& entry : project.files) {
        if (entry.role == core::ProjectFileRole::RegisterAssessments)
            return project.rootPath / entry.relativePath;
    }
    return {};
}

std::string ConfidenceSourceHash(const core::ProjectFileEntry& entry) {
    if (entry.rawHash.empty())
        return {};
    return entry.hashAlgorithm.empty() ? entry.rawHash : entry.hashAlgorithm + ":" + entry.rawHash;
}

void SetConfidenceSource(AppRuntimeState& state, const core::ProjectFileEntry& entry) {
    if (!state.confidence_controller)
        return;
    state.confidence_controller->SetActiveSource(
        entry.id.empty() ? "main" : entry.id, entry.relativePath.generic_string(), ConfidenceSourceHash(entry));
    if (state.app_state.loaded_case.has_value() &&
        state.confidence_controller->RefreshStaleFlags(state.app_state.loaded_case.value()) &&
        state.confidence_controller->LastInactivatedCount() > 0) {
        const int count = state.confidence_controller->LastInactivatedCount();
        state.events.Emit(StatusMessageEvent{
            std::to_string(count) +
            " confidence assessment(s) were marked inactive because their target elements changed."});
    }
}

bool ProjectFileOpenWouldLeaveLoadedSacm(const core::AppState& app_state, const core::ProjectFileEntry& entry) {
    if (app_state.loaded_file_path.empty())
        return true;
    return app_state.loaded_file_path != ProjectFilePath(app_state, entry);
}

bool CurrentSacmDocumentHasUnsavedChanges(const AppRuntimeState& state) {
    const bool sacm_document_loaded =
        state.app_state.has_projected_package() || state.app_state.loaded_case.has_value();
    return sacm_document_loaded && (state.document_dirty || state.app_state.has_unsaved_changes);
}

bool EnsureProjectSacmFileOpen(AppRuntimeState& state, const core::ProjectFileEntry& entry, bool require_loaded_case) {
    if (IsLoadedProjectSacmFile(state.app_state, entry) && state.app_state.has_projected_package() &&
        (!require_loaded_case || state.app_state.loaded_case.has_value())) {
        return true;
    }
    return state.app_state.open_project_file(entry);
}

void CopyTerminologyPackageToEditor(AppRuntimeState& state, const sacm::TerminologyPackage& package) {
    ui::CopyToBuffer(state.terminology.package_name_buf, sizeof(state.terminology.package_name_buf), package.name);
    ui::CopyToBuffer(state.terminology.package_description_buf,
                     sizeof(state.terminology.package_description_buf),
                     package.description);
}

std::string FirstElementIdForArgumentPackage(const sacm::AssuranceCasePackage& package,
                                             const sacm::SacmPackageTreeNode& selected_package) {
    for (const auto& argument_package : package.argumentPackages) {
        const bool id_matches = !selected_package.id.empty() && argument_package.id == selected_package.id;
        const bool gid_matches = !selected_package.gid.empty() && argument_package.gid == selected_package.gid;
        if (!id_matches && !gid_matches)
            continue;
        if (!argument_package.claims.empty())
            return argument_package.claims.front().id;
        if (!argument_package.argumentReasonings.empty())
            return argument_package.argumentReasonings.front().id;
        if (!argument_package.artifactReferences.empty())
            return argument_package.artifactReferences.front().id;
    }
    return {};
}

std::string FirstElementIdForArgumentPackage(const sacm::ArgumentPackage& argument_package) {
    if (!argument_package.claims.empty())
        return argument_package.claims.front().id;
    if (!argument_package.argumentReasonings.empty())
        return argument_package.argumentReasonings.front().id;
    if (!argument_package.artifactReferences.empty())
        return argument_package.artifactReferences.front().id;
    return {};
}

const sacm::ArgumentPackage* FirstArgumentPackage(const sacm::AssuranceCasePackage& package) {
    return package.argumentPackages.empty() ? nullptr : &package.argumentPackages.front();
}

} // namespace

void AppRuntime::BeginCreateProject() {
    std::string selected_path;
    std::string error_message;
    const dialogs::DialogResult result = dialogs::BrowseForProjectParentFolder(
        impl_->project_controller->project_parent_buf, selected_path, error_message);
    if (result == dialogs::DialogResult::Selected) {
        CopyToBuffer(impl_->project_controller->project_parent_buf,
                     sizeof(impl_->project_controller->project_parent_buf),
                     selected_path);
        if (impl_->project_controller->project_name_buf[0] == '\0') {
            CopyToBuffer(impl_->project_controller->project_name_buf,
                         sizeof(impl_->project_controller->project_name_buf),
                         "MySafetyCase");
        }
        impl_->project_controller->show_create_project_modal = true;
    } else if (result == dialogs::DialogResult::Failed) {
        SetStatus(ui::i18n::trf("Browse failed: {0}", error_message));
    }
}

void AppRuntime::BeginOpenProject() {
    std::string default_path = impl_->project_controller->open_project_path_buf;
    if (default_path.empty() && !impl_->project_controller->recent_projects.empty()) {
        default_path = impl_->project_controller->recent_projects.front().path;
    }

    std::string selected_path;
    std::string error_message;
    const dialogs::DialogResult result = dialogs::BrowseForProjectManifest(default_path, selected_path, error_message);
    if (result == dialogs::DialogResult::Selected) {
        CopyToBuffer(impl_->project_controller->open_project_path_buf,
                     sizeof(impl_->project_controller->open_project_path_buf),
                     selected_path);
        TryOpenProjectManifest(selected_path);
    } else if (result == dialogs::DialogResult::Failed) {
        SetStatus(ui::i18n::trf("Browse failed: {0}", error_message));
    }
}

void AppRuntime::TouchCurrentProjectRecent() {
    impl_->project_controller->TouchCurrentProjectRecent(impl_->app_state);
}

void AppRuntime::BeginCreateProjectSacmFile() {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus(AF_TR("Create or open a project first."));
        return;
    }
    impl_->project_controller->BeginProjectFileCreate(ProjectFileCreateKind::Sacm, "main.sacm");
}

void AppRuntime::BeginCreateProjectEvidenceRegister() {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus(AF_TR("Create or open a project first."));
        return;
    }
    impl_->project_controller->BeginProjectFileCreate(ProjectFileCreateKind::EvidenceRegister,
                                                      "evidence-register.af.json");
}

void AppRuntime::BeginCreateProjectJ3377CaeRegister() {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus(AF_TR("Create or open a project first."));
        return;
    }
    impl_->project_controller->BeginProjectFileCreate(ProjectFileCreateKind::J3377CaeRegister,
                                                      "j3377-cae-register.af.json");
}

void AppRuntime::OpenProjectFile(const core::ProjectFileEntry& entry) {
    if (entry.role == core::ProjectFileRole::SacmArgument) {
        if (IsLoadedProjectSacmFile(impl_->app_state, entry) && impl_->app_state.loaded_case.has_value()) {
            ui::UiState& ui_state = ui::GetUiState();
            impl_->app_state.active_project_file_role = entry.role;
            impl_->app_state.active_project_file_path = ProjectFilePath(impl_->app_state, entry);
            impl_->workbench.show_gsn_tab = true;
            ui_state.center_view = ui::CenterView::GsnCanvas;
            impl_->workbench.force_center_tab_selection = true;
            if (impl_->workbench.argument_package_canvas_tabs.empty())
                OpenFirstArgumentPackageCanvas();
            SetStatus(ui::i18n::trf("SACM file is already open: {0}", entry.relativePath.generic_string()));
            return;
        }
    }

    if (CurrentSacmDocumentHasUnsavedChanges(*impl_) && ProjectFileOpenWouldLeaveLoadedSacm(impl_->app_state, entry)) {
        impl_->project_controller->pending_open_project_file_entry = entry;
        impl_->project_controller->show_save_before_project_file_open_modal = true;
        return;
    }

    PerformOpenProjectFile(entry);
}

void AppRuntime::PerformOpenProjectFile(const core::ProjectFileEntry& entry) {
    // Migrate a legacy GSN-strategy encoding on disk BEFORE loading, so the
    // loaded state and the promoted trusted baseline both reflect the standard
    // single-inference form and open-verify converges. No-op for non-audited or
    // already-migrated files. (Phase 9 Stage 7, slice 3.)
    core::audit::StrategyMigrationResult strategy_migration;
    if (entry.role == core::ProjectFileRole::SacmArgument && impl_->app_state.current_project.has_value()) {
        std::string migration_error;
        if (!core::audit::MigrateStrategyEncodingIfNeeded(
                impl_->app_state.current_project.value(), entry.relativePath, strategy_migration, migration_error)) {
            SetStatus(ui::i18n::trf("Strategy encoding migration failed: {0}", migration_error));
        }
    }

    if (!impl_->app_state.open_project_file(entry))
        return;

    ui::UiState& ui_state = ui::GetUiState();
    if (entry.role == core::ProjectFileRole::SacmArgument) {
        SetConfidenceSource(*impl_, entry);
        // Fully repopulate every problem source for the freshly opened file on
        // the next frame (RefreshDirtyProblems runs after the tree rebuild).
        impl_->problems_dirty.MarkAll();
        impl_->proposal_controller->ClearActiveState();
        ClearProposalHighlightState(ui_state);
        impl_->document_dirty = false;
        impl_->tree_needs_rebuild = true;
        impl_->workbench.argument_package_canvas_tabs.clear();
        impl_->workbench.active_argument_package_canvas_key.clear();
        impl_->workbench.pending_focus_root = false;
        impl_->workbench.show_gsn_tab = true;
        ui_state.center_view = ui::CenterView::GsnCanvas;
        impl_->workbench.force_center_tab_selection = true;
        OpenFirstArgumentPackageCanvas();

        // Construct the audited command bus over the project's audit store.
        // `EnsureAuditStore` was already called in `AppState::open_project` /
        // `create_project_sacm_file`, so the manifest and event log exist on
        // disk. If construction fails, fall back to direct mutation (legacy
        // behaviour) and surface a warning.
        impl_->command_bus.reset();
        if (impl_->app_state.current_project.has_value()) {
            std::string bus_error;
            auto bus = core::commands::CommandBus::Open(
                impl_->app_state.current_project.value(), ProjectFilePath(impl_->app_state, entry), bus_error);
            if (!bus) {
                SetStatus(ui::i18n::trf("Audit bus init failed: {0}", bus_error));
            } else {
                impl_->command_bus = std::move(bus);
            }
        }

        // Audit replay verification (design §13). Best-effort: a mismatch is
        // surfaced as a warning, not an error — the loaded SACM remains the
        // user's working state.
        impl_->last_audit_verification.reset();
        if (impl_->app_state.current_project.has_value()) {
            auto verification = core::audit::VerifyProject(impl_->app_state.current_project.value());
            if (verification.ran && !verification.success) {
                std::string msg = "Audit replay verification failed — the recorded "
                                  "history no longer reproduces the on-disk SACM. "
                                  "Toggle 'Show history' on a canvas tab to reconcile.";
                for (const auto& d : verification.diagnostics)
                    msg += "\n  - " + d;
                SetStatus(msg);
            } else if (strategy_migration.migrated) {
                // Verify passed after a migration: surface the one-time note as
                // the final status so the on-disk rewrite is visible, not silent.
                SetStatus(strategy_migration.note);
            }
            impl_->last_audit_verification = std::move(verification);
        }
    } else if (entry.role == core::ProjectFileRole::EvidenceRegister) {
        impl_->workbench.show_evidence_tab = true;
        ui_state.center_view = ui::CenterView::EvidenceRegister;
        impl_->workbench.force_center_tab_selection = true;
    } else if (entry.role == core::ProjectFileRole::J3377CaeRegister) {
        impl_->workbench.show_cse_tab = true;
        ui_state.center_view = ui::CenterView::CseRegister;
        impl_->workbench.force_center_tab_selection = true;
    } else if (entry.role == core::ProjectFileRole::ReviewProposal) {
        std::string proposal_id = entry.relativePath.filename().generic_string();
        const std::string suffix = ".afpatch.json";
        if (proposal_id.size() >= suffix.size() &&
            proposal_id.compare(proposal_id.size() - suffix.size(), suffix.size(), suffix) == 0) {
            proposal_id.erase(proposal_id.size() - suffix.size());
        }
        PreviewProposalById(proposal_id);
    }
}

void AppRuntime::ConfirmPendingProjectFileOpen(bool save_current) {
    if (!impl_->project_controller->pending_open_project_file_entry.has_value()) {
        impl_->project_controller->show_save_before_project_file_open_modal = false;
        return;
    }

    if (save_current && !SaveProject())
        return;

    core::ProjectFileEntry entry = impl_->project_controller->pending_open_project_file_entry.value();
    impl_->project_controller->pending_open_project_file_entry.reset();
    impl_->project_controller->show_save_before_project_file_open_modal = false;
    PerformOpenProjectFile(entry);
}

bool AppRuntime::ReconcileAuditStore() {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus(AF_TR("Cannot reconcile audit log: no project is open."));
        return false;
    }
    if (impl_->app_state.active_project_file_role != core::ProjectFileRole::SacmArgument) {
        SetStatus(AF_TR("Cannot reconcile audit log: no SACM file is active."));
        return false;
    }

    const core::AssuranceProject& project = impl_->app_state.current_project.value();
    // Locate the currently-active project file entry so we can re-open it
    // after the audit store has been rebuilt.
    const core::ProjectFileEntry* active_entry = nullptr;
    for (const auto& f : project.files) {
        if (project.rootPath / f.relativePath == impl_->app_state.active_project_file_path) {
            active_entry = &f;
            break;
        }
    }
    if (!active_entry) {
        SetStatus(AF_TR("Cannot reconcile audit log: active SACM file is no longer listed in the project."));
        return false;
    }

    // Persist any in-memory edits first; the new initial snapshot is built
    // from the on-disk SACM bytes, so we want them to reflect live state.
    if (impl_->document_dirty) {
        if (!SaveProject()) {
            SetStatus(AF_TR("Cannot reconcile audit log: failed to save current SACM file."));
            return false;
        }
    }

    // Tear down the bus before mutating the audit store; the bus holds an
    // open handle to `transactions.af.jsonl` on Windows and rename would
    // fail otherwise.
    impl_->command_bus.reset();

    core::ProjectFileEntry entry_copy = *active_entry;
    core::audit::ReconcileAuditStoreResult result;
    std::string error;
    if (!core::audit::ReconcileAuditStore(project, entry_copy.relativePath, result, error)) {
        SetStatus(ui::i18n::trf("Audit reconciliation failed: {0}", error));
        // Best-effort: re-open the project so the user retains a working
        // session against the original (now-restored) audit artifacts.
        PerformOpenProjectFile(entry_copy);
        return false;
    }

    SetStatus(ui::i18n::trf("Audit log reconciled. Previous artifacts backed up to {0}.", result.backup_dir));

    // Re-open the SACM file: this reinstalls the command bus over the fresh
    // event store and re-runs replay verification (which should now succeed).
    PerformOpenProjectFile(entry_copy);
    return true;
}

void AppRuntime::OpenProjectPackageNode(const core::ProjectFileEntry& entry, const sacm::SacmPackageTreeNode& node) {
    ui::UiState& ui_state = ui::GetUiState();
    impl_->selected_package_node = node;
    impl_->selected_package_file_path = impl_->app_state.current_project.has_value()
                                            ? impl_->app_state.current_project->rootPath / entry.relativePath
                                            : entry.relativePath;

    if (node.type == sacm::SacmPackageNodeType::ArgumentPackage) {
        if (!CanSwitchProjectSacmFile(impl_->app_state, entry)) {
            SetStatus(AF_TR("Save the current SACM file before opening another package."));
            return;
        }
        const bool same_file_loaded = IsLoadedProjectSacmFile(impl_->app_state, entry);
        if (!EnsureProjectSacmFileOpen(*impl_, entry, true))
            return;
        if (!same_file_loaded) {
            impl_->workbench.argument_package_canvas_tabs.clear();
            impl_->workbench.active_argument_package_canvas_key.clear();
        }

        impl_->proposal_controller->ClearActiveState();
        ClearProposalHighlightState(ui_state);
        impl_->document_dirty = false;
        impl_->tree_needs_rebuild = true;
        impl_->workbench.pending_focus_root = false;

        std::string first_id;
        if (impl_->app_state.has_projected_package()) {
            first_id = FirstElementIdForArgumentPackage(impl_->app_state.projected_package(), node);
            if (!first_id.empty()) {
                ui_state.selected_element_id = first_id;
                ui_state.center_on_selection = true;
            } else {
                SetStatus(AF_TR("Opened argument package; no focusable argument element was found in the package."));
            }
        }
        OpenArgumentPackageCanvas(node.id, node.gid, node.displayName, first_id);
        return;
    }

    if (node.type == sacm::SacmPackageNodeType::TerminologyPackage) {
        if (!CanSwitchProjectSacmFile(impl_->app_state, entry)) {
            SetStatus(AF_TR("Save the current SACM file before opening another package."));
            return;
        }
        if (!EnsureProjectSacmFileOpen(*impl_, entry, false))
            return;
        if (!impl_->app_state.has_projected_package()) {
            SetStatus(AF_TR("Opened SACM file, but no editable package model was available."));
            return;
        }

        core::TerminologyPackageRef package_ref{node.id, node.gid};
        const sacm::TerminologyPackage* terminology_package =
            core::FindTerminologyPackage(impl_->app_state.projected_package(), package_ref);
        if (!terminology_package) {
            SetStatus(AF_TR("Terminology package was not found in the editable model."));
            return;
        }

        impl_->terminology.selected_package_ref = package_ref;
        impl_->terminology.selected_package_file_path = impl_->selected_package_file_path;
        impl_->terminology.selected_term_ref = core::TerminologyTermRef{};
        impl_->terminology.selected_category_ref = core::TerminologyCategoryRef{};
        CopyToBuffer(impl_->terminology.category_filter_buf, sizeof(impl_->terminology.category_filter_buf), "");
        CopyTerminologyPackageToEditor(*impl_, *terminology_package);
        impl_->workbench.show_terminology_package_tab = true;
        ui_state.center_view = ui::CenterView::TerminologyPackage;
        impl_->workbench.force_center_tab_selection = true;
        return;
    }

    impl_->workbench.show_package_details_tab = true;
    ui_state.center_view = ui::CenterView::PackageDetails;
    impl_->workbench.force_center_tab_selection = true;
}

bool AppRuntime::RemoveProjectFile(const core::ProjectFileEntry& entry) {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus(AF_TR("Open a project before removing files."));
        return false;
    }

    const core::ProjectFileRole role = entry.role;
    const std::filesystem::path relative_path = entry.relativePath;
    core::AssuranceProject& project = impl_->app_state.current_project.value();
    std::string error;
    bool removed = false;

    if (role == core::ProjectFileRole::ReviewProposal) {
        std::string proposal_id = relative_path.filename().generic_string();
        const std::string suffix = ".afpatch.json";
        if (proposal_id.size() >= suffix.size() &&
            proposal_id.compare(proposal_id.size() - suffix.size(), suffix.size(), suffix) == 0) {
            proposal_id.erase(proposal_id.size() - suffix.size());
        }
        removed = DeleteProposalPatchFile(proposal_id, error);
        if (removed)
            CloseProposalPreviewIfOpen(proposal_id);
    } else if (role == core::ProjectFileRole::ExportedReport) {
        removed = core::ProjectService::RemoveTrackedFile(project, relative_path, true, error);
    } else {
        SetStatus(AF_TR("Removing this file type is not supported here."));
        return false;
    }

    if (!removed) {
        SetStatus(ui::i18n::trf("Remove file failed: {0}", error));
        return false;
    }

    impl_->events.Emit(DocumentDirtyEvent{});
    impl_->events.Emit(ProjectFilesChangedEvent{});
    core::ProjectService::RefreshFileStatus(project);
    SetStatus(ui::i18n::trf("Removed {0}.", relative_path.generic_string()));
    return true;
}

bool AppRuntime::RevealProjectFileInExplorer(const core::ProjectFileEntry& entry) {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus(AF_TR("Open a project before revealing files."));
        return false;
    }

    const std::filesystem::path absolute_path = impl_->app_state.current_project->rootPath / entry.relativePath;
    std::string error;
    if (!app::dialogs::RevealPathInFileExplorer(absolute_path, error)) {
        SetStatus(ui::i18n::trf("Could not open File Explorer: {0}", error));
        return false;
    }
    return true;
}

void AppRuntime::OpenArgumentPackageCanvas(const std::string& package_id,
                                           const std::string& package_gid,
                                           const std::string& display_name,
                                           const std::string& focus_element_id) {
    std::filesystem::path source_file_path = impl_->app_state.loaded_file_path;
    if (source_file_path.empty())
        source_file_path = impl_->app_state.active_project_file_path;

    const std::string key = ArgumentPackageCanvasKey(source_file_path, package_id, package_gid);
    auto& tabs = impl_->workbench.argument_package_canvas_tabs;
    auto found = std::find_if(tabs.begin(), tabs.end(), [&](const auto& tab) { return tab.key == key; });
    if (found == tabs.end()) {
        WorkbenchState::ArgumentPackageCanvasTab tab;
        tab.key = key;
        tab.package_id = package_id;
        tab.package_gid = package_gid;
        tab.title = display_name.empty() ? (package_id.empty() ? "Argument Package" : package_id) : display_name;
        tab.source_file_path = source_file_path;
        tabs.push_back(std::move(tab));
    } else if (!display_name.empty()) {
        found->title = display_name;
    }

    impl_->workbench.active_argument_package_canvas_key = key;
    impl_->workbench.show_gsn_tab = true;
    impl_->workbench.pending_focus_root = false;
    impl_->workbench.force_center_tab_selection = true;
    ui::UiState& ui_state = ui::GetUiState();
    ui_state.center_view = ui::CenterView::GsnCanvas;
    if (!focus_element_id.empty()) {
        ui_state.selected_element_id = focus_element_id;
        ui_state.selected_acp_id.clear();
        ui_state.selected_relationship_id.clear();
        ui_state.selected_relationship_edge_key.clear();
        ui_state.center_on_selection = true;
    }
}

void AppRuntime::OpenFirstArgumentPackageCanvas() {
    if (!impl_->app_state.has_projected_package())
        return;
    const sacm::ArgumentPackage* argument_package = FirstArgumentPackage(impl_->app_state.projected_package());
    if (!argument_package)
        return;
    const std::string title = argument_package->name.empty() ? argument_package->id : argument_package->name;
    OpenArgumentPackageCanvas(argument_package->id,
                              argument_package->gid,
                              title.empty() ? "Argument Package" : title,
                              FirstElementIdForArgumentPackage(*argument_package));
}

void AppRuntime::BeginAddTerminologyPackage(const core::ProjectFileEntry& entry,
                                            const sacm::SacmPackageTreeNode& parent_node) {
    actions::TerminologyActions(*impl_).BeginAddPackage(entry, parent_node);
}

void AppRuntime::ConfirmAddTerminologyPackage() {
    if (actions::TerminologyActions(*impl_).ConfirmAddPackage())
        impl_->problems_dirty.terminology = true;
}

void AppRuntime::ApplyTerminologyPackageEdits() {
    if (actions::TerminologyActions(*impl_).ApplyPackageEdits())
        impl_->problems_dirty.terminology = true;
}

void AppRuntime::BeginDeleteTerminologyPackage() {
    actions::TerminologyActions(*impl_).BeginDeletePackage();
}

void AppRuntime::ConfirmDeleteTerminologyPackage() {
    if (actions::TerminologyActions(*impl_).ConfirmDeletePackage())
        impl_->problems_dirty.terminology = true;
}

void AppRuntime::RemoveProjectPackage(const core::ProjectFileEntry& entry, const sacm::SacmPackageTreeNode& node) {
    if (!CanSwitchProjectSacmFile(impl_->app_state, entry)) {
        SetStatus(AF_TR("Save the current SACM file before removing a package."));
        return;
    }
    if (!EnsureProjectSacmFileOpen(*impl_, entry, false))
        return;
    if (!impl_->app_state.has_projected_package()) {
        SetStatus(AF_TR("Could not load an editable SACM package model."));
        return;
    }

    const std::string label = node.displayName.empty() ? node.id : node.displayName;
    std::string status_message;
    std::string kind_label;
    std::unique_ptr<core::commands::ICommand> command;

    switch (node.type) {
    case sacm::SacmPackageNodeType::TerminologyPackage: {
        command = std::make_unique<core::commands::RemoveTerminologyPackageCommand>(node.id, node.gid);
        kind_label = AF_TR("terminology package");
        break;
    }
    case sacm::SacmPackageNodeType::ArgumentPackage: {
        command = std::make_unique<core::commands::RemoveArgumentPackageCommand>(node.id, node.gid);
        kind_label = AF_TR("argument package");
        break;
    }
    case sacm::SacmPackageNodeType::ArtifactPackage: {
        command = std::make_unique<core::commands::RemoveArtifactPackageCommand>(node.id, node.gid);
        kind_label = AF_TR("artifact package");
        break;
    }
    default:
        SetStatus(AF_TR("Removing this package type is not supported yet."));
        return;
    }

    const auto outcome = app::commands::DispatchAuditedCommand(*impl_, *command);
    if (!outcome.success) {
        SetStatus(ui::i18n::trf("Remove {0} failed: {1}", kind_label, outcome.error));
        return;
    }

    if (node.type == sacm::SacmPackageNodeType::TerminologyPackage) {
        if (impl_->terminology.selected_package_ref.id == node.id &&
            impl_->terminology.selected_package_ref.gid == node.gid) {
            impl_->terminology.selected_package_ref = core::TerminologyPackageRef{};
            impl_->workbench.show_terminology_package_tab = false;
        }
    } else if (node.type == sacm::SacmPackageNodeType::ArgumentPackage) {
        impl_->tree_needs_rebuild = true;
    }

    status_message = ui::i18n::trf("Removed {0} {1}.", kind_label, label);
    impl_->sacm_package_tree_cache.erase(entry.relativePath.generic_string());
    impl_->problems_dirty.terminology = true;
    impl_->problems_dirty.acp = true;
    SetStatus(status_message);
}

void AppRuntime::SelectTerminologyTerm(const core::TerminologyTermRef& term_ref) {
    actions::TerminologyActions(*impl_).SelectTerm(term_ref);
}

void AppRuntime::BeginAddTerminologyTerm() {
    actions::TerminologyActions(*impl_).BeginAddTerm();
}

void AppRuntime::BeginEditTerminologyTerm(const core::TerminologyTermRef& term_ref) {
    actions::TerminologyActions(*impl_).BeginEditTerm(term_ref);
}

void AppRuntime::ConfirmTerminologyTermEdit() {
    if (actions::TerminologyActions(*impl_).ConfirmTermEdit())
        impl_->problems_dirty.terminology = true;
}

void AppRuntime::OpenTerminologyTermFromCanvas(const core::TerminologyPackageRef& package_ref,
                                               const core::TerminologyTermRef& term_ref) {
    actions::TerminologyActions(*impl_).OpenTermFromCanvas(package_ref, term_ref);
}

void AppRuntime::EditTerminologyTermFromCanvas(const core::TerminologyPackageRef& package_ref,
                                               const core::TerminologyTermRef& term_ref) {
    actions::TerminologyActions(*impl_).EditTermFromCanvas(package_ref, term_ref);
}

void AppRuntime::AddTerminologyTermAsContextFromCanvas(const std::string& element_id,
                                                       const core::TerminologyPackageRef& package_ref,
                                                       const core::TerminologyTermRef& term_ref) {
    if (actions::TerminologyActions(*impl_).AddTermAsContextFromCanvas(element_id, package_ref, term_ref))
        impl_->problems_dirty.terminology = true;
}

void AppRuntime::AddVisibleTerminologyTermContextFromCanvas(const std::string& element_id,
                                                            const core::TerminologyPackageRef& package_ref,
                                                            const core::TerminologyTermRef& term_ref) {
    if (actions::TerminologyActions(*impl_).AddVisibleTermContextFromCanvas(element_id, package_ref, term_ref))
        impl_->problems_dirty.terminology = true;
}

void AppRuntime::FindTerminologyUsagesFromCanvas(const core::TerminologyPackageRef& package_ref,
                                                 const core::TerminologyTermRef& term_ref) {
    actions::TerminologyActions(*impl_).BeginFindUsages(package_ref, term_ref);
}

void AppRuntime::BeginFindTerminologyUsages(const core::TerminologyPackageRef& package_ref,
                                            const core::TerminologyTermRef& term_ref) {
    actions::TerminologyActions(*impl_).BeginFindUsages(package_ref, term_ref);
}

void AppRuntime::NavigateToTerminologyUsage(std::size_t usage_index) {
    actions::TerminologyActions(*impl_).NavigateToUsage(usage_index);
}

void AppRuntime::ChangeTerminologyMeaningFromCanvas(const std::string& element_id, const std::string& term_value) {
    actions::TerminologyActions(*impl_).ChangeMeaningFromCanvas(element_id, term_value);
}

void AppRuntime::BeginQuickDefineTerminologyTerm(const std::string& element_id, const std::string& term_value) {
    actions::TerminologyActions(*impl_).BeginQuickDefineTerm(element_id, term_value);
}

void AppRuntime::BeginLinkExistingTerminologyTerm(const std::string& element_id, const std::string& term_value) {
    actions::TerminologyActions(*impl_).BeginLinkExistingTerm(element_id, term_value);
}

void AppRuntime::IgnoreTerminologySuggestion(const std::string& element_id, const std::string& term_value) {
    actions::TerminologyActions(*impl_).IgnoreSuggestion(element_id, term_value);
    impl_->problems_dirty.terminology = true;
}

bool AppRuntime::IsTerminologySuggestionIgnored(const std::string& element_id, const std::string& term_value) const {
    return actions::TerminologyActions(*impl_).IsSuggestionIgnored(element_id, term_value);
}

void AppRuntime::RestoreTerminologySuggestion(const std::string& element_id, const std::string& term_value) {
    actions::TerminologyActions(*impl_).RestoreSuggestion(element_id, term_value);
    impl_->problems_dirty.terminology = true;
}

std::vector<actions::IgnoredSuggestionView> AppRuntime::ListIgnoredTerminologySuggestions() const {
    return actions::TerminologyActions(*impl_).ListIgnoredSuggestions();
}

void AppRuntime::EnsureTerminologyIgnoreStorage() {
    actions::TerminologyActions(*impl_).LoadIgnoredSuggestions();
    impl_->problems_dirty.terminology = true;
}

namespace {

std::filesystem::path TranslationReviewFilePath(const AppRuntimeState& state) {
    if (!state.app_state.current_project.has_value())
        return {};
    return state.app_state.current_project->rootPath / "analysis" / "translation-review.af.json";
}

void SaveTranslationReviewSidecar(const AppRuntimeState& state) {
    const std::filesystem::path path = TranslationReviewFilePath(state);
    if (path.empty())
        return; // No project open — translation review is not persisted.

    std::vector<std::string> ids(state.translation_review_pending_ids.begin(),
                                 state.translation_review_pending_ids.end());
    std::sort(ids.begin(), ids.end());

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        return;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return;
    out << core::translation::SerializeTranslationReview(ids);
}

void LoadTranslationReviewSidecar(AppRuntimeState& state) {
    state.translation_review_pending_ids.clear();
    const std::filesystem::path path = TranslationReviewFilePath(state);
    if (path.empty())
        return;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return;
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::vector<std::string> ids;
    std::string error;
    if (!core::translation::ParseTranslationReview(buffer.str(), ids, error))
        return;
    for (std::string& id : ids)
        state.translation_review_pending_ids.insert(std::move(id));
}

} // namespace

void AppRuntime::SyncTranslationReviewProblems() {
    const parser::AssuranceCase* model =
        impl_->app_state.loaded_case.has_value() ? &impl_->app_state.loaded_case.value() : nullptr;
    bool pending_changed = false;
    app::SyncTranslationReviewProblems(
        impl_->problems_manager, model, impl_->translation_review_pending_ids, pending_changed);
    if (pending_changed)
        SaveTranslationReviewSidecar(*impl_);
}

void AppRuntime::MarkTranslationReviewPending(const std::string& element_id) {
    if (element_id.empty() || !impl_->app_state.loaded_case.has_value())
        return;
    const parser::AssuranceCase& ac = impl_->app_state.loaded_case.value();
    const parser::SacmElement* elem = nullptr;
    for (const parser::SacmElement& e : ac.elements) {
        if (e.id == element_id) {
            elem = &e;
            break;
        }
    }
    if (!elem || !core::ElementHasSecondaryTranslation(*elem))
        return;
    if (impl_->translation_review_pending_ids.insert(element_id).second) {
        SaveTranslationReviewSidecar(*impl_);
        impl_->problems_dirty.translation = true;
    }
}

void AppRuntime::AcceptTranslationReview(const std::string& element_id) {
    if (impl_->translation_review_pending_ids.erase(element_id) > 0) {
        SaveTranslationReviewSidecar(*impl_);
        impl_->problems_dirty.translation = true;
    }
}

bool AppRuntime::IsTranslationReviewPending(const std::string& element_id) const {
    return impl_->translation_review_pending_ids.count(element_id) > 0;
}

void AppRuntime::EnsureTranslationReviewStorage() {
    LoadTranslationReviewSidecar(*impl_);
    impl_->problems_dirty.translation = true;
}

void AppRuntime::DiscardOrphanedRegisterAssessment(const core::ProblemItem& problem) {
    RegisterAssessmentRef ref;
    if (!DecodeRegisterAssessmentPayload(problem.quick_fix_payload, ref)) {
        SetStatus(AF_TR("Register problem does not identify an assessment."));
        return;
    }
    if (impl_->register_controller->HasStorageError()) {
        SetStatus(ui::i18n::trf("Register assessments could not be loaded, so nothing can be discarded: {0}",
                                impl_->register_controller->StorageError()));
        return;
    }

    const bool discarded = ref.kind == RegisterAssessmentKind::Cse
                               ? impl_->register_controller->DiscardCseAssessment(ref.key)
                               : impl_->register_controller->DiscardEvidenceAssessment(ref.key);
    if (!discarded) {
        SetStatus(AF_TR("That register assessment was already discarded."));
        impl_->problems_dirty.registers = true;
        return;
    }

    // The file is only rewritten on save, so say so: this is the one undo the
    // register store has.
    SetStatus(ui::i18n::trf("Discarded the assessment of {0}. Close the project without saving to keep it after all.",
                            ref.key));
}

// Applies the repair a GSN v3 well-formedness finding offers. Returns false when
// the problem is not one of ours, so the caller falls through to the next
// handler.
//
// The repair is chosen per rule rather than "delete whatever is wrong":
// withdrawing the relationship is right for a connection GSN does not permit,
// and wrong for a Strategy that is simply in the wrong slot of an inference the
// author meant to make.
bool AppRuntime::ApplyGsnWellFormednessQuickFix(const core::ProblemItem& problem) {
    GsnRepairPayload payload;
    if (!DecodeGsnRepairPayload(problem.quick_fix_payload, payload))
        return false;

    if (problem.type == "UnresolvedEndpoint") {
        DropRelationshipReference(payload.relationship_id, payload.reference);
        return true;
    }
    if (problem.type == "ChallengeTargetUnresolved" || problem.type == "SupportedElementIsALeaf" ||
        problem.type == "ContextualizedElementIsALeaf" || problem.type == "EvidenceSourceIsNotASolution") {
        RemoveRelationship(payload.relationship_id);
        return true;
    }
    if (problem.type == "StrategyUsedAsAssertion") {
        MoveStrategyToReasoning(payload.relationship_id, problem.element_id);
        return true;
    }
    if (problem.type == "DuplicateNotationIdentifier") {
        RenumberGsnIdentifier(problem.element_id);
        return true;
    }
    if (problem.type == "UndevelopedElementHasSupport") {
        SetElementUndeveloped(problem.element_id, false);
        return true;
    }
    return false;
}

void AppRuntime::HandleProblemQuickFix(const core::ProblemItem& problem) {
    if (problem.type.rfind("Acp", 0) == 0) {
        if (problem.quick_fix_payload.empty()) {
            SetStatus(AF_TR("ACP problem does not identify an ACP."));
            return;
        }
        ui::UiState& ui_state = ui::GetUiState();
        ui_state.selected_acp_id = problem.quick_fix_payload;
        ui_state.selected_element_id.clear();
        ui_state.selected_relationship_id.clear();
        ui_state.selected_relationship_edge_key.clear();
        ui_state.center_view = ui::CenterView::GsnCanvas;
        impl_->workbench.force_center_tab_selection = true;
        SetStatus(ui::i18n::trf("Opened {0}", problem.quick_fix_payload));
        return;
    }
    if (problem.type == "TranslationReviewNeeded") {
        if (!problem.element_id.empty()) {
            impl_->events.Emit(SelectionChangedEvent{problem.element_id, true});
            impl_->events.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
        }
        return;
    }
    if (problem.type == kRegisterAssessmentOrphanedProblemType) {
        DiscardOrphanedRegisterAssessment(problem);
        return;
    }
    if (ApplyGsnWellFormednessQuickFix(problem))
        return;
    actions::TerminologyActions(*impl_).HandleProblemQuickFix(problem);
}

void AppRuntime::ConfirmQuickDefineTerminologyTerm(bool add_as_context) {
    if (actions::TerminologyActions(*impl_).ConfirmQuickDefineTerm(add_as_context))
        impl_->problems_dirty.terminology = true;
}

void AppRuntime::BeginDeleteTerminologyTerm(const core::TerminologyTermRef& term_ref) {
    actions::TerminologyActions(*impl_).BeginDeleteTerm(term_ref);
}

void AppRuntime::ConfirmDeleteTerminologyTerm() {
    if (actions::TerminologyActions(*impl_).ConfirmDeleteTerm())
        impl_->problems_dirty.terminology = true;
}

void AppRuntime::SelectTerminologyCategory(const core::TerminologyCategoryRef& category_ref) {
    actions::TerminologyActions(*impl_).SelectCategory(category_ref);
}

void AppRuntime::SetTerminologyCategoryFilter(const std::string& category_filter) {
    actions::TerminologyActions(*impl_).SetCategoryFilter(category_filter);
}

void AppRuntime::BeginAddTerminologyCategory() {
    actions::TerminologyActions(*impl_).BeginAddCategory();
}

void AppRuntime::BeginEditTerminologyCategory(const core::TerminologyCategoryRef& category_ref) {
    actions::TerminologyActions(*impl_).BeginEditCategory(category_ref);
}

void AppRuntime::ConfirmTerminologyCategoryEdit() {
    actions::TerminologyActions(*impl_).ConfirmCategoryEdit();
}

void AppRuntime::BeginDeleteTerminologyCategory(const core::TerminologyCategoryRef& category_ref) {
    actions::TerminologyActions(*impl_).BeginDeleteCategory(category_ref);
}

void AppRuntime::ConfirmDeleteTerminologyCategory() {
    actions::TerminologyActions(*impl_).ConfirmDeleteCategory();
}

void AppRuntime::SeedRecommendedTerminologyCategories() {
    actions::TerminologyActions(*impl_).SeedRecommendedCategories();
}

void AppRuntime::RefreshSacmPackageTreeCache() {
    if (!impl_->app_state.current_project.has_value()) {
        impl_->sacm_package_tree_cache.clear();
        return;
    }

    const auto& project = impl_->app_state.current_project.value();
    std::set<std::string> live_paths;
    for (const auto& entry : project.files) {
        if (entry.role != core::ProjectFileRole::SacmArgument)
            continue;
        const std::string relative = entry.relativePath.generic_string();
        live_paths.insert(relative);
        if (impl_->sacm_package_tree_cache.find(relative) != impl_->sacm_package_tree_cache.end())
            continue;
        if (IsLoadedProjectSacmFile(impl_->app_state, entry) && impl_->app_state.has_projected_package()) {
            impl_->sacm_package_tree_cache[relative] = BuildLoadedSacmPackageTree(impl_->app_state, project, entry);
            continue;
        }
        impl_->sacm_package_tree_cache[relative] = sacm::build_sacm_package_tree(project.rootPath / entry.relativePath);
    }

    for (auto it = impl_->sacm_package_tree_cache.begin(); it != impl_->sacm_package_tree_cache.end();) {
        if (live_paths.count(it->first) == 0) {
            it = impl_->sacm_package_tree_cache.erase(it);
        } else {
            ++it;
        }
    }
}

bool AppRuntime::OpenFirstProjectSacmFile() {
    // Load this project's persisted terminology-ignore decisions before any SACM
    // file is opened and terminology problems are first synced. Runs for both the
    // open-project and create-project flows (both reach OpenFirstProjectSacmFile).
    EnsureTerminologyIgnoreStorage();
    // Restore the persisted "translation review needed" flags for the same reason:
    // before SACM files are opened and problems are first synced.
    EnsureTranslationReviewStorage();

    if (!impl_->app_state.current_project.has_value())
        return false;

    for (const auto& entry : impl_->app_state.current_project->files) {
        if (entry.role != core::ProjectFileRole::SacmArgument)
            continue;
        if (entry.state == core::ProjectFileState::Missing)
            continue;
        // Delegate to the same path used by explicit Open File so the audit
        // command bus gets installed and history-timeline recording is
        // active for any subsequent mutations.
        PerformOpenProjectFile(entry);
        if (impl_->app_state.loaded_case.has_value())
            return true;
    }

    SetStatus(AF_TR("Project opened, but no SACM file could be loaded."));
    return false;
}

bool AppRuntime::EnsureReviewItemStorage() {
    if (!impl_->app_state.current_project.has_value()) {
        impl_->review_controller->ClearStorage();
        impl_->problems_dirty.review = true;
        return false;
    }

    core::AssuranceProject& project = impl_->app_state.current_project.value();
    std::filesystem::path review_path = ReviewItemsPath(project);
    if (review_path.empty()) {
        review_path = project.rootPath / "reviews" / "review-items.af.json";
    }

    std::string error;
    if (impl_->review_controller->ConfigureStorage(review_path, error)) {
        impl_->problems_dirty.review = true;
        return true;
    }

    impl_->problems_dirty.review = true;
    SetStatus(ui::i18n::trf("Review items could not be loaded: {0}", error));
    return false;
}

bool AppRuntime::EnsureConfidenceStorage() {
    if (!impl_->app_state.current_project.has_value()) {
        impl_->confidence_controller->ClearStorage();
        impl_->problems_dirty.confidence = true;
        return false;
    }

    core::AssuranceProject& project = impl_->app_state.current_project.value();
    std::filesystem::path confidence_path = ConfidenceItemsPath(project);
    if (confidence_path.empty())
        confidence_path = project.rootPath / "analysis" / "confidence.af.json";

    std::string error;
    if (impl_->confidence_controller->ConfigureStorage(confidence_path, project.id, error)) {
        impl_->problems_dirty.confidence = true;
        return true;
    }

    impl_->problems_dirty.confidence = true;
    SetStatus(ui::i18n::trf("Confidence assessments could not be loaded: {0}", error));
    return false;
}

bool AppRuntime::EnsureRegisterStorage() {
    impl_->problems_dirty.registers = true;
    if (!impl_->app_state.current_project.has_value()) {
        impl_->register_controller->ClearStorage();
        return false;
    }

    const core::AssuranceProject& project = impl_->app_state.current_project.value();
    std::filesystem::path register_path = RegisterAssessmentsPath(project);
    if (register_path.empty())
        register_path = project.rootPath / "registers" / "register-assessments.af.json";

    std::string error;
    if (impl_->register_controller->ConfigureStorage(register_path, error))
        return true;

    // The register tab tells the user the same thing for as long as the failure
    // stands; this status line is for the moment it happens.
    SetStatus(ui::i18n::trf("Register assessments could not be loaded: {0}", error));
    return false;
}

void AppRuntime::EnsureProjectSideStorage() {
    EnsureReviewItemStorage();
    EnsureConfidenceStorage();
    EnsureRegisterStorage();
    UpdateAgentBridgeForProject();
}

void AppRuntime::UpdateAgentBridgeForProject() {
    if (impl_->agent_bridge == nullptr) {
        return;
    }
    if (!impl_->app_state.current_project.has_value()) {
        // Includes a bare SACM file opened outside a project: there is no root
        // to key an endpoint record on, and no project-owned draft workspace to
        // work against, so there is nothing useful to serve.
        impl_->agent_bridge->Stop();
        return;
    }

    std::string error;
    if (!impl_->agent_bridge->Start(impl_->app_state.current_project->rootPath, kAppVersion, error)) {
        // Not fatal. The application is perfectly usable without an AI client
        // attached, and failing an project open over it would be a poor trade.
        SetStatus(ui::i18n::trf("AI clients cannot connect to this project: {0}", error));
    }
}

bool AppRuntime::PollAgentBridge() {
    if (impl_->agent_bridge == nullptr || !impl_->agent_bridge->listening()) {
        return false;
    }

    const std::string project_path = impl_->app_state.current_project.has_value()
                                         ? impl_->app_state.current_project->rootPath.generic_string()
                                         : std::string();

    const int handled = impl_->agent_bridge->PollPendingRequests(
        [this, &project_path](const bridge::Request& request, const controllers::AgentConnection& connection) {
            const AgentRequestContext context{impl_->app_state,
                                              project_path,
                                              connection.client_label,
                                              [this](const std::string& relative_path, std::string& error) {
                                                  return OpenAgentRequestedCaseFile(relative_path, error);
                                              },
                                              connection.id,
                                              connection.session_id,
                                              &impl_->draft_workspace,
                                              [this] {
                                                  SyncDraftWorkspace();
                                                  if (!impl_->app_state.loaded_case.has_value())
                                                      return AgentArgumentView{};
                                                  const core::drafts::DraftWorkspace* workspace =
                                                      impl_->draft_workspace.workspace();
                                                  const parser::AssuranceCase& view = CurrentArgumentView();
                                                  // CurrentArgumentView deliberately falls back to accepted
                                                  // content if a recovered draft cannot materialize. Label
                                                  // the bytes actually returned, not merely the workspace's
                                                  // desired state, so an MCP client never mistakes that
                                                  // fallback for the integrated working model.
                                                  const bool is_working =
                                                      workspace != nullptr && workspace->has_active_groups() &&
                                                      workspace->state == core::drafts::DraftWorkspaceState::Active &&
                                                      &view != &impl_->app_state.loaded_case.value();
                                                  return AgentArgumentView{&view, workspace, is_working};
                                              }};
            return HandleAgentRequest(request, context);
        });

    return handled > 0;
}

bool AppRuntime::AcceptAgentChangeSet(const std::string& change_set_id, std::string& error) {
    error.clear();
    const core::changesets::ChangeSet* change_set = impl_->agent_change_sets.Find(change_set_id);
    if (change_set == nullptr || !change_set->open()) {
        error = "That change set is no longer open.";
        return false;
    }
    if (!impl_->app_state.loaded_case.has_value()) {
        error = "No assurance case is loaded.";
        return false;
    }

    // Re-checked at the moment of acceptance, not at the moment it was staged.
    // The user may have edited the argument while reading the proposal, or moved
    // to a different one of the project's arguments, and a patch that no longer
    // applies must be refused rather than forced. The Review panel runs the same
    // check every frame, so this is the second time the user sees the reason
    // rather than the first.
    const core::changesets::ChangeSetAcceptability acceptability = core::changesets::EvaluateChangeSetAcceptability(
        *change_set, impl_->app_state.loaded_file_path, impl_->app_state.loaded_case.value());
    if (!acceptability.can_accept) {
        error = acceptability.reason;
        return false;
    }

    // The same audited command an accepted proposal has always gone through, so
    // this edit is recorded, undoable and attributed exactly like one made with
    // the mouse. No new command type exists for agent-authored changes, which is
    // what guarantees they cannot do anything the application could not.
    core::commands::ApplyProposalCommand command(change_set->proposal);
    // Attributed to the client that proposed it. Without the author the bus
    // records `system`, and a reader of the audit log a year later cannot tell
    // an agent-authored change from one somebody typed -- which is the single
    // most useful thing the log could have told them about it.
    const app::commands::DispatchOutcome outcome =
        app::commands::DispatchAuditedCommand(*impl_, command, change_set->proposal.author_name);
    if (!outcome.success) {
        error = outcome.error;
        return false;
    }

    impl_->agent_change_sets.MarkApplied(change_set_id);
    impl_->app_state.mark_dirty();
    impl_->tree_needs_rebuild = true;
    return true;
}

bool AppRuntime::RejectAgentChangeSet(const std::string& change_set_id, std::string& error) {
    if (!impl_->agent_change_sets.Discard(change_set_id, error)) {
        return false;
    }
    impl_->tree_needs_rebuild = true;
    return true;
}

bool AppRuntime::PromoteWorkingDraft(std::string& error) {
    error.clear();
    const core::drafts::DraftWorkspace* workspace = impl_->draft_workspace.workspace();
    if (workspace == nullptr || !workspace->has_active_groups()) {
        error = "There is no working draft to accept.";
        return false;
    }

    // Delegated rather than duplicated, so accept-all cannot skip the checks
    // accept-selected performs -- including the verification that the accepted
    // argument actually ended up holding the change.
    std::vector<std::string> every_active;
    for (const core::drafts::DraftChangeGroup* group : workspace->ActiveGroups())
        every_active.push_back(group->id);
    return PromoteDraftGroups(every_active, error);
}

namespace {

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

// Context, assumption and justification hang off the side of a node; everything
// else develops it. Same rule the interactive add already follows, restated here
// because the draft speaks in operations rather than in tree edits.
bool AttachesAsContext(core::NewElementKind kind) {
    return kind == core::NewElementKind::Context || kind == core::NewElementKind::Assumption ||
           kind == core::NewElementKind::Justification;
}

void ReplaceCreateRef(std::optional<core::reviews::ElementRef>& element,
                      const std::string& before,
                      const std::string& after) {
    if (element.has_value() && element->create_ref == before)
        element->create_ref = after;
}

std::vector<core::reviews::PatchOperation>
MakeHumanCreateRefsUnique(const core::drafts::DraftChangeGroup& group,
                          const std::vector<core::reviews::PatchOperation>& operations) {
    std::set<std::string> used;
    for (const core::reviews::PatchOperation& operation : group.operations) {
        if (operation.create_ref.has_value())
            used.insert(operation.create_ref.value());
    }

    std::vector<core::reviews::PatchOperation> normalized = operations;
    std::map<std::string, std::string> replacements;
    for (const core::reviews::PatchOperation& operation : operations) {
        if (!operation.create_ref.has_value() || used.count(operation.create_ref.value()) == 0)
            continue;
        const std::string& before = operation.create_ref.value();
        std::string after;
        for (std::uint64_t suffix = group.operations.size() + 1;; ++suffix) {
            after = before + "_" + std::to_string(suffix);
            if (used.insert(after).second)
                break;
        }
        replacements.emplace(before, std::move(after));
    }

    for (core::reviews::PatchOperation& operation : normalized) {
        for (const auto& [before, after] : replacements) {
            if (operation.create_ref == before)
                operation.create_ref = after;
            ReplaceCreateRef(operation.element, before, after);
            ReplaceCreateRef(operation.source, before, after);
            ReplaceCreateRef(operation.target, before, after);
        }
    }
    return normalized;
}

} // namespace

bool AppRuntime::DraftEditingActive() const {
    const core::drafts::DraftWorkspace* workspace = impl_->draft_workspace.workspace();
    return workspace != nullptr && workspace->has_active_groups() &&
           workspace->state == core::drafts::DraftWorkspaceState::Active;
}

bool AppRuntime::StageHumanDraftOperations(const std::string& title,
                                           const std::vector<core::reviews::PatchOperation>& operations,
                                           std::string& error) {
    error.clear();
    if (!impl_->app_state.loaded_case.has_value()) {
        error = "No assurance case is loaded.";
        return false;
    }

    const std::string author = impl_->reviewer_name.empty() ? std::string("You") : impl_->reviewer_name;

    // One group for the session's hand edits rather than one per click: a
    // reviewer accepting later wants "my corrections", not forty groups of one
    // operation each.
    std::string group_id;
    if (const core::drafts::DraftWorkspace* workspace = impl_->draft_workspace.workspace()) {
        for (const core::drafts::DraftChangeGroup& group : workspace->groups) {
            if (group.source == core::drafts::DraftSource::Human &&
                group.state == core::drafts::DraftGroupState::Building && group.source_label == author) {
                group_id = group.id;
                break;
            }
        }
    }
    if (group_id.empty()) {
        core::drafts::DraftGroupRequest request;
        request.title = title;
        request.source = core::drafts::DraftSource::Human;
        request.source_label = author;
        group_id = impl_->draft_workspace.BeginGroup(request, impl_->app_state.loaded_case.value(), error);
        if (group_id.empty())
            return false;
    }

    std::vector<core::reviews::PatchOperation> normalized = operations;
    if (const core::drafts::DraftWorkspace* workspace = impl_->draft_workspace.workspace()) {
        if (const core::drafts::DraftChangeGroup* group = workspace->FindGroup(group_id))
            normalized = MakeHumanCreateRefsUnique(*group, operations);
    }

    if (!impl_->draft_workspace.StageOperations(group_id, normalized, impl_->app_state.loaded_case.value(), error))
        return false;
    impl_->tree_needs_rebuild = true;
    return true;
}

bool AppRuntime::AddChildToSelectedAsDraft(core::NewElementKind kind) {
    if (!DraftEditingActive())
        return false;

    const std::string& parent_id = ui::GetUiState().selected_element_id;
    if (parent_id.empty()) {
        SetStatus(AF_TR("Select the element to add under first."));
        return true;
    }

    core::reviews::PatchOperation create;
    create.type = CreateOperationFor(kind);
    create.create_ref = "$new";

    core::reviews::PatchOperation attach;
    attach.type = AttachesAsContext(kind) ? core::reviews::PatchOperationType::AddInContextOf
                                          : core::reviews::PatchOperationType::AddSupportedBy;
    core::reviews::ElementRef source;
    source.create_ref = "$new";
    core::reviews::ElementRef target;
    // The id the user clicked, which is an id in the *working* model. That is the
    // whole point: it may be an element another draft group created, which the
    // accepted model has never heard of.
    target.existing_id = parent_id;
    attach.source = source;
    attach.target = target;

    std::string error;
    if (!StageHumanDraftOperations(AF_TR("My edits"), {create, attach}, error))
        SetStatus(ui::i18n::trf("Could not add to the draft: {0}", error));
    return true;
}

bool AppRuntime::AddTopGoalAsDraft() {
    if (!DraftEditingActive())
        return false;

    core::reviews::PatchOperation create;
    create.type = core::reviews::PatchOperationType::CreateClaim;
    create.create_ref = "$new";

    std::string error;
    if (!StageHumanDraftOperations(AF_TR("My edits"), {create}, error))
        SetStatus(ui::i18n::trf("Could not add to the draft: {0}", error));
    return true;
}

bool AppRuntime::RemoveSelectedAsDraft(core::RemoveMode mode) {
    if (!DraftEditingActive())
        return false;

    const std::string& element_id = ui::GetUiState().selected_element_id;
    if (element_id.empty()) {
        SetStatus(AF_TR("Select the element to remove first."));
        return true;
    }

    core::reviews::PatchOperation remove;
    remove.type = core::reviews::PatchOperationType::RemoveElement;
    core::reviews::ElementRef element;
    element.existing_id = element_id;
    remove.element = element;
    remove.field = mode == core::RemoveMode::NodeAndDescendants
                       ? core::reviews::kReviewProposalRemoveModeNodeAndDescendants
                       : core::reviews::kReviewProposalRemoveModeNodeOnly;

    std::string error;
    if (!StageHumanDraftOperations(AF_TR("My edits"), {remove}, error))
        SetStatus(ui::i18n::trf("Could not remove in the draft: {0}", error));
    return true;
}

bool AppRuntime::CommitTextEditAsDraft(const std::string& element_id,
                                       const std::string& field_token,
                                       const std::string& language,
                                       const std::string& new_value) {
    if (!DraftEditingActive())
        return false;
    if (element_id.empty())
        return false;

    if (field_token != "name" && field_token != "content" && field_token != "description") {
        SetStatus(ui::i18n::trf("\"{0}\" cannot be edited while a working draft is active.", field_token));
        return true;
    }

    core::reviews::PatchOperation update;
    update.type = core::reviews::PatchOperationType::UpdateElementText;
    core::reviews::ElementRef element;
    element.existing_id = element_id;
    update.element = element;
    update.field = field_token;
    // A secondary-language edit revises that language and says nothing about the
    // primary, so it carries no `new_value`: writing one would blank the English
    // of a claim the user was translating.
    if (!language.empty() && language != core::reviews::kPatchPrimaryLanguage) {
        update.translations[language] = new_value;
    } else {
        update.new_value = new_value;
    }

    std::string error;
    if (!StageHumanDraftOperations(AF_TR("My edits"), {update}, error))
        SetStatus(ui::i18n::trf("Could not record the edit in the draft: {0}", error));
    return true;
}

parser::AssuranceCase* AppRuntime::InspectorModel() {
    if (!impl_->app_state.loaded_case.has_value())
        return nullptr;
    if (!DraftEditingActive())
        return &impl_->app_state.loaded_case.value();

    // Refreshed on the same key the materialization is cached under, so this
    // copy is made when something actually changed rather than every frame.
    const std::uint64_t draft_revision = impl_->draft_workspace.revision();
    if (!impl_->inspector_model_valid || impl_->inspector_model_draft_revision != draft_revision ||
        impl_->inspector_model_case_revision != impl_->app_state.case_revision) {
        impl_->inspector_model = CurrentArgumentView();
        impl_->inspector_model_draft_revision = draft_revision;
        impl_->inspector_model_case_revision = impl_->app_state.case_revision;
        impl_->inspector_model_valid = true;
    }
    return &impl_->inspector_model;
}

bool AppRuntime::PromoteDraftGroups(const std::vector<std::string>& group_ids, std::string& error) {
    error.clear();
    const core::drafts::DraftWorkspace* workspace = impl_->draft_workspace.workspace();
    if (workspace == nullptr || group_ids.empty()) {
        error = "There is nothing to accept.";
        return false;
    }
    if (!impl_->app_state.loaded_case.has_value()) {
        error = "No assurance case is loaded.";
        return false;
    }
    if (workspace->state == core::drafts::DraftWorkspaceState::NeedsRebase) {
        error = "The argument changed since this draft was written. Inspect or discard it first.";
        return false;
    }
    if (workspace->state == core::drafts::DraftWorkspaceState::Promoting) {
        error = "This promotion is awaiting durable SACM completion. Resolve the autosave problem first.";
        return false;
    }

    const std::string author = core::drafts::DraftPromotionAuthor(*workspace, group_ids);

    // Taken before `BeginPromotion` marks the workspace `Promoting`, so what undo
    // restores is the workspace a user would recognize rather than one frozen
    // mid-commit. It is written out only once the audit sequence to key it on
    // exists, which is after dispatch.
    const core::drafts::DraftWorkspace pre_promotion = *workspace;

    // Read while the groups are still here: promotion consumes them, and by the
    // time the accepted model is current enough to flag against, there is
    // nothing left to ask who wrote the Japanese.
    const std::vector<std::string> machine_translated =
        core::drafts::MachineTranslatedElementIds(*workspace, group_ids);

    // Everything that can refuse, refuses here -- before the accepted model is
    // touched at all.
    const core::drafts::DraftPromotionPlan plan =
        core::drafts::PlanDraftPromotion(*workspace,
                                         impl_->app_state.loaded_case.value(),
                                         group_ids,
                                         author,
                                         impl_->draft_workspace.authoritative_identities());
    if (!plan.ok) {
        error = plan.error;
        return false;
    }

    const bool library_primary_promotion =
        impl_->command_bus != nullptr && impl_->app_state.library_document != nullptr;
    std::string authoritative_before_xml;
    if (library_primary_promotion) {
        const sacm_adapter::SaveOutcome before = sacm_adapter::save_document(*impl_->app_state.library_document);
        if (!before.ok) {
            error = "Could not snapshot the authoritative SACM document before promotion: " +
                    sacm_adapter::summarize_load_diagnostics(before.diagnostics);
            return false;
        }
        authoritative_before_xml = before.xml;
    }
    parser::AssuranceCase authoritative_preflight;
    if (library_primary_promotion &&
        !core::commands::PreflightProposalAgainstLibrary(*impl_->app_state.library_document,
                                                         plan.compiled.proposal,
                                                         plan.compiled.identities,
                                                         authoritative_preflight,
                                                         error)) {
        return false;
    }

    if (!impl_->draft_workspace.BeginPromotion(plan.closure, plan.promoted_model, error))
        return false;

    // The draft is consumed by the act of accepting it, so whatever the log does
    // not record here is not recoverable from anywhere else. `author` names the
    // approver and the contributing sources; this is the rest of the trace.
    core::audit::DraftPromotionRecord attribution;
    attribution.group_ids = plan.closure;
    attribution.source_labels = plan.compiled.source_labels;
    for (const std::string& group_id : plan.closure) {
        const core::drafts::DraftChangeGroup* group = workspace->FindGroup(group_id);
        if (group == nullptr)
            continue;
        for (const std::string& guideline_id : group->guideline_ids) {
            if (std::find(attribution.guideline_ids.begin(), attribution.guideline_ids.end(), guideline_id) ==
                attribution.guideline_ids.end()) {
                attribution.guideline_ids.push_back(guideline_id);
            }
        }
        for (const std::string& review_item_id : group->review_item_ids) {
            if (std::find(attribution.review_item_ids.begin(), attribution.review_item_ids.end(), review_item_id) ==
                attribution.review_item_ids.end()) {
                attribution.review_item_ids.push_back(review_item_id);
            }
        }
        if (!group->rationale.empty()) {
            const std::string title = group->title.empty() ? group->id : group->title;
            attribution.rationales.push_back(title + ": " + group->rationale);
        }
    }

    core::commands::ApplyProposalCommand command(plan.compiled.proposal, plan.compiled.identities);
    const app::commands::DispatchOutcome outcome =
        app::commands::DispatchAuditedCommand(*impl_, command, author, attribution);
    if (!outcome.success) {
        const std::string dispatch_error = outcome.error;
        std::string rollback_error;
        if (library_primary_promotion &&
            !sacm_adapter::reload_document(*impl_->app_state.library_document, authoritative_before_xml)) {
            rollback_error = " The in-memory SACM document could not be restored; reopen the project before editing.";
        }
        impl_->rederive_views_from_library = library_primary_promotion;
        impl_->tree_needs_rebuild = true;
        std::string cancel_error;
        impl_->draft_workspace.CancelPromotion(cancel_error);
        error = dispatch_error + rollback_error;
        if (!cancel_error.empty())
            error += " Draft recovery could not clear its promotion marker: " + cancel_error;
        return false;
    }
    if (!outcome.sacm_written) {
        error = "The promotion was recorded in the audit log, but the accepted SACM file was not written. "
                "The draft is retained in a pending state until recovery confirms the file.";
        return false;
    }

    // The authoritative library must now be exactly what the isolated bridge
    // rehearsal produced. Checked rather than assumed, because a command that
    // reports success is not the same thing as an argument that contains the
    // change: the bridge reloads the document from a package projection, and an
    // element that reaches no package can be dropped on the way.
    //
    // This is not hypothetical. Accepting the shipped example reported success,
    // removed the draft, and wrote a file with none of the accepted changes in
    // it -- the created claim gone and the reworded goal unchanged. The draft was
    // the only copy of that work.
    //
    // Do not compare the live `loaded_case` on this path: library-primary edits
    // intentionally rederive that UI projection at the next frame boundary, so
    // it still describes the pre-promotion case here. That stale comparison was
    // why a successful Accept All was reported as a mismatch. The draft stays
    // until the authoritative result is verified; deleting the only remaining
    // copy of unaccepted work is not recoverable.
    const parser::AssuranceCase produced_model = library_primary_promotion
                                                     ? sacm_adapter::project_case(*impl_->app_state.library_document)
                                                     : impl_->app_state.loaded_case.value();
    const parser::AssuranceCase& expected_model =
        library_primary_promotion ? authoritative_preflight : plan.promoted_model;
    const std::string produced = core::reviews::ComputeModelSemanticHash(produced_model);
    const std::string expected = core::reviews::ComputeModelSemanticHash(expected_model);
    if (produced != expected) {
        error = "The accepted argument does not match what was about to be accepted, so the draft has been kept. "
                "Undo this change and report it: the promotion path dropped part of the result.";
        impl_->app_state.mark_dirty();
        impl_->tree_needs_rebuild = true;
        return false;
    }

    // Undo restores the accepted model from the audit log, and nothing in that
    // log describes a draft. Without this snapshot, undoing the promotion takes
    // the change out of the accepted argument while `RemovePromotedGroups` has
    // already taken it out of the draft -- and when the promotion consumed the
    // last group, the workspace was deleted with it. Recorded before the removal
    // so it is durable before the work it recovers is gone.
    //
    // A failure here does not fail the promotion: the accepted file is already
    // written and correct. It costs the ability to undo *into* the draft, and the
    // user is told that rather than left to discover it.
    std::string snapshot_error;
    if (outcome.transaction_sequence != 0 &&
        !impl_->draft_workspace.SavePromotionSnapshot(outcome.transaction_sequence, pre_promotion, snapshot_error)) {
        impl_->app_state.status_message =
            ui::i18n::trf("Accepted, but undoing this will not bring the draft back: {0}", snapshot_error);
    }

    // Promoted groups leave the active workspace; the rest stay visible against
    // the new baseline. The base hash moves with it, or the next open would call
    // the draft stale against an argument the user just accepted into.
    if (!impl_->draft_workspace.RemovePromotedGroups(plan.closure, plan.promoted_model, error)) {
        impl_->app_state.status_message = ui::i18n::trf("Accepted, but the draft could not be updated: {0}", error);
    }
    // Accepting the argument is not the same as accepting a translation of it.
    // Applied at the next rebuild, when the accepted model contains what was
    // just promoted.
    impl_->translation_review_marks_pending_rebuild.insert(
        impl_->translation_review_marks_pending_rebuild.end(), machine_translated.begin(), machine_translated.end());
    impl_->app_state.mark_dirty();
    impl_->tree_needs_rebuild = true;
    return true;
}

bool AppRuntime::RejectDraftGroups(const std::vector<std::string>& group_ids,
                                   DraftRejectionScope scope,
                                   std::string& error) {
    error.clear();
    const core::drafts::DraftWorkspace* workspace = impl_->draft_workspace.workspace();
    if (workspace == nullptr || group_ids.empty()) {
        error = "There is nothing to reject.";
        return false;
    }

    // A group whose creations another group edits cannot survive alone once the
    // creation is rejected: it would refer to an element that will never exist.
    // What happens to it is the user's decision, taken before this is called.
    const std::vector<std::string> dependents = core::drafts::DependentsOf(*workspace, group_ids);

    // One gesture, one undo entry. Rejecting a change and deciding what happens
    // to the changes built on it is a single decision the user took, and undoing
    // half of it would leave the draft in the incoherent state the decision
    // existed to prevent.
    const std::string label = [&]() {
        const core::drafts::DraftChangeGroup* first = workspace->FindGroup(group_ids.front());
        if (first == nullptr || first->title.empty())
            return std::string("Rejected a change");
        return "Rejected " + first->title;
    }();
    const core::drafts::DraftWorkspaceStore::EditUndoScope undo_scope(impl_->draft_workspace, label);

    for (const std::string& group_id : group_ids) {
        if (!impl_->draft_workspace.RejectGroup(group_id, error))
            return false;
    }
    for (const std::string& dependent : dependents) {
        const bool applied = scope == DraftRejectionScope::Cascade
                                 ? impl_->draft_workspace.RejectGroup(dependent, error)
                                 : impl_->draft_workspace.MarkGroupNeedsAttention(dependent, error);
        if (!applied)
            return false;
    }
    impl_->tree_needs_rebuild = true;
    return true;
}

void AppRuntime::BeginRejectDraftGroups(const std::vector<std::string>& group_ids) {
    const core::drafts::DraftWorkspace* workspace = impl_->draft_workspace.workspace();
    if (workspace == nullptr || group_ids.empty()) {
        SetStatus(AF_TR("There is nothing to reject."));
        return;
    }

    const std::vector<std::string> dependents = core::drafts::DependentsOf(*workspace, group_ids);
    if (dependents.empty()) {
        // Nothing else is affected, so there is no decision to put to the user.
        std::string error;
        const std::size_t rejected = group_ids.size();
        if (RejectDraftGroups(group_ids, DraftRejectionScope::Cascade, error)) {
            // Counted, because this takes a selection: the panel rejects one row
            // at a time today, but a message that says "the change" after
            // rejecting three is the kind of report a reviewer acts on.
            SetStatus(ui::i18n::trnf("Rejected {0} change. The accepted argument is unchanged.",
                                     "Rejected {0} changes. The accepted argument is unchanged.",
                                     static_cast<int>(rejected),
                                     rejected));
        } else {
            SetStatus(ui::i18n::trf("Could not reject this change: {0}", error));
        }
        return;
    }

    const auto title_of = [workspace](const std::string& group_id) {
        const core::drafts::DraftChangeGroup* group = workspace->FindGroup(group_id);
        if (group == nullptr || group->title.empty())
            return group_id;
        return group->title;
    };

    AppRuntimeState::PendingDraftRejection pending;
    pending.active = true;
    pending.selection = group_ids;
    pending.dependents = dependents;
    for (const std::string& group_id : group_ids)
        pending.selection_titles.push_back(title_of(group_id));
    for (const std::string& group_id : dependents)
        pending.dependent_titles.push_back(title_of(group_id));
    impl_->pending_draft_rejection = std::move(pending);
}

void AppRuntime::ResolvePendingDraftRejection(DraftRejectionScope scope) {
    if (!impl_->pending_draft_rejection.active)
        return;
    const std::vector<std::string> selection = impl_->pending_draft_rejection.selection;
    const std::size_t stranded = impl_->pending_draft_rejection.dependents.size();
    impl_->pending_draft_rejection = {};

    std::string error;
    if (!RejectDraftGroups(selection, scope, error)) {
        SetStatus(ui::i18n::trf("Could not reject this change: {0}", error));
        return;
    }
    // Two sentences, each pluralized on its own count. One sentence cannot be:
    // gettext pluralizes on a single number, and this message has two -- how many
    // changes the user rejected and how many were affected by it. Phrased without
    // a pronoun referring back to the selection, so neither half has to agree in
    // number with the other.
    const std::size_t rejected = selection.size();
    const std::string rejected_sentence =
        ui::i18n::trnf("Rejected {0} change.", "Rejected {0} changes.", static_cast<int>(rejected), rejected);
    const std::string consequence = scope == DraftRejectionScope::Cascade
                                        ? ui::i18n::trnf("{0} dependent change was rejected too.",
                                                         "{0} dependent changes were rejected too.",
                                                         static_cast<int>(stranded),
                                                         stranded)
                                        : ui::i18n::trnf("{0} change now needs attention before it can be accepted.",
                                                         "{0} changes now need attention before they can be accepted.",
                                                         static_cast<int>(stranded),
                                                         stranded);
    SetStatus(rejected_sentence + " " + consequence);
}

void AppRuntime::CancelPendingDraftRejection() {
    impl_->pending_draft_rejection = {};
}

void AppRuntime::FocusDraftGroupOnCanvas(const std::string& group_id) {
    const std::shared_ptr<const core::drafts::DraftMaterializationResult> materialization =
        impl_->draft_frame_materialization;
    if (materialization == nullptr)
        return;

    // The index is keyed by element, so the group's elements are found by asking
    // each changed element who contributed to it. In materialization order, so
    // "the first element this group changed" is stable between frames rather
    // than whichever the map happened to yield first.
    for (const std::string& element_id : materialization->change_index.ChangedElementIds()) {
        const std::vector<std::string> contributors = materialization->change_index.ContributingGroupIds(element_id);
        if (std::find(contributors.begin(), contributors.end(), group_id) == contributors.end())
            continue;
        // A removed element is not on the canvas to be centred on, but the
        // presentation view keeps it as a tombstone, so selecting it still lands
        // somewhere the user can see.
        impl_->events.Emit(SelectionChangedEvent{element_id, true});
        impl_->events.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
        return;
    }
}

bool AppRuntime::DiscardWorkingDraft(std::string& error) {
    error.clear();
    if (!impl_->draft_workspace.DiscardWorkspace(error))
        return false;
    impl_->tree_needs_rebuild = true;
    return true;
}

bool AppRuntime::OpenAgentRequestedCaseFile(const std::string& relative_path, std::string& error) {
    error.clear();
    if (!impl_->app_state.current_project.has_value()) {
        error = "No project is open.";
        return false;
    }

    for (const core::ProjectFileEntry& entry : impl_->app_state.current_project->files) {
        if (entry.role != core::ProjectFileRole::SacmArgument || entry.relativePath.generic_string() != relative_path) {
            continue;
        }
        // Goes through the same path a user's click takes, so the command bus,
        // the audit store and every side-storage controller are reconfigured for
        // the new file exactly as they would be otherwise.
        const std::filesystem::path wanted = impl_->app_state.current_project->rootPath / entry.relativePath;
        OpenProjectFile(entry);

        // `OpenProjectFile` returns normally in cases where it did NOT open the
        // file: unsaved changes raise a save-first modal and defer the open, and
        // a load failure returns void. Reporting success then would tell the
        // agent it had switched while every subsequent read answered from the
        // previous document -- an agent reasoning confidently about a file
        // nobody is looking at, which is the whole failure this design exists to
        // remove. So the result is taken from the state, not from the call.
        if (impl_->app_state.loaded_file_path == wanted) {
            return true;
        }
        if (impl_->project_controller->show_save_before_project_file_open_modal) {
            error = "The argument currently open has unsaved changes, so Assurance Forge is asking "
                    "the user whether to save before switching. Ask them to answer that prompt, "
                    "then try again.";
            return false;
        }
        error = impl_->app_state.status_message.empty() ? ("Assurance Forge could not open " + relative_path + ".")
                                                        : impl_->app_state.status_message;
        return false;
    }

    error = "No argument file in this project has the path \"" + relative_path + "\".";
    return false;
}

bool AppRuntime::TryOpenProjectManifest(const std::string& selected_path) {
    std::filesystem::path manifest_path(selected_path);
    if (!IsProjectManifestPath(manifest_path)) {
        SetStatus(AF_TR("Please select an af.proj file."));
        return false;
    }
    if (!impl_->app_state.open_project(selected_path)) {
        return false;
    }
    impl_->document_dirty = false;
    impl_->review_controller->ClearDirty();
    impl_->confidence_controller->ClearDirty();
    impl_->register_controller->ClearDirty();
    impl_->guideline_catalog_load_attempted = false;
    if (impl_->app_state.current_project.has_value()) {
        impl_->proposal_controller->manager.SetProjectRoot(impl_->app_state.current_project->rootPath);
        EnsureProjectSideStorage();
    }
    RefreshSacmPackageTreeCache();
    OpenFirstProjectSacmFile();
    TouchCurrentProjectRecent();
    CopyToBuffer(impl_->project_controller->open_project_path_buf,
                 sizeof(impl_->project_controller->open_project_path_buf),
                 selected_path);
    ImGui::CloseCurrentPopup();
    return true;
}

} // namespace app
