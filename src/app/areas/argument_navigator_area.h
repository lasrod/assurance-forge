#pragma once

#include "core/element_factory.h"
#include "imgui.h"
#include "ui/element_context_menu.h"
#include "ui/tree_view.h"

#include <functional>

namespace app {
struct AppRuntimeState;
namespace frame {
struct AppLayoutRegion;
}
} // namespace app

namespace app::areas {

struct ArgumentNavigatorAreaCallbacks {
    std::function<ui::ElementContextActions()> make_element_context_actions;
    std::function<void(core::NewElementKind)> add_proposal_child;
    std::function<void()> add_proposal_top_goal;
    std::function<void(core::RemoveMode)> remove_proposal_selected;
    std::function<void(const char*)> show_not_implemented;
    ui::TreeEditActions tree_edit_actions;
};

void RenderArgumentNavigatorArea(AppRuntimeState& state,
                                 const frame::AppLayoutRegion& region,
                                 ImGuiWindowFlags panel_flags,
                                 const ArgumentNavigatorAreaCallbacks& callbacks);

} // namespace app::areas