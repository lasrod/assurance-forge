#include "ui/panels/project_files_panel.h"

#include "ui/theme.h"

#include <array>
#include <string_view>
#include <vector>

namespace ui::panels {
namespace {

struct FolderSpec {
    const char* path;
    const char* label;
};

constexpr std::array<FolderSpec, 5> kVisibleFolders = {{
    {"arguments", "arguments/"},
    {"registers", "registers/"},
    {"reviews", "reviews/"},
    {"conformance", "conformance/"},
    {"exports", "exports/"},
}};

bool EntryBelongsToFolder(const core::ProjectFileEntry& entry, std::string_view folder) {
    auto relative = entry.relativePath.generic_string();
    return relative == folder || relative.rfind(std::string(folder) + "/", 0) == 0;
}

bool IsInterfaceNode(sacm::SacmPackageNodeType type) {
    return type == sacm::SacmPackageNodeType::AssuranceCasePackageInterface ||
           type == sacm::SacmPackageNodeType::ArgumentPackageInterface ||
           type == sacm::SacmPackageNodeType::ArtifactPackageInterface ||
           type == sacm::SacmPackageNodeType::TerminologyPackageInterface;
}

bool IsBindingNode(sacm::SacmPackageNodeType type) {
    return type == sacm::SacmPackageNodeType::AssuranceCasePackageBinding ||
           type == sacm::SacmPackageNodeType::ArgumentPackageBinding ||
           type == sacm::SacmPackageNodeType::ArtifactPackageBinding ||
           type == sacm::SacmPackageNodeType::TerminologyPackageBinding;
}

std::string PackageNodeLabel(const sacm::SacmPackageTreeNode& node) {
    std::string label = sacm::SacmPackageNodeTypeToDisplayString(node.type);
    if (!node.displayName.empty()) {
        label += ": ";
        label += node.displayName;
    }
    return label;
}

void RenderPackageNode(const core::ProjectFileEntry& entry,
                       const sacm::SacmPackageTreeNode& node,
                       const ProjectFilesPanelCallbacks& callbacks);

void RenderPackageGroup(const char* label,
                        const std::vector<const sacm::SacmPackageTreeNode*>& nodes,
                        const core::ProjectFileEntry& entry,
                        const ProjectFilesPanelCallbacks& callbacks) {
    if (nodes.empty())
        return;

    const bool open = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen, "%s", label);
    if (open) {
        for (const auto* node : nodes) {
            RenderPackageNode(entry, *node, callbacks);
        }
        ImGui::TreePop();
    }
}

void RenderPackageChildren(const core::ProjectFileEntry& entry,
                           const sacm::SacmPackageTreeNode& node,
                           const ProjectFilesPanelCallbacks& callbacks) {
    std::vector<const sacm::SacmPackageTreeNode*> argument_packages;
    std::vector<const sacm::SacmPackageTreeNode*> artifact_packages;
    std::vector<const sacm::SacmPackageTreeNode*> terminology_packages;
    std::vector<const sacm::SacmPackageTreeNode*> interfaces;
    std::vector<const sacm::SacmPackageTreeNode*> bindings;
    std::vector<const sacm::SacmPackageTreeNode*> other_packages;

    for (const auto& child : node.children) {
        if (child.type == sacm::SacmPackageNodeType::ArgumentPackage) {
            argument_packages.push_back(&child);
        } else if (child.type == sacm::SacmPackageNodeType::ArtifactPackage) {
            artifact_packages.push_back(&child);
        } else if (child.type == sacm::SacmPackageNodeType::TerminologyPackage) {
            terminology_packages.push_back(&child);
        } else if (IsInterfaceNode(child.type)) {
            interfaces.push_back(&child);
        } else if (IsBindingNode(child.type)) {
            bindings.push_back(&child);
        } else {
            other_packages.push_back(&child);
        }
    }

    RenderPackageGroup("Argument Packages", argument_packages, entry, callbacks);
    RenderPackageGroup("Artifact Packages", artifact_packages, entry, callbacks);
    RenderPackageGroup("Terminology Packages", terminology_packages, entry, callbacks);
    RenderPackageGroup("Interfaces", interfaces, entry, callbacks);
    RenderPackageGroup("Bindings", bindings, entry, callbacks);
    RenderPackageGroup("Other Packages", other_packages, entry, callbacks);
}

void RenderPackageNode(const core::ProjectFileEntry& entry,
                       const sacm::SacmPackageTreeNode& node,
                       const ProjectFilesPanelCallbacks& callbacks) {
    const bool has_children = !node.children.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (!has_children)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    const std::string label = PackageNodeLabel(node);
    const std::string id = node.id.empty() ? node.gid + node.xmlLocalName + label : node.id;
    ImGui::PushID(id.c_str());
    const bool open = ImGui::TreeNodeEx("package", flags, "%s", label.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen() && callbacks.open_package_node) {
        callbacks.open_package_node(entry, node);
    }
    if (has_children && open) {
        RenderPackageChildren(entry, node, callbacks);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void RenderFile(const core::ProjectFileEntry& entry,
                const ProjectFilesPanelModel& model,
                const ProjectFilesPanelCallbacks& callbacks) {
    std::string label = entry.relativePath.filename().generic_string();
    ImGui::PushID(entry.relativePath.generic_string().c_str());
    const auto tree_it = model.sacm_package_trees_by_path.find(entry.relativePath.generic_string());
    const bool has_package_tree = entry.role == core::ProjectFileRole::SacmArgument &&
                                  tree_it != model.sacm_package_trees_by_path.end();
    const bool has_package_children = has_package_tree && tree_it->second.success && !tree_it->second.root.children.empty();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!has_package_children)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    const bool open = ImGui::TreeNodeEx("file", flags, "%s", label.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen() && callbacks.open_file) {
        callbacks.open_file(entry);
    }
    if (entry.state != core::ProjectFileState::Clean) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", core::ProjectFileStateToDisplayString(entry.state));
    } else if (entry.role == core::ProjectFileRole::ReviewProposal) {
        auto found = model.proposal_validity_by_path.find(entry.relativePath.generic_string());
        if (found != model.proposal_validity_by_path.end()) {
            const bool valid = found->second.validity == core::reviews::ProposalValidity::Valid;
            ImGui::SameLine();
            ImGui::PushStyleColor(
                ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(valid ? ui::GetTheme().success : ui::GetTheme().danger));
            ImGui::TextUnformatted(valid ? "(Valid)" : "(Broken)");
            ImGui::PopStyleColor();
        }
    }

    if (has_package_tree && !tree_it->second.success) {
        ImGui::TextDisabled("Package tree unavailable: %s", tree_it->second.error_message.c_str());
    }

    if (has_package_children && open) {
        for (const auto& child : tree_it->second.root.children) {
            RenderPackageNode(entry, child, callbacks);
        }
        ImGui::TreePop();
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

void ShowProjectFilesTree(const ProjectFilesPanelModel& model, const ProjectFilesPanelCallbacks& callbacks) {
    const core::AssuranceProject& project = *model.project;
    ImGui::TextWrapped("%s", project.name.c_str());
    ImGui::TextDisabled("%s", project.rootPath.string().c_str());
    ImGui::Separator();

    for (const auto& folder : kVisibleFolders) {
        bool open = ImGui::TreeNodeEx(folder.label, ImGuiTreeNodeFlags_DefaultOpen, "%s", folder.label);
        RenderFolderContextMenu(folder.path, callbacks);
        if (!open)
            continue;

        bool has_files = false;
        for (const auto& entry : project.files) {
            if (!EntryBelongsToFolder(entry, folder.path))
                continue;
            has_files = true;
            RenderFile(entry, model, callbacks);
        }
        if (!has_files) {
            ImGui::TextDisabled("No files");
        }
        ImGui::TreePop();
    }
}

} // namespace

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
            ShowProjectFilesTree(model, callbacks);
        } else {
            ImGui::TextDisabled("No project open.");
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace ui::panels
