// Primary action toolbar.
//
// Every frequent action — save, undo, add a file, fit the canvas, export —
// lived only in a menu or a right-click, so nothing the user does often was
// visible. This surfaces existing commands; it introduces no capability of its
// own, and each button routes to the same callback the menu item does.
#pragma once

#include <functional>

namespace ui::panels {

enum class ToolbarAction {
    OpenProject,
    SaveProject,
    Undo,
    NewSacmFile,
    FitToView,
    ExportGsnSvg,
    Preferences,
};

struct ToolbarModel {
    bool has_project = false;
    bool has_loaded_case = false;
    bool can_undo = false;
    bool gsn_canvas_active = false;
    bool has_unsaved_changes = false;
};

struct ToolbarCallbacks {
    std::function<void()> open_project;
    std::function<void()> save_project;
    std::function<void()> undo;
    std::function<void()> new_sacm_file;
    std::function<void()> fit_to_view;
    std::function<void()> export_gsn_svg;
    std::function<void()> open_preferences;
};

// Whether `action` is available in the state `model` describes. Pure, so the
// disabled states are testable without a UI context — a Save button that looks
// available with no project open is a lie about what the application will do.
bool IsActionEnabled(const ToolbarModel& model, ToolbarAction action);

// Height the bar occupies. Valid only inside a frame; derives from font metrics.
float ToolbarHeight();

void ShowToolbar(const ToolbarModel& model, const ToolbarCallbacks& callbacks, float top_y);

} // namespace ui::panels
