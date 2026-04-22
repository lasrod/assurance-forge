#include "ui/sccg_panel.h"

#include "core/sccg_review.h"
#include "imgui.h"

namespace ui {

void ShowSccgGuidelinesView() {
    const std::vector<core::SccgGuideline>& guidelines = core::GetSccgGuidelines();
    if (guidelines.empty()) {
        ImGui::TextDisabled("No SCCG guidelines loaded. Expected data/sccg_guidelines.tsv.");
        return;
    }

    static int selected_idx = 0;
    if (selected_idx < 0) selected_idx = 0;
    if (selected_idx >= static_cast<int>(guidelines.size())) {
        selected_idx = static_cast<int>(guidelines.size()) - 1;
    }

    ImGui::Text("Safety Case Core Guidelines");
    ImGui::Separator();

    ImGui::BeginChild("SccgList", ImVec2(320, 0), true);
    for (int i = 0; i < static_cast<int>(guidelines.size()); ++i) {
        const core::SccgGuideline& g = guidelines[i];
        std::string label = g.id + " - " + g.title;
        if (ImGui::Selectable(label.c_str(), i == selected_idx)) {
            selected_idx = i;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("SccgDetail", ImVec2(0, 0), true);
    const core::SccgGuideline& g = guidelines[selected_idx];

    ImGui::Text("ID: %s", g.id.c_str());
    ImGui::Text("Category: %s", g.category.c_str());
    ImGui::Separator();

    ImGui::TextWrapped("%s", g.title.c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("%s", g.guideline.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Source: Safety Case Core Guidelines (CC BY 4.0)");
    ImGui::TextDisabled("https://lasrod.github.io/safety-case-core-guidelines/");

    ImGui::EndChild();
}

}  // namespace ui
