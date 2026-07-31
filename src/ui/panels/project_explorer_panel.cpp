#include "ui/panels/project_explorer_panel.h"

#include "ui/i18n/localization.h"
#include "ui/theme.h"

#include "hello_imgui/icons_font_awesome_4.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace ui::panels {
namespace {

constexpr const char* kCaseExplorerTitle = "Case Explorer";
constexpr std::size_t kSearchBufferSize = 256;
constexpr float kSectionContentIndent = 7.0f;

std::array<char, kSearchBufferSize> g_search_buffer{};
std::string g_search_project_id;

struct PackageNodeRenderEntry {
    const sacm::SacmPackageTreeNode* node = nullptr;
    std::size_t child_index = 0;
};

std::string Lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

bool MatchesSearch(std::string_view value) {
    if (g_search_buffer[0] == '\0')
        return true;
    return Lower(value).find(Lower(g_search_buffer.data())) != std::string::npos;
}

bool EntryMatchesSearch(const core::ProjectFileEntry& entry) {
    return MatchesSearch(entry.relativePath.filename().generic_string()) ||
           MatchesSearch(core::ProjectFileRoleToDisplayString(entry.role));
}

std::string CountText(std::size_t count) {
    return std::to_string(count);
}

void DrawTrailingText(std::string_view text, ImU32 color, float right_padding = 0.0f) {
    if (text.empty())
        return;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 item_min = ImGui::GetItemRectMin();
    const ImVec2 item_max = ImGui::GetItemRectMax();
    const ImVec2 size = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    draw_list->AddText(ImVec2(item_max.x - size.x - ImGui::GetStyle().FramePadding.x - right_padding,
                             item_min.y + (item_max.y - item_min.y - size.y) * 0.5f),
                       color,
                       text.data(),
                       text.data() + text.size());
}

bool NavigationRow(const char* id,
                   const char* icon,
                   const std::string& label,
                   bool selected,
                   std::string_view trailing = {},
                   ImU32 trailing_color = 0) {
    ImGui::PushID(id);
    const std::string row_label = std::string(icon) + "  " + label;
    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(ui::WithAlpha(ui::GetTheme().accent, 0.22f)));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                          ImGui::ColorConvertU32ToFloat4(ui::WithAlpha(ui::GetTheme().accent_hover, 0.18f)));
    const bool clicked = ImGui::Selectable(row_label.c_str(), selected, 0, ImVec2(0.0f, 28.0f));
    ImGui::PopStyleColor(2);
    DrawTrailingText(trailing, trailing_color == 0 ? ui::GetTheme().text_muted : trailing_color);
    ImGui::PopID();
    return clicked;
}

bool BeginSection(const char* id,
                  const char* icon,
                  const std::string& label,
                  std::size_t count,
                  bool default_open,
                  const std::function<void()>& add_action = {}) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (default_open)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;

    ImGui::PushID(id);
    const std::string section_label = std::string(icon) + "  " + label;
    const bool open = ImGui::TreeNodeEx("section", flags, "%s", section_label.c_str());
    const ImVec2 child_cursor = ImGui::GetCursorPos();
    const float count_right_padding =
        add_action ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f;
    DrawTrailingText(CountText(count), ui::GetTheme().text_muted, count_right_padding);
    if (add_action) {
        ImGui::SameLine();
        const float button_width = ImGui::GetFrameHeight();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - button_width));
        if (ImGui::SmallButton(ICON_FA_PLUS))
            add_action();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", AF_TR("Add").c_str());
        // SameLine() moves the next-row cursor back to the section header.
        // Restore the cursor TreeNodeEx prepared for its children so opening,
        // closing, and reopening a section cannot flatten its indentation.
        ImGui::SetCursorPos(child_cursor);
    }
    ImGui::PopID();
    if (open)
        ImGui::Indent(kSectionContentIndent);
    return open;
}

void EndSection() {
    ImGui::Unindent(kSectionContentIndent);
    ImGui::TreePop();
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
    if (!node.displayName.empty())
        label = node.displayName;
    if (!node.id.empty() && node.id != label)
        label += "  ·  " + node.id;
    return label;
}

void RenderPackageNode(const core::ProjectFileEntry& entry,
                       const sacm::SacmPackageTreeNode& node,
                       const ProjectExplorerPanelCallbacks& callbacks,
                       const std::string& tree_path);

void RenderPackageGroup(const char* id,
                        const std::string& label,
                        const std::vector<PackageNodeRenderEntry>& nodes,
                        const core::ProjectFileEntry& entry,
                        const ProjectExplorerPanelCallbacks& callbacks,
                        const std::string& parent_path) {
    if (nodes.empty())
        return;

    const bool open = ImGui::TreeNodeEx(id, ImGuiTreeNodeFlags_DefaultOpen, "%s", label.c_str());
    if (open) {
        for (const PackageNodeRenderEntry& entry_ref : nodes) {
            if (entry_ref.node)
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
        const sacm::SacmPackageTreeNode& child = node.children[child_index];
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

    RenderPackageGroup("##grp_argument", AF_TR("Argument Packages"), argument_packages, entry, callbacks, parent_path);
    RenderPackageGroup("##grp_artifact", AF_TR("Artifact Packages"), artifact_packages, entry, callbacks, parent_path);
    RenderPackageGroup("##grp_terminology",
                       AF_TR("Terminology Packages"),
                       terminology_packages,
                       entry,
                       callbacks,
                       parent_path);
    RenderPackageGroup("##grp_interfaces", AF_TR("Interfaces"), interfaces, entry, callbacks, parent_path);
    RenderPackageGroup("##grp_bindings", AF_TR("Bindings"), bindings, entry, callbacks, parent_path);
    RenderPackageGroup("##grp_other", AF_TR("Other Packages"), other_packages, entry, callbacks, parent_path);
}

void RenderPackageNode(const core::ProjectFileEntry& entry,
                       const sacm::SacmPackageTreeNode& node,
                       const ProjectExplorerPanelCallbacks& callbacks,
                       const std::string& tree_path) {
    const bool has_children = !node.children.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!has_children)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    const std::string label = PackageNodeLabel(node);
    const std::string id = entry.relativePath.generic_string() + ":" + tree_path + ":" + node.id + ":" + node.gid;
    ImGui::PushID(id.c_str());
    const bool open = ImGui::TreeNodeEx("package", flags, "%s", label.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen() && callbacks.open_package_node)
        callbacks.open_package_node(entry, node);

    if (node.type == sacm::SacmPackageNodeType::AssuranceCasePackage && callbacks.add_terminology_package) {
        if (ImGui::BeginPopupContextItem("##package_context")) {
            if (ImGui::MenuItem(AF_TR("Add Terminology Package").c_str()))
                callbacks.add_terminology_package(entry, node);
            ImGui::EndPopup();
        }
    }
    const bool removable = node.type == sacm::SacmPackageNodeType::ArgumentPackage ||
                           node.type == sacm::SacmPackageNodeType::ArtifactPackage ||
                           node.type == sacm::SacmPackageNodeType::TerminologyPackage;
    if (removable && callbacks.remove_package && ImGui::BeginPopupContextItem("##package_remove_context")) {
        if (ImGui::MenuItem(AF_TR("Remove").c_str()))
            callbacks.remove_package(entry, node);
        ImGui::EndPopup();
    }
    if (has_children && open) {
        RenderPackageChildren(entry, node, callbacks, tree_path);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

bool IsActiveFile(const ProjectExplorerPanelModel& model, const core::ProjectFileEntry& entry) {
    if (!model.project || model.active_file_path.empty())
        return false;
    return model.active_file_path == model.project->rootPath / entry.relativePath;
}

std::string FriendlyArgumentName(const core::ProjectFileEntry& entry, const ProjectExplorerPanelModel& model) {
    const auto tree = model.sacm_package_trees_by_path.find(entry.relativePath.generic_string());
    if (tree != model.sacm_package_trees_by_path.end() && tree->second.success &&
        !tree->second.root.displayName.empty()) {
        return tree->second.root.displayName;
    }
    return entry.relativePath.stem().generic_string();
}

void RenderSimpleFile(const core::ProjectFileEntry& entry,
                      const ProjectExplorerPanelModel& model,
                      const ProjectExplorerPanelCallbacks& callbacks,
                      const char* icon,
                      const std::string& label) {
    if (!EntryMatchesSearch(entry) && !MatchesSearch(label))
        return;
    ImGui::PushID(entry.relativePath.generic_string().c_str());
    const bool has_warning = entry.state != core::ProjectFileState::Clean;
    if (NavigationRow("file",
                      icon,
                      label,
                      IsActiveFile(model, entry),
                      has_warning ? ICON_FA_EXCLAMATION_TRIANGLE : "",
                      ui::GetTheme().warning) &&
        callbacks.open_file) {
        callbacks.open_file(entry);
    }
    if (has_warning && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", core::ProjectFileStateToDisplayString(entry.state));
    ImGui::PopID();
}

std::vector<const core::ProjectFileEntry*> EntriesWithRole(const core::AssuranceProject& project,
                                                          core::ProjectFileRole role) {
    std::vector<const core::ProjectFileEntry*> entries;
    for (const core::ProjectFileEntry& entry : project.files) {
        if (entry.role == role)
            entries.push_back(&entry);
    }
    return entries;
}

void RenderArguments(const ProjectExplorerPanelModel& model, const ProjectExplorerPanelCallbacks& callbacks) {
    if (!BeginSection("arguments",
                      ICON_FA_BULLSEYE,
                      AF_TR("Arguments"),
                      model.summary.argument_files,
                      true,
                      callbacks.add_sacm_file)) {
        return;
    }
    for (const core::ProjectFileEntry* entry :
         EntriesWithRole(*model.project, core::ProjectFileRole::SacmArgument)) {
        RenderSimpleFile(*entry, model, callbacks, ICON_FA_BULLSEYE, FriendlyArgumentName(*entry, model));
    }
    if (model.summary.argument_files == 0)
        ImGui::TextDisabled("%s", AF_TR("No arguments yet.").c_str());
    EndSection();
}

void RenderEvidence(const ProjectExplorerPanelModel& model, const ProjectExplorerPanelCallbacks& callbacks) {
    const std::size_t evidence_attention = model.summary.unlinked_evidence;
    if (!BeginSection("evidence", ICON_FA_DATABASE, AF_TR("Evidence"), model.summary.evidence, true,
                      callbacks.add_evidence_register)) {
        return;
    }
    const std::string evidence_badge =
        evidence_attention == 0 ? CountText(model.summary.evidence)
                                : ui::i18n::trf("{0} unlinked", evidence_attention);
    if (NavigationRow("evidence_register",
                      ICON_FA_TABLE,
                      AF_TR("Evidence Register"),
                      model.evidence_register_selected,
                      evidence_badge,
                      evidence_attention == 0 ? ui::GetTheme().text_muted : ui::GetTheme().warning) &&
        callbacks.open_evidence_register) {
        callbacks.open_evidence_register();
    }
    if (NavigationRow("cse_register",
                      ICON_FA_LINK,
                      AF_TR("Claim-Evidence Traceability"),
                      model.cse_register_selected,
                      CountText(model.summary.evidence)) &&
        callbacks.open_cse_register) {
        callbacks.open_cse_register();
    }
    EndSection();
}

void RenderReviews(const ProjectExplorerPanelModel& model, const ProjectExplorerPanelCallbacks& callbacks) {
    if (!BeginSection("reviews",
                      ICON_FA_COMMENTS,
                      AF_TR("Reviews"),
                      model.summary.open_review_items,
                      true)) {
        return;
    }
    const ImU32 finding_color =
        model.summary.open_review_items == 0 ? ui::GetTheme().text_muted : ui::GetTheme().warning;
    if (NavigationRow("open_findings",
                      ICON_FA_EXCLAMATION_TRIANGLE,
                      AF_TR("Open Findings"),
                      false,
                      CountText(model.summary.open_review_items),
                      finding_color) &&
        callbacks.open_reviews) {
        callbacks.open_reviews();
    }
    const std::size_t proposal_count = model.summary.valid_proposals + model.summary.broken_proposals;
    const ImU32 proposal_color =
        model.summary.broken_proposals == 0 ? ui::GetTheme().text_muted : ui::GetTheme().danger;
    if (NavigationRow("change_proposals",
                      ICON_FA_FILE_ALT,
                      AF_TR("Change Proposals"),
                      false,
                      model.summary.broken_proposals == 0
                          ? CountText(proposal_count)
                          : ui::i18n::trf("{0} broken", model.summary.broken_proposals),
                      proposal_color) &&
        callbacks.open_reviews) {
        callbacks.open_reviews();
    }
    EndSection();
}

void RenderConformance(const ProjectExplorerPanelModel& model, const ProjectExplorerPanelCallbacks& callbacks) {
    if (!BeginSection("conformance",
                      ICON_FA_CLIPBOARD_CHECK,
                      AF_TR("Conformance"),
                      model.summary.conformance_files,
                      false,
                      callbacks.add_j3377_cae_register)) {
        return;
    }
    bool rendered = false;
    for (const core::ProjectFileEntry& entry : model.project->files) {
        if (entry.role != core::ProjectFileRole::J3377CaeRegister &&
            entry.role != core::ProjectFileRole::ConformanceSheet) {
            continue;
        }
        rendered = true;
        RenderSimpleFile(entry,
                         model,
                         callbacks,
                         ICON_FA_CHECK_SQUARE,
                         entry.relativePath.stem().generic_string());
    }
    if (!rendered && MatchesSearch(AF_TR("Start Conformance Assessment"))) {
        if (NavigationRow("start_conformance",
                          ICON_FA_PLUS_SQUARE,
                          AF_TR("Start Conformance Assessment"),
                          false) &&
            callbacks.add_j3377_cae_register) {
            callbacks.add_j3377_cae_register();
        }
    }
    EndSection();
}

void RenderReports(const ProjectExplorerPanelModel& model, const ProjectExplorerPanelCallbacks& callbacks) {
    if (!BeginSection(
            "reports", ICON_FA_FILE_PDF, AF_TR("Reports"), model.summary.exported_reports, false)) {
        return;
    }
    if (NavigationRow("report_builder", ICON_FA_FILE_ALT, AF_TR("Report Builder"), false) &&
        callbacks.show_not_implemented) {
        callbacks.show_not_implemented("Report Builder");
    }
    for (const core::ProjectFileEntry* entry :
         EntriesWithRole(*model.project, core::ProjectFileRole::ExportedReport)) {
        if (!EntryMatchesSearch(*entry))
            continue;
        ImGui::PushID(entry->relativePath.generic_string().c_str());
        if (NavigationRow("report",
                          ICON_FA_FILE_PDF,
                          entry->relativePath.filename().generic_string(),
                          false) &&
            callbacks.reveal_in_file_explorer) {
            callbacks.reveal_in_file_explorer(*entry);
        }
        ImGui::PopID();
    }
    EndSection();
}

bool ContainsTerminologyPackage(const sacm::SacmPackageTreeNode& node) {
    if (node.type == sacm::SacmPackageNodeType::TerminologyPackage)
        return true;
    return std::any_of(node.children.begin(), node.children.end(), ContainsTerminologyPackage);
}

void RenderTerminologyNodes(const core::ProjectFileEntry& entry,
                            const sacm::SacmPackageTreeNode& node,
                            const ProjectExplorerPanelCallbacks& callbacks,
                            const std::string& path) {
    if (node.type == sacm::SacmPackageNodeType::TerminologyPackage) {
        if (MatchesSearch(PackageNodeLabel(node)))
            RenderPackageNode(entry, node, callbacks, path);
        return;
    }
    for (std::size_t index = 0; index < node.children.size(); ++index)
        RenderTerminologyNodes(entry, node.children[index], callbacks, path + "/" + std::to_string(index));
}

void RenderTerminology(const ProjectExplorerPanelModel& model, const ProjectExplorerPanelCallbacks& callbacks) {
    std::size_t file_count = 0;
    for (const auto& [path, tree] : model.sacm_package_trees_by_path) {
        (void)path;
        if (tree.success && ContainsTerminologyPackage(tree.root))
            ++file_count;
    }
    if (!BeginSection("terminology", ICON_FA_LANGUAGE, AF_TR("Terminology"), file_count, false))
        return;
    for (const core::ProjectFileEntry& entry : model.project->files) {
        const auto tree = model.sacm_package_trees_by_path.find(entry.relativePath.generic_string());
        if (tree != model.sacm_package_trees_by_path.end() && tree->second.success)
            RenderTerminologyNodes(entry, tree->second.root, callbacks, "terminology");
    }
    if (file_count == 0)
        ImGui::TextDisabled("%s", AF_TR("No terminology packages.").c_str());
    EndSection();
}

void RenderAdvancedFile(const core::ProjectFileEntry& entry,
                        const ProjectExplorerPanelModel& model,
                        const ProjectExplorerPanelCallbacks& callbacks) {
    if (!EntryMatchesSearch(entry))
        return;

    const auto tree = model.sacm_package_trees_by_path.find(entry.relativePath.generic_string());
    const bool has_tree = entry.role == core::ProjectFileRole::SacmArgument &&
                          tree != model.sacm_package_trees_by_path.end() && tree->second.success &&
                          !tree->second.root.children.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (!has_tree)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    ImGui::PushID(entry.relativePath.generic_string().c_str());
    const std::string label = entry.relativePath.generic_string();
    const bool open = ImGui::TreeNodeEx("advanced_file", flags, "%s  %s", ICON_FA_FILE_CODE, label.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen() && callbacks.open_file)
        callbacks.open_file(entry);

    if ((entry.role == core::ProjectFileRole::ReviewProposal ||
         entry.role == core::ProjectFileRole::ExportedReport) &&
        ImGui::BeginPopupContextItem("##advanced_file_context")) {
        if (callbacks.remove_file && ImGui::MenuItem(AF_TR("Delete").c_str()))
            callbacks.remove_file(entry);
        if (entry.role == core::ProjectFileRole::ExportedReport && callbacks.reveal_in_file_explorer &&
            ImGui::MenuItem(AF_TR("Open in File Explorer").c_str())) {
            callbacks.reveal_in_file_explorer(entry);
        }
        ImGui::EndPopup();
    }
    if (entry.state != core::ProjectFileState::Clean) {
        DrawTrailingText(ICON_FA_EXCLAMATION_TRIANGLE, ui::GetTheme().warning);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", core::ProjectFileStateToDisplayString(entry.state));
    }

    if (has_tree && open) {
        for (std::size_t index = 0; index < tree->second.root.children.size(); ++index)
            RenderPackageNode(entry, tree->second.root.children[index], callbacks, std::to_string(index));
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void RenderAdvanced(const ProjectExplorerPanelModel& model, const ProjectExplorerPanelCallbacks& callbacks) {
    if (!BeginSection("advanced", ICON_FA_WRENCH, AF_TR("Advanced"), model.project->files.size(), false))
        return;
    for (const core::ProjectFileEntry& entry : model.project->files)
        RenderAdvancedFile(entry, model, callbacks);
    EndSection();
}

void RenderProjectHeader(const ProjectExplorerPanelModel& model) {
    const core::AssuranceProject& project = *model.project;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::GetTheme().surface_2));
    ImGui::BeginChild("##case_header", ImVec2(0.0f, 66.0f), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::GetTheme().accent), "%s", ICON_FA_SHIELD_ALT);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", project.name.c_str());
    ImGui::TextDisabled(
        "%s",
        model.summary.attention_count() == 0
            ? AF_TR("No open project alerts.").c_str()
            : ui::i18n::trnf("{0} item needs attention",
                              "{0} items need attention",
                              static_cast<int>(model.summary.attention_count()),
                              model.summary.attention_count())
                  .c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void ShowCaseExplorer(const ProjectExplorerPanelModel& model, const ProjectExplorerPanelCallbacks& callbacks) {
    const core::AssuranceProject& project = *model.project;
    if (g_search_project_id != project.id) {
        g_search_project_id = project.id;
        g_search_buffer.fill('\0');
    }

    RenderProjectHeader(model);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##case_search", AF_TR("Search project...").c_str(), g_search_buffer.data(),
                             g_search_buffer.size());
    ImGui::Separator();

    if (NavigationRow("overview",
                      ICON_FA_HOME,
                      AF_TR("Overview"),
                      model.overview_selected,
                      CountText(model.summary.elements)) &&
        callbacks.open_overview) {
        callbacks.open_overview();
    }

    RenderArguments(model, callbacks);
    RenderEvidence(model, callbacks);
    RenderReviews(model, callbacks);
    RenderConformance(model, callbacks);
    RenderReports(model, callbacks);
    RenderTerminology(model, callbacks);
    RenderAdvanced(model, callbacks);
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
    ImGui::Begin((AF_TR("Case Explorer") + "###" + kCaseExplorerTitle).c_str(), nullptr, panel_flags);

    if (ImGui::BeginChild("CaseExplorerTree", ImVec2(0.0f, 0.0f), false)) {
        if (model.project) {
            ShowCaseExplorer(model, callbacks);
        } else {
            ImGui::TextDisabled("%s", AF_TR("No project open.").c_str());
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace ui::panels
