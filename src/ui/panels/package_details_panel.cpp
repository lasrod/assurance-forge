#include "ui/panels/package_details_panel.h"

#include "imgui.h"

namespace ui::panels {

void ShowPackageDetailsPanel(const sacm::SacmPackageTreeNode* package_node,
                             const std::filesystem::path& source_file_path) {
    if (!package_node) {
        ImGui::TextDisabled("No SACM package selected.");
        return;
    }

    ImGui::TextUnformatted(sacm::SacmPackageNodeTypeToDisplayString(package_node->type));
    ImGui::Separator();

    ImGui::Text("Name: %s", package_node->displayName.empty() ? "(unnamed)" : package_node->displayName.c_str());
    ImGui::Text("Type: %s", sacm::SacmPackageNodeTypeToString(package_node->type));
    if (!package_node->xmlLocalName.empty()) {
        ImGui::Text("XML element: %s", package_node->xmlLocalName.c_str());
    }
    if (!package_node->id.empty()) {
        ImGui::Text("ID: %s", package_node->id.c_str());
    }
    if (!package_node->gid.empty()) {
        ImGui::Text("GID: %s", package_node->gid.c_str());
    }
    if (!source_file_path.empty()) {
        ImGui::TextWrapped("Source file: %s", source_file_path.string().c_str());
    }

    if (!package_node->description.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted("Description");
        ImGui::TextWrapped("%s", package_node->description.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("A full editor for this SACM package type will be added in a later workflow.");
}

} // namespace ui::panels
