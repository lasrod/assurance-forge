#include "app/areas/term_usages_area.h"

#include "app/app_runtime_state.h"
#include "ui/panels/terminology_usages_panel.h"

namespace app::areas {

void RenderTermUsagesAreaContent(AppRuntimeState& state, const TermUsagesAreaCallbacks& callbacks) {
    ui::panels::TerminologyUsagesPanelModel model;
    model.has_search = state.terminology.usages_active;
    model.term_value = state.terminology.usage_search_term_value;
    model.term_name = state.terminology.usage_search_term_name;
    model.message = state.terminology.usage_search_message;
    model.error = state.terminology.usage_search_error;
    model.usages = &state.terminology.usage_results;
    model.selected_usage_index = state.terminology.selected_usage_index;

    ui::panels::TerminologyUsagesPanelCallbacks panel_callbacks;
    panel_callbacks.select_usage = [&state](std::size_t usage_index) {
        if (usage_index < state.terminology.usage_results.size())
            state.terminology.selected_usage_index = static_cast<int>(usage_index);
    };
    panel_callbacks.activate_usage = callbacks.activate_usage;
    ui::panels::ShowTerminologyUsagesPanelContent(model, panel_callbacks);
}

} // namespace app::areas
