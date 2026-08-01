#include "app/areas/toolbar_area.h"

#include "app/app_runtime_state.h"
#include "ui/ui_state.h"

namespace app::areas {

void RenderToolbarArea(AppRuntimeState& state, const ui::panels::ToolbarCallbacks& callbacks, float top_y) {
    ui::panels::ToolbarModel model;
    model.has_project = state.app_state.current_project.has_value();
    model.has_loaded_case = state.app_state.loaded_case.has_value();
    model.has_unsaved_changes = state.app_state.has_unsaved_changes;
    model.gsn_canvas_active = ui::GetUiState().center_view == ui::CenterView::GsnCanvas;

    ui::panels::ShowToolbar(model, callbacks, top_y);
}

} // namespace app::areas
