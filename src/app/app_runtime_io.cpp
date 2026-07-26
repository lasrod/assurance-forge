#include "app/app_runtime.h"

#include "app/app_runtime_state.h"
#include "core/problems/problem_utils.h"
#include "core/project_service.h"
#include "export/gsn_svg_exporter.h"
#include "ui/text_edit_session.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace app {

void AppRuntime::FlushPendingTextEdits() {
    std::vector<ui::PendingTextEdit> pending = ui::TextEditSession::CollectPendingEdits();
    if (pending.empty())
        return;
    if (!impl_->app_state.loaded_case.has_value() || !impl_->app_state.has_projected_package())
        return;
    impl_->element_edit_controller->FlushPendingTextEdits(*impl_, pending);
}

bool AppRuntime::SaveProject() {
    // Commit any in-progress inspector edit first. The model already holds the
    // typed value; without this the un-audited SACM write below would persist
    // it with no matching audit transaction, diverging the replay log.
    FlushPendingTextEdits();

    if (!impl_->app_state.current_project.has_value()) {
        impl_->app_state.status_message = "Create or open a project first.";
        return false;
    }

    // Flush controllers that own their own persistence (review items,
    // confidence assessments and register assessments live in separate files
    // outside SACM). These do not flow through CommandBus and never produce
    // audit transactions.
    if (impl_->review_controller->IsDirty()) {
        core::AssuranceProject& project = impl_->app_state.current_project.value();
        std::string error;
        if (!impl_->review_controller->SaveIfDirty(project, error)) {
            impl_->app_state.status_message = "Review item save failed: " + error;
            return false;
        }
    }

    if (impl_->confidence_controller->IsDirty()) {
        core::AssuranceProject& project = impl_->app_state.current_project.value();
        std::string error;
        if (!impl_->confidence_controller->SaveIfDirty(project, error)) {
            impl_->app_state.status_message = "Confidence save failed: " + error;
            return false;
        }
    }

    if (impl_->register_controller->IsDirty()) {
        core::AssuranceProject& project = impl_->app_state.current_project.value();
        std::string error;
        if (!impl_->register_controller->SaveIfDirty(project, error)) {
            impl_->app_state.status_message = "Register assessment save failed: " + error;
            return false;
        }
    }

    // Write SACM when the in-memory model has unflushed mutations.
    // Audited commands already wrote SACM atomically through CommandBus and
    // left the audit manifest in sync, so for those flows `document_dirty`
    // is false and re-saving would just rewrite identical bytes. The
    // bypass paths (terminology / proposal / package-removal) set
    // `document_dirty` so this branch fires. Other code paths that only
    // call `app_state.mark_dirty()` without emitting `DocumentDirtyEvent`
    // (e.g. the no-bus fallback in `DispatchAuditedCommand` before it
    // started emitting the event, or any future direct mutation) still
    // need their changes persisted, so `has_unsaved_changes` also forces
    // a save.
    const bool needs_sacm_save = impl_->document_dirty || impl_->app_state.has_unsaved_changes;
    if (needs_sacm_save) {
        if (!impl_->app_state.save_project())
            return false;
        impl_->document_dirty = false;
    }

    // Either we just flushed everything, or there was nothing to flush. In
    // both cases the project is consistent on disk now.
    impl_->app_state.has_unsaved_changes = false;
    impl_->app_state.status_message = "Project saved: " + impl_->app_state.current_project->name;
    return true;
}

void AppRuntime::ExportGsnSvg() {
    constexpr const char* kExportProblemPrefix = "gsn-svg-export:";
    core::ClearProblemsByIdPrefix(impl_->problems_manager, kExportProblemPrefix);

    if (!impl_->app_state.current_project.has_value()) {
        SetStatus("GSN SVG export failed: no project is open.");
        return;
    }
    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("GSN SVG export failed: no SACM safety case is open.");
        return;
    }

    std::filesystem::path source_path = impl_->app_state.active_project_file_path;
    if (source_path.empty())
        source_path = impl_->app_state.loaded_file_path;
    std::string source_stem = source_path.stem().string();
    if (source_stem.empty())
        source_stem = impl_->app_state.loaded_case->name;

    export_gsn::GsnSvgExportResult export_result = export_gsn::ExportCurrentSafetyCaseToGsnSvg(
        impl_->app_state.loaded_case.value(), impl_->app_state.current_project->rootPath, source_stem);
    if (!export_result.success) {
        SetStatus("GSN SVG export failed: " + export_result.error_message);
        return;
    }

    std::filesystem::path display_path = export_result.output_path;
    std::error_code ec;
    std::filesystem::path relative_path =
        std::filesystem::relative(export_result.output_path, impl_->app_state.current_project->rootPath, ec);
    if (!ec && !relative_path.empty()) {
        display_path = relative_path;
        core::ProjectFileEntry tracked_export;
        std::string track_error;
        if (!core::ProjectService::TrackExistingFile(impl_->app_state.current_project.value(),
                                                     relative_path,
                                                     core::ProjectFileRole::ExportedReport,
                                                     tracked_export,
                                                     track_error)) {
            export_result.warnings.push_back("Exported SVG was written but could not be added to Project Explorer: " +
                                             track_error);
        }
    } else {
        export_result.warnings.push_back(
            "Exported SVG was written but its project-relative path could not be determined.");
    }

    for (size_t i = 0; i < export_result.warnings.size(); ++i) {
        core::ProblemItem problem;
        problem.id = std::string(kExportProblemPrefix) + std::to_string(i + 1);
        problem.severity = core::ProblemSeverity::Warning;
        problem.source = core::ProblemSource::ImportExport;
        problem.type = "GsnSvgExport";
        problem.message = export_result.warnings[i];
        impl_->problems_manager.AddOrUpdateProblem(problem);
    }

    if (!export_result.warnings.empty()) {
        SetStatus("GSN SVG exported with warnings. See Problems/Export log.");
    } else {
        SetStatus("GSN SVG exported to " + display_path.generic_string());
    }
}

} // namespace app
