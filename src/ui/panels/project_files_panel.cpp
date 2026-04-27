#include "ui/panels/project_files_panel.h"

namespace ui::panels {
namespace {

void RenderFile(const char* filename) {
    ImGui::TreeNodeEx(filename,
                      ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen,
                      "%s", filename);
}

void ShowProjectFilesTree() {
    if (ImGui::TreeNodeEx("arguments/", ImGuiTreeNodeFlags_DefaultOpen, "%s", "arguments/")) {
        RenderFile("main.sacm");
        RenderFile("perception.sacm");
        RenderFile("braking.sacm");
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("registers/", ImGuiTreeNodeFlags_DefaultOpen, "%s", "registers/")) {
        RenderFile("evidence-register.af.json");
        RenderFile("cse-register.af.json");
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("conformance/", ImGuiTreeNodeFlags_DefaultOpen, "%s", "conformance/")) {
        RenderFile("ul4600-conformance.af.json");
        RenderFile("iso26262-conformance.af.json");
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("exports/", ImGuiTreeNodeFlags_DefaultOpen, "%s", "exports/")) {
        RenderFile("review-pack.pdf");
        RenderFile("conformance-summary.xlsx");
        ImGui::TreePop();
    }
}

}  // namespace

void ShowProjectFilesPanel(float width, float height, float top_y, ImGuiWindowFlags panel_flags) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, top_y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("Project Files", nullptr, panel_flags);

    if (ImGui::BeginChild("ProjectFilesTree", ImVec2(0, 0), false)) {
        ShowProjectFilesTree();
    }
    ImGui::EndChild();

    ImGui::End();
}

}  // namespace ui::panels
