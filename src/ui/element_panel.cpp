#include "ui/element_panel.h"
#include "ui/ui_state.h"
#include "imgui.h"

namespace ui {

void ShowElementPanel(const parser::AssuranceCase* ac) {
    const UiState& state = GetUiState();

    if (state.selected_element_id.empty()) {
        ImGui::TextDisabled("No element selected.");
        return;
    }

    if (!ac) {
        ImGui::TextDisabled("No safety case loaded.");
        return;
    }

    // Find the element by ID
    const parser::SacmElement* found = nullptr;
    for (const auto& elem : ac->elements) {
        if (elem.id == state.selected_element_id) {
            found = &elem;
            break;
        }
    }

    if (!found) {
        ImGui::TextDisabled("Element not found: %s", state.selected_element_id.c_str());
        return;
    }

    // ID
    ImGui::Text("ID");
    ImGui::Separator();
    ImGui::TextWrapped("%s", found->id.c_str());
    ImGui::Spacing();

    // Title (name)
    ImGui::Text("Title");
    ImGui::Separator();
    if (!found->name.empty())
        ImGui::TextWrapped("%s", found->name.c_str());
    else
        ImGui::TextDisabled("(none)");
    ImGui::Spacing();

    // Type
    ImGui::Text("Type");
    ImGui::Separator();
    ImGui::TextWrapped("%s", found->type.c_str());
    ImGui::Spacing();

    // Description
    ImGui::Text("Description");
    ImGui::Separator();
    if (!found->description.empty())
        ImGui::TextWrapped("%s", found->description.c_str());
    else if (!found->content.empty())
        ImGui::TextWrapped("%s", found->content.c_str());
    else
        ImGui::TextDisabled("(none)");
}

}  // namespace ui
