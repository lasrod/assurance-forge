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

    const std::string display_name = package_node->displayName.empty() ? AF_TR("(unnamed)") : package_node->displayName;
    ImGui::TextUnformatted(ui::i18n::trf("Name: {0}", display_name).c_str());
    ImGui::TextUnformatted(ui::i18n::trf("Type: {0}", sacm::SacmPackageNodeTypeToString(package_node->type)).c_str());
    if (!package_node->xmlLocalName.empty()) {
        ImGui::TextUnformatted(ui::i18n::trf("XML element: {0}", package_node->xmlLocalName).c_str());
    }
    if (!package_node->id.empty()) {
        ImGui::TextUnformatted(ui::i18n::trf("ID: {0}", package_node->id).c_str());
    }
    if (!package_node->gid.empty()) {
        ImGui::TextUnformatted(ui::i18n::trf("GID: {0}", package_node->gid).c_str());
    }
    if (!source_file_path.empty()) {
        ImGui::TextWrapped("%s", ui::i18n::trf("Source file: {0}", source_file_path.string()).c_str());
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
