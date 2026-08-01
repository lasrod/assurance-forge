#include "app/areas/status_bar_area.h"

#include "app/app_runtime_state.h"
#include "ui/panels/status_bar_panel.h"
#include "ui/ui_state.h"

#include <filesystem>

namespace app::areas {
namespace {

// Prefer the file the document was actually loaded from; fall back to the
// project file the explorer considers active, which is what is set before a
// newly created file has been written.
std::filesystem::path StatusDocumentPath(const AppRuntimeState& state) {
    if (!state.app_state.loaded_file_path.empty())
        return state.app_state.loaded_file_path;
    return state.app_state.active_project_file_path;
}

} // namespace

void RenderStatusBarArea(AppRuntimeState& state) {
    ui::panels::StatusBarModel model;

    model.has_project = state.app_state.current_project.has_value();
    if (model.has_project)
        model.project_name = state.app_state.current_project->name;

    const std::filesystem::path document = StatusDocumentPath(state);
    if (!document.empty()) {
        model.document_name = document.filename().generic_string();
        model.document_full_path = document.generic_string();
    }
    model.has_unsaved_changes = state.app_state.has_unsaved_changes;

    // An autosave failure outranks whatever the last ordinary message was: it is
    // precisely the case where the user must not assume their work is on disk.
    if (!state.last_autosave_error.empty()) {
        model.message = state.last_autosave_error;
        model.message_is_error = true;
    } else {
        model.message = state.app_state.status_message;
    }

    for (const core::ProblemItem& problem : state.problems_manager.GetProblems()) {
        if (problem.severity == core::ProblemSeverity::Error)
            ++model.error_count;
        else if (problem.severity == core::ProblemSeverity::Warning)
            ++model.warning_count;
    }

    ui::UiState& ui_state = ui::GetUiState();
    model.selected_element_id = ui_state.selected_element_id;

    ui::panels::StatusBarCallbacks callbacks;
    callbacks.open_problems = [&ui_state]() { ui_state.problems_panel_open_pending = true; };

    ui::panels::ShowStatusBar(model, callbacks);
}

} // namespace app::areas
