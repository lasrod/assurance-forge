#include "ui/element_context_menu.h"

#include "imgui.h"

#include <cstdio>

namespace ui {

void RenderAddElementMenu(const ElementContextActions& actions) {
    if (ImGui::BeginMenu("Add")) {
        const bool can_add = static_cast<bool>(actions.add_child);
        if (ImGui::MenuItem("Goal", nullptr, false, can_add)) actions.add_child(core::NewElementKind::Goal);
        if (ImGui::MenuItem("Strategy", nullptr, false, can_add)) actions.add_child(core::NewElementKind::Strategy);
        if (ImGui::MenuItem("Solution", nullptr, false, can_add)) actions.add_child(core::NewElementKind::Solution);
        if (ImGui::MenuItem("Context", nullptr, false, can_add)) actions.add_child(core::NewElementKind::Context);
        if (ImGui::MenuItem("Assumption", nullptr, false, can_add)) actions.add_child(core::NewElementKind::Assumption);
        if (ImGui::MenuItem("Justification", nullptr, false, can_add)) actions.add_child(core::NewElementKind::Justification);
        ImGui::Separator();
        if (ImGui::MenuItem("Challenge", nullptr, false, static_cast<bool>(actions.not_implemented))) {
            actions.not_implemented("Challenge");
        }
        ImGui::EndMenu();
    }
}

void RenderRemoveSubmenu(const parser::AssuranceCase* active_case,
                         const std::string& selected_id,
                         const ElementContextActions& actions) {
    if (ImGui::BeginMenu("Remove")) {
        if (selected_id.empty()) {
            ImGui::TextDisabled("No element selected.");
            ImGui::EndMenu();
            return;
        }

        auto count_for = [&](core::RemoveMode mode) -> int {
            if (!active_case) return 0;
            return static_cast<int>(core::PlanRemoval(*active_case, selected_id, mode).size());
        };

        const int n_only = count_for(core::RemoveMode::NodeOnly);
        const int n_descendants = count_for(core::RemoveMode::NodeAndDescendants);
        const bool can_remove = static_cast<bool>(actions.remove_selected);

        char label[96];

        std::snprintf(label, sizeof(label), "This node only (%d)", n_only);
        if (ImGui::MenuItem(label, nullptr, false, can_remove && n_only > 0)) {
            actions.remove_selected(core::RemoveMode::NodeOnly);
        }

        std::snprintf(label, sizeof(label), "Node and descendants (%d)", n_descendants);
        if (ImGui::MenuItem(label, nullptr, false, can_remove && n_descendants > n_only)) {
            actions.remove_selected(core::RemoveMode::NodeAndDescendants);
        }

        ImGui::EndMenu();
    }
}

}  // namespace ui
