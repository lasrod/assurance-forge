#include "ui/element_context_menu.h"

#include "ui/i18n/localization.h"

#include "imgui.h"

#include <cstdio>

namespace ui {

void RenderAddElementMenu(const ElementContextActions& actions) {
    if (ImGui::BeginMenu(AF_TR("Add").c_str())) {
        const bool can_add = static_cast<bool>(actions.add_child);
        if (ImGui::MenuItem(AF_TR("Goal").c_str(), nullptr, false, can_add))
            actions.add_child(core::NewElementKind::Goal);
        if (ImGui::MenuItem(AF_TR("Strategy").c_str(), nullptr, false, can_add))
            actions.add_child(core::NewElementKind::Strategy);
        if (ImGui::MenuItem(AF_TR("Solution").c_str(), nullptr, false, can_add))
            actions.add_child(core::NewElementKind::Solution);
        if (ImGui::MenuItem(AF_TR("Context").c_str(), nullptr, false, can_add))
            actions.add_child(core::NewElementKind::Context);
        if (ImGui::MenuItem(AF_TR("Assumption").c_str(), nullptr, false, can_add))
            actions.add_child(core::NewElementKind::Assumption);
        if (ImGui::MenuItem(AF_TR("Justification").c_str(), nullptr, false, can_add))
            actions.add_child(core::NewElementKind::Justification);
        ImGui::Separator();
        if (ImGui::MenuItem(AF_TR("ACP").c_str(), nullptr, false, static_cast<bool>(actions.add_acp_to_selected_element)))
            actions.add_acp_to_selected_element();
        ImGui::Separator();
        if (ImGui::MenuItem(AF_TR("Challenge").c_str(), nullptr, false, static_cast<bool>(actions.not_implemented))) {
            actions.not_implemented(AF_TR("Challenge").c_str());
        }
        ImGui::EndMenu();
    }
}

void RenderAiReviewMenu(const ElementContextActions& actions) {
    if (actions.render_ai_review_menu)
        actions.render_ai_review_menu();
}

void RenderRemoveSubmenu(const parser::AssuranceCase* active_case,
                         const std::string& selected_id,
                         const ElementContextActions& actions) {
    if (ImGui::BeginMenu(AF_TR("Remove").c_str())) {
        if (selected_id.empty()) {
            ImGui::TextDisabled("%s", AF_TR("No element selected.").c_str());
            ImGui::EndMenu();
            return;
        }

        auto count_for = [&](core::RemoveMode mode) -> int {
            if (!active_case)
                return 0;
            return static_cast<int>(core::PlanRemoval(*active_case, selected_id, mode).size());
        };

        const int n_only = count_for(core::RemoveMode::NodeOnly);
        const int n_descendants = count_for(core::RemoveMode::NodeAndDescendants);
        const bool can_remove = static_cast<bool>(actions.remove_selected);

        const std::string node_only_label = ui::i18n::trf("This node only ({0})", n_only);
        if (ImGui::MenuItem(node_only_label.c_str(), nullptr, false, can_remove && n_only > 0)) {
            actions.remove_selected(core::RemoveMode::NodeOnly);
        }

        const std::string descendants_label = ui::i18n::trf("Node and descendants ({0})", n_descendants);
        if (ImGui::MenuItem(descendants_label.c_str(), nullptr, false, can_remove && n_descendants > n_only)) {
            actions.remove_selected(core::RemoveMode::NodeAndDescendants);
        }

        ImGui::EndMenu();
    }
}

} // namespace ui
