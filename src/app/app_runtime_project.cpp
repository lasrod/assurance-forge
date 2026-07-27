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
#include "app/translation_review_sync.h"
#include "core/translation_review_store.h"
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
#include "core/terminology_package_service.h"
#include "core/terminology_text_utils.h"
#include "export/gsn_svg_exporter.h"
#include "imgui.h"
#include "parser/model_utils.h"
#include "sacm/sacm_package_tree.h"
#include "sacm/sacm_serializer.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/imgui_buffer_utils.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
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
        SetStatus("Browse failed: " + error_message);
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
        SetStatus("Browse failed: " + error_message);
    }
}

void AppRuntime::TouchCurrentProjectRecent() {
    impl_->project_controller->TouchCurrentProjectRecent(impl_->app_state);
}

void AppRuntime::BeginCreateProjectSacmFile() {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus("Create or open a project first.");
        return;
    }
    impl_->project_controller->BeginProjectFileCreate(ProjectFileCreateKind::Sacm, "main.sacm");
}

void AppRuntime::BeginCreateProjectEvidenceRegister() {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus("Create or open a project first.");
        return;
    }
    impl_->project_controller->BeginProjectFileCreate(ProjectFileCreateKind::EvidenceRegister,
                                                      "evidence-register.af.json");
}

void AppRuntime::BeginCreateProjectJ3377CaeRegister() {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus("Create or open a project first.");
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
            SetStatus("SACM file is already open: " + entry.relativePath.generic_string());
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
    if (entry.role == core::ProjectFileRole::SacmArgument &&
        impl_->app_state.current_project.has_value()) {
        std::string migration_error;
        if (!core::audit::MigrateStrategyEncodingIfNeeded(impl_->app_state.current_project.value(),
                                                          entry.relativePath, strategy_migration,
                                                          migration_error)) {
            SetStatus("Strategy encoding migration failed: " + migration_error);
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
            auto bus = core::commands::CommandBus::Open(impl_->app_state.current_project.value(),
                                                        ProjectFilePath(impl_->app_state, entry), bus_error);
            if (!bus) {
                SetStatus("Audit bus init failed: " + bus_error);
            } else {
                impl_->command_bus = std::move(bus);
            }
        }

        // Audit replay verification (design §13). Best-effort: a mismatch is
        // surfaced as a warning, not an error — the loaded SACM remains the
        // user's working state.
        impl_->last_audit_verification.reset();
        if (impl_->app_state.current_project.has_value()) {
            auto verification =
                core::audit::VerifyProject(impl_->app_state.current_project.value());
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
        SetStatus("Cannot reconcile audit log: no project is open.");
        return false;
    }
    if (impl_->app_state.active_project_file_role != core::ProjectFileRole::SacmArgument) {
        SetStatus("Cannot reconcile audit log: no SACM file is active.");
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
        SetStatus("Cannot reconcile audit log: active SACM file is no longer listed in the project.");
        return false;
    }

    // Persist any in-memory edits first; the new initial snapshot is built
    // from the on-disk SACM bytes, so we want them to reflect live state.
    if (impl_->document_dirty) {
        if (!SaveProject()) {
            SetStatus("Cannot reconcile audit log: failed to save current SACM file.");
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
        SetStatus("Audit reconciliation failed: " + error);
        // Best-effort: re-open the project so the user retains a working
        // session against the original (now-restored) audit artifacts.
        PerformOpenProjectFile(entry_copy);
        return false;
    }

    SetStatus("Audit log reconciled. Previous artifacts backed up to " + result.backup_dir + ".");

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
            SetStatus("Save the current SACM file before opening another package.");
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
                SetStatus("Opened argument package; no focusable argument element was found in the package.");
            }
        }
        OpenArgumentPackageCanvas(node.id, node.gid, node.displayName, first_id);
        return;
    }

    if (node.type == sacm::SacmPackageNodeType::TerminologyPackage) {
        if (!CanSwitchProjectSacmFile(impl_->app_state, entry)) {
            SetStatus("Save the current SACM file before opening another package.");
            return;
        }
        if (!EnsureProjectSacmFileOpen(*impl_, entry, false))
            return;
        if (!impl_->app_state.has_projected_package()) {
            SetStatus("Opened SACM file, but no editable package model was available.");
            return;
        }

        core::TerminologyPackageRef package_ref{node.id, node.gid};
        const sacm::TerminologyPackage* terminology_package =
            core::FindTerminologyPackage(impl_->app_state.projected_package(), package_ref);
        if (!terminology_package) {
            SetStatus("Terminology package was not found in the editable model.");
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
        SetStatus("Open a project before removing files.");
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
        SetStatus("Removing this file type is not supported here.");
        return false;
    }

    if (!removed) {
        SetStatus("Remove file failed: " + error);
        return false;
    }

    impl_->events.Emit(DocumentDirtyEvent{});
    impl_->events.Emit(ProjectFilesChangedEvent{});
    core::ProjectService::RefreshFileStatus(project);
    SetStatus("Removed " + relative_path.generic_string() + ".");
    return true;
}

bool AppRuntime::RevealProjectFileInExplorer(const core::ProjectFileEntry& entry) {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus("Open a project before revealing files.");
        return false;
    }

    const std::filesystem::path absolute_path = impl_->app_state.current_project->rootPath / entry.relativePath;
    std::string error;
    if (!app::dialogs::RevealPathInFileExplorer(absolute_path, error)) {
        SetStatus("Could not open File Explorer: " + error);
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
        SetStatus("Save the current SACM file before removing a package.");
        return;
    }
    if (!EnsureProjectSacmFileOpen(*impl_, entry, false))
        return;
    if (!impl_->app_state.has_projected_package()) {
        SetStatus("Could not load an editable SACM package model.");
        return;
    }

    const std::string label = node.displayName.empty() ? node.id : node.displayName;
    std::string status_message;
    std::string kind_label;
    std::unique_ptr<core::commands::ICommand> command;

    switch (node.type) {
    case sacm::SacmPackageNodeType::TerminologyPackage: {
        command = std::make_unique<core::commands::RemoveTerminologyPackageCommand>(node.id, node.gid);
        kind_label = "terminology package";
        break;
    }
    case sacm::SacmPackageNodeType::ArgumentPackage: {
        command = std::make_unique<core::commands::RemoveArgumentPackageCommand>(node.id, node.gid);
        kind_label = "argument package";
        break;
    }
    case sacm::SacmPackageNodeType::ArtifactPackage: {
        command = std::make_unique<core::commands::RemoveArtifactPackageCommand>(node.id, node.gid);
        kind_label = "artifact package";
        break;
    }
    default:
        SetStatus("Removing this package type is not supported yet.");
        return;
    }

    const auto outcome = app::commands::DispatchAuditedCommand(*impl_, *command);
    if (!outcome.success) {
        SetStatus("Remove " + kind_label + " failed: " + outcome.error);
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

    status_message = "Removed " + kind_label + " " + label + ".";
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
    app::SyncTranslationReviewProblems(impl_->problems_manager, model, impl_->translation_review_pending_ids,
                                       pending_changed);
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
        SetStatus("Register problem does not identify an assessment.");
        return;
    }
    if (impl_->register_controller->HasStorageError()) {
        SetStatus("Register assessments could not be loaded, so nothing can be discarded: " +
                  impl_->register_controller->StorageError());
        return;
    }

    const bool discarded = ref.kind == RegisterAssessmentKind::Cse
                               ? impl_->register_controller->DiscardCseAssessment(ref.key)
                               : impl_->register_controller->DiscardEvidenceAssessment(ref.key);
    if (!discarded) {
        SetStatus("That register assessment was already discarded.");
        impl_->problems_dirty.registers = true;
        return;
    }

    // The file is only rewritten on save, so say so: this is the one undo the
    // register store has.
    SetStatus("Discarded the assessment of " + ref.key +
              ". Close the project without saving to keep it after all.");
}

void AppRuntime::HandleProblemQuickFix(const core::ProblemItem& problem) {
    if (problem.type.rfind("Acp", 0) == 0) {
        if (problem.quick_fix_payload.empty()) {
            SetStatus("ACP problem does not identify an ACP.");
            return;
        }
        ui::UiState& ui_state = ui::GetUiState();
        ui_state.selected_acp_id = problem.quick_fix_payload;
        ui_state.selected_element_id.clear();
        ui_state.selected_relationship_id.clear();
        ui_state.selected_relationship_edge_key.clear();
        ui_state.center_view = ui::CenterView::GsnCanvas;
        impl_->workbench.force_center_tab_selection = true;
        SetStatus("Opened " + problem.quick_fix_payload);
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

    SetStatus("Project opened, but no SACM file could be loaded.");
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
    SetStatus("Review items could not be loaded: " + error);
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
    SetStatus("Confidence assessments could not be loaded: " + error);
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
    SetStatus("Register assessments could not be loaded: " + error);
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
        // to key an endpoint record on, and no proposals directory to work
        // against, so there is nothing useful to serve.
        impl_->agent_bridge->Stop();
        return;
    }

    std::string error;
    if (!impl_->agent_bridge->Start(impl_->app_state.current_project->rootPath, kAppVersion,
                                    error)) {
        // Not fatal. The application is perfectly usable without an AI client
        // attached, and failing an project open over it would be a poor trade.
        SetStatus("AI clients cannot connect to this project: " + error);
    }
}

bool AppRuntime::PollAgentBridge() {
    if (impl_->agent_bridge == nullptr || !impl_->agent_bridge->listening()) {
        return false;
    }

    const std::string project_path =
        impl_->app_state.current_project.has_value()
            ? impl_->app_state.current_project->rootPath.generic_string()
            : std::string();

    const int handled = impl_->agent_bridge->PollPendingRequests(
        [this, &project_path](const bridge::Request&              request,
                              const controllers::AgentConnection& connection) {
            const AgentRequestContext context{
                impl_->app_state,
                project_path,
                connection.client_label,
                [this](const std::string& relative_path, std::string& error) {
                    return OpenAgentRequestedCaseFile(relative_path, error);
                },
                &impl_->agent_change_sets,
                connection.id};
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
    // The user may have edited the argument while reading the proposal, and a
    // patch that no longer applies must be refused rather than forced.
    const core::reviews::ProposalValidityResult validity =
        core::reviews::EvaluateReviewProposalValidity(change_set->proposal,
                                                      impl_->app_state.loaded_case.value());
    if (validity.validity != core::reviews::ProposalValidity::Valid) {
        error = "The argument changed while this was being reviewed, so it no longer applies: " +
                validity.reason;
        return false;
    }

    // The same audited command an accepted proposal has always gone through, so
    // this edit is recorded, undoable and attributed exactly like one made with
    // the mouse. No new command type exists for agent-authored changes, which is
    // what guarantees they cannot do anything the application could not.
    core::commands::ApplyProposalCommand command(change_set->proposal);
    const app::commands::DispatchOutcome outcome =
        app::commands::DispatchAuditedCommand(*impl_, command);
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

bool AppRuntime::OpenAgentRequestedCaseFile(const std::string& relative_path, std::string& error) {
    error.clear();
    if (!impl_->app_state.current_project.has_value()) {
        error = "No project is open.";
        return false;
    }

    for (const core::ProjectFileEntry& entry : impl_->app_state.current_project->files) {
        if (entry.role != core::ProjectFileRole::SacmArgument ||
            entry.relativePath.generic_string() != relative_path) {
            continue;
        }
        // Goes through the same path a user's click takes, so the command bus,
        // the audit store and every side-storage controller are reconfigured for
        // the new file exactly as they would be otherwise.
        const std::filesystem::path wanted =
            impl_->app_state.current_project->rootPath / entry.relativePath;
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
        error = impl_->app_state.status_message.empty()
                    ? ("Assurance Forge could not open " + relative_path + ".")
                    : impl_->app_state.status_message;
        return false;
    }

    error = "No argument file in this project has the path \"" + relative_path + "\".";
    return false;
}

bool AppRuntime::TryOpenProjectManifest(const std::string& selected_path) {
    std::filesystem::path manifest_path(selected_path);
    if (!IsProjectManifestPath(manifest_path)) {
        SetStatus("Please select an af.proj file.");
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
