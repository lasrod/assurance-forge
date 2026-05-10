#include "app/areas/problems_area.h"

#include "app/app_runtime_state.h"
#include "ui/panels/problems_panel.h"
#include "ui/ui_state.h"

namespace app::areas {

void RenderProblemsAreaContent(AppRuntimeState& state, const ProblemsAreaCallbacks& callbacks) {
    ui::panels::ProblemsPanelModel model{
        state.problems_manager,
        ui::GetUiState(),
    };
    ui::panels::ProblemsPanelCallbacks panel_callbacks{
        callbacks.activate_problem,
        callbacks.quick_fix_problem,
    };
    ui::panels::ShowProblemsPanelContent(model, panel_callbacks, false);
}

} // namespace app::areas
