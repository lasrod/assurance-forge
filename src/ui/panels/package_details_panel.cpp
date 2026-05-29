#include "ui/panels/package_details_panel.h"

#include "imgui.h"
#include "ui/i18n/localization.h"

namespace ui::panels {

void ShowPackageDetailsPanel(const sacm::SacmPackageTreeNode* package_node,
                             const std::filesystem::path& source_file_path) {
    if (!package_node) {
        ImGui::TextDisabled("%s", AF_TR("No SACM package selected.").c_str());
        return;
    }

    ImGui::TextUnformatted(sacm::SacmPackageNodeTypeToDisplayString(package_node->type));
    ImGui::Separator();

    ImGui::Text(AF_TR("Name: %s").c_str(),
                package_node->displayName.empty() ? AF_TR("(unnamed)").c_str() : package_node->displayName.c_str());
    ImGui::Text(AF_TR("Type: %s").c_str(), sacm::SacmPackageNodeTypeToString(package_node->type));
    if (!package_node->xmlLocalName.empty()) {
        ImGui::Text(AF_TR("XML element: %s").c_str(), package_node->xmlLocalName.c_str());
    }
    if (!package_node->id.empty()) {
        ImGui::Text(AF_TR("ID: %s").c_str(), package_node->id.c_str());
    }
    if (!package_node->gid.empty()) {
        ImGui::Text(AF_TR("GID: %s").c_str(), package_node->gid.c_str());
    }
    if (!source_file_path.empty()) {
        ImGui::TextWrapped(AF_TR("Source file: %s").c_str(), source_file_path.string().c_str());
    }

    if (!package_node->description.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted(AF_TR("Description").c_str());
        ImGui::TextWrapped("%s", package_node->description.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("%s",
                        AF_TR("A full editor for this SACM package type will be added in a later workflow.").c_str());
}

} // namespace ui::panels
