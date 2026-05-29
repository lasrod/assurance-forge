#include "ui/panels/project_explorer_panel.h"

#include "ui/i18n/localization.h"
#include "ui/theme.h"

#include <array>
#include <string_view>
#include <vector>

namespace ui::panels {
namespace {

constexpr const char* kProjectExplorerTitle = "Project Explorer";

struct FolderSpec {
    const char* path;
    const char* label;
};

struct PackageNodeRenderEntry {
    const sacm::SacmPackageTreeNode* node = nullptr;
    std::size_t child_index = 0;
};

constexpr std::array<FolderSpec, 6> kVisibleFolders = {{
    {"arguments", "arguments/"},
    {"analysis", "analysis/"},
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
                       const ProjectExplorerPanelCallbacks& callbacks,
                       const std::string& tree_path);

void RenderPackageGroup(const char* label,
                        const std::vector<PackageNodeRenderEntry>& nodes,
                        const core::ProjectFileEntry& entry,
                        const ProjectExplorerPanelCallbacks& callbacks,
                        const std::string& parent_path) {
    if (nodes.empty())
        return;

    const bool open = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen, "%s", label);
    if (open) {
        for (const PackageNodeRenderEntry& entry_ref : nodes) {
            if (!entry_ref.node)
                continue;
            RenderPackageNode(
                entry, *entry_ref.node, callbacks, parent_path + "/" + std::to_string(entry_ref.child_index));
        }
        ImGui::TreePop();
    }
}

void RenderPackageChildren(const core::ProjectFileEntry& entry,
                           const sacm::SacmPackageTreeNode& node,
                           const ProjectExplorerPanelCallbacks& callbacks,
                           const std::string& parent_path) {
    std::vector<PackageNodeRenderEntry> argument_packages;
    std::vector<PackageNodeRenderEntry> artifact_packages;
    std::vector<PackageNodeRenderEntry> terminology_packages;
    std::vector<PackageNodeRenderEntry> interfaces;
    std::vector<PackageNodeRenderEntry> bindings;
    std::vector<PackageNodeRenderEntry> other_packages;

    for (std::size_t child_index = 0; child_index < node.children.size(); ++child_index) {
        const auto& child = node.children[child_index];
        PackageNodeRenderEntry child_entry{&child, child_index};
        if (child.type == sacm::SacmPackageNodeType::ArgumentPackage) {
            argument_packages.push_back(child_entry);
        } else if (child.type == sacm::SacmPackageNodeType::ArtifactPackage) {
            artifact_packages.push_back(child_entry);
        } else if (child.type == sacm::SacmPackageNodeType::TerminologyPackage) {
            terminology_packages.push_back(child_entry);
        } else if (IsInterfaceNode(child.type)) {
            interfaces.push_back(child_entry);
        } else if (IsBindingNode(child.type)) {
            bindings.push_back(child_entry);
        } else {
            other_packages.push_back(child_entry);
        }
    }

    RenderPackageGroup(AF_TR("Argument Packages").c_str(), argument_packages, entry, callbacks, parent_path);
    RenderPackageGroup(AF_TR("Artifact Packages").c_str(), artifact_packages, entry, callbacks, parent_path);
    RenderPackageGroup(AF_TR("Terminology Packages").c_str(), terminology_packages, entry, callbacks, parent_path);
    RenderPackageGroup(AF_TR("Interfaces").c_str(), interfaces, entry, callbacks, parent_path);
    RenderPackageGroup(AF_TR("Bindings").c_str(), bindings, entry, callbacks, parent_path);
    RenderPackageGroup(AF_TR("Other Packages").c_str(), other_packages, entry, callbacks, parent_path);
}

void RenderPackageNode(const core::ProjectFileEntry& entry,
                       const sacm::SacmPackageTreeNode& node,
                       const ProjectExplorerPanelCallbacks& callbacks,
                       const std::string& tree_path) {
    const bool has_children = !node.children.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (!has_children)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    const std::string label = PackageNodeLabel(node);
    const std::string id = entry.relativePath.generic_string() + ":" + tree_path + ":" + node.id + ":" + node.gid +
                           ":" + node.xmlLocalName + ":" + label;
    ImGui::PushID(id.c_str());
    const bool open = ImGui::TreeNodeEx("package", flags, "%s", label.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen() && callbacks.open_package_node) {
        callbacks.open_package_node(entry, node);
    }
    if (node.type == sacm::SacmPackageNodeType::AssuranceCasePackage && callbacks.add_terminology_package) {
        if (ImGui::BeginPopupContextItem("##package_context")) {
            if (ImGui::MenuItem(AF_TR("Add Terminology Package").c_str())) {
                callbacks.add_terminology_package(entry, node);
            }
            ImGui::EndPopup();
        }
    }
    const bool is_removable_package = node.type == sacm::SacmPackageNodeType::ArgumentPackage ||
                                      node.type == sacm::SacmPackageNodeType::ArtifactPackage ||
                                      node.type == sacm::SacmPackageNodeType::TerminologyPackage;
    if (is_removable_package && callbacks.remove_package) {
        if (ImGui::BeginPopupContextItem("##package_remove_context")) {
            if (ImGui::MenuItem(AF_TR("Remove").c_str())) {
                callbacks.remove_package(entry, node);
            }
            ImGui::EndPopup();
        }
    }
    if (has_children && open) {
        RenderPackageChildren(entry, node, callbacks, tree_path);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void RenderFile(const core::ProjectFileEntry& entry,
                const ProjectExplorerPanelModel& model,
                const ProjectExplorerPanelCallbacks& callbacks) {
    std::string label = entry.relativePath.filename().generic_string();
    ImGui::PushID(entry.relativePath.generic_string().c_str());
    const auto tree_it = model.sacm_package_trees_by_path.find(entry.relativePath.generic_string());
    const bool has_package_tree =
        entry.role == core::ProjectFileRole::SacmArgument && tree_it != model.sacm_package_trees_by_path.end();
    const bool has_package_children =
        has_package_tree && tree_it->second.success && !tree_it->second.root.children.empty();

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!has_package_children)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    const bool open = ImGui::TreeNodeEx("file", flags, "%s", label.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen() && callbacks.open_file) {
        callbacks.open_file(entry);
    }

    // Trigger the file context popup against the tree node itself, BEFORE any SameLine() text
    // (state tag, validity tag, etc.) is appended. BeginPopupContextItem uses the previous item
    // for hit testing, so attaching it after the SameLine text would mean right-clicking the
    // file row's label has no effect (the small trailing tag would be the only hit zone).
    const bool is_proposal = entry.role == core::ProjectFileRole::ReviewProposal;
    const bool is_export = entry.role == core::ProjectFileRole::ExportedReport;
    const bool wants_file_context_menu = (is_proposal && callbacks.remove_file) ||
                                         (is_export && (callbacks.remove_file || callbacks.reveal_in_file_explorer));
    if (wants_file_context_menu) {
        ImGui::OpenPopupOnItemClick("##file_context", ImGuiPopupFlags_MouseButtonRight);
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
            ImGui::TextUnformatted((valid ? AF_TR("(Valid)") : AF_TR("(Broken)")).c_str());
            ImGui::PopStyleColor();
        }
    }

    if (has_package_tree && !tree_it->second.success) {
        ImGui::TextDisabled(AF_TR("Package tree unavailable: %s").c_str(), tree_it->second.error_message.c_str());
    }

    if (wants_file_context_menu) {
        if (ImGui::BeginPopup("##file_context")) {
            // Use "Delete" for filesystem-backed exports so the destructive nature of the
            // operation is unambiguous. Proposals remain "Remove" to match the existing UX.
            const std::string remove_label = is_export ? AF_TR("Delete") : AF_TR("Remove");
            if (callbacks.remove_file && (is_proposal || is_export) && ImGui::MenuItem(remove_label.c_str())) {
                callbacks.remove_file(entry);
            }
            if (is_export && callbacks.reveal_in_file_explorer &&
                ImGui::MenuItem(AF_TR("Open in File Explorer").c_str())) {
                callbacks.reveal_in_file_explorer(entry);
            }
            ImGui::EndPopup();
        }
    }

    if (has_package_children && open) {
        for (std::size_t child_index = 0; child_index < tree_it->second.root.children.size(); ++child_index) {
            const auto& child = tree_it->second.root.children[child_index];
            RenderPackageNode(entry, child, callbacks, std::to_string(child_index));
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void RenderFolderContextMenu(std::string_view folder, const ProjectExplorerPanelCallbacks& callbacks) {
    if (folder == "arguments") {
        if (ImGui::BeginPopupContextItem("##arguments_context")) {
            if (ImGui::MenuItem(AF_TR("Add New GSN / SACM File").c_str()) && callbacks.add_sacm_file) {
                callbacks.add_sacm_file();
            }
            ImGui::EndPopup();
        }
        return;
    }

    if (folder == "registers") {
        if (ImGui::BeginPopupContextItem("##registers_context")) {
            if (ImGui::MenuItem(AF_TR("Add Evidence Register").c_str()) && callbacks.add_evidence_register) {
                callbacks.add_evidence_register();
            }
            if (ImGui::MenuItem(AF_TR("Add J3377 CAE Register").c_str()) && callbacks.add_j3377_cae_register) {
                callbacks.add_j3377_cae_register();
            }
            ImGui::EndPopup();
        }
    }
}

void ShowProjectExplorerTree(const ProjectExplorerPanelModel& model, const ProjectExplorerPanelCallbacks& callbacks) {
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
            ImGui::TextDisabled("%s", AF_TR("No files").c_str());
        }
        ImGui::TreePop();
    }
}

} // namespace

void ShowProjectExplorerPanel(float width,
                              float height,
                              float top_y,
                              ImGuiWindowFlags panel_flags,
                              ProjectExplorerPanelModel model,
                              const ProjectExplorerPanelCallbacks& callbacks) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, top_y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin(AF_TR(kProjectExplorerTitle).c_str(), nullptr, panel_flags);

    if (ImGui::BeginChild("ProjectExplorerTree", ImVec2(0, 0), false)) {
        if (model.project) {
            ShowProjectExplorerTree(model, callbacks);
        } else {
            ImGui::TextDisabled("%s", AF_TR("No project open.").c_str());
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace ui::panels
