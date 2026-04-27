#include "ui/panels/project_files_panel.h"

#include <array>
#include <string_view>
#include <vector>

namespace ui::panels {
namespace {

struct FolderSpec {
    const char* path;
    const char* label;
};

constexpr std::array<FolderSpec, 4> kVisibleFolders = {{
    {"arguments", "arguments/"},
    {"registers", "registers/"},
    {"conformance", "conformance/"},
    {"exports", "exports/"},
}};

bool EntryBelongsToFolder(const core::ProjectFileEntry& entry, std::string_view folder) {
    auto relative = entry.relativePath.generic_string();
    return relative == folder || relative.rfind(std::string(folder) + "/", 0) == 0;
}

void RenderFile(const core::ProjectFileEntry& entry, const ProjectFilesPanelCallbacks& callbacks) {
    std::string label = entry.relativePath.filename().generic_string();
    ImGui::PushID(entry.relativePath.generic_string().c_str());
    ImGui::TreeNodeEx("file",
                      ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen,
                      "%s", label.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && callbacks.open_file) {
        callbacks.open_file(entry);
    }
    if (entry.state != core::ProjectFileState::Clean) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", core::ProjectFileStateToDisplayString(entry.state));
    }
    ImGui::PopID();
}

void RenderFolderContextMenu(std::string_view folder, const ProjectFilesPanelCallbacks& callbacks) {
    if (folder == "arguments") {
        if (ImGui::BeginPopupContextItem("##arguments_context")) {
            if (ImGui::MenuItem("Add New GSN / SACM File") && callbacks.add_sacm_file) {
                callbacks.add_sacm_file();
            }
            ImGui::EndPopup();
        }
        return;
    }

    if (folder == "registers") {
        if (ImGui::BeginPopupContextItem("##registers_context")) {
            if (ImGui::MenuItem("Add Evidence Register") && callbacks.add_evidence_register) {
                callbacks.add_evidence_register();
            }
            if (ImGui::MenuItem("Add J3377 CAE Register") && callbacks.add_j3377_cae_register) {
                callbacks.add_j3377_cae_register();
            }
            ImGui::EndPopup();
        }
    }
}

void ShowProjectFilesTree(const core::AssuranceProject& project, const ProjectFilesPanelCallbacks& callbacks) {
    ImGui::TextWrapped("%s", project.name.c_str());
    ImGui::TextDisabled("%s", project.rootPath.string().c_str());
    ImGui::Separator();

    for (const auto& folder : kVisibleFolders) {
        bool open = ImGui::TreeNodeEx(folder.label, ImGuiTreeNodeFlags_DefaultOpen, "%s", folder.label);
        RenderFolderContextMenu(folder.path, callbacks);
        if (!open) continue;

        bool has_files = false;
        for (const auto& entry : project.files) {
            if (!EntryBelongsToFolder(entry, folder.path)) continue;
            has_files = true;
            RenderFile(entry, callbacks);
        }
        if (!has_files) {
            ImGui::TextDisabled("No files");
        }
        ImGui::TreePop();
    }
}

}  // namespace

void ShowProjectFilesPanel(float width,
                           float height,
                           float top_y,
                           ImGuiWindowFlags panel_flags,
                           ProjectFilesPanelModel model,
                           const ProjectFilesPanelCallbacks& callbacks) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, top_y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("Project Files", nullptr, panel_flags);

    if (ImGui::BeginChild("ProjectFilesTree", ImVec2(0, 0), false)) {
        if (model.project) {
            ShowProjectFilesTree(*model.project, callbacks);
        } else {
            ImGui::TextDisabled("No project open.");
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

}  // namespace ui::panels
