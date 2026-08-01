#include "ui/panels/project_explorer_panel.h"

#include "ui/fonts.h"
#include "ui/i18n/localization.h"
#include "ui/theme.h"
#include "ui/widgets/text_ellipsis.h"

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

// X at which the trailing badge starts. Derived from the *measured* item rect
// rather than predicted from style metrics, so the label budget below can never
// drift out of step with where the badge actually lands.
float TrailingTextLeft(std::string_view text, float right_padding) {
    const float right = ImGui::GetItemRectMax().x - ImGui::GetStyle().FramePadding.x - right_padding;
    if (text.empty())
        return right;
    return right - ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
}

void DrawTrailingText(std::string_view text, ImU32 color, float right_padding = 0.0f) {
    if (text.empty())
        return;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 item_min = ImGui::GetItemRectMin();
    const ImVec2 item_max = ImGui::GetItemRectMax();
    const ImVec2 size = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    draw_list->AddText(
        ImVec2(TrailingTextLeft(text, right_padding), item_min.y + (item_max.y - item_min.y - size.y) * 0.5f),
        color,
        text.data(),
        text.data() + text.size());
}

// Draws "<icon>  <label>" over a row whose widget was rendered with an empty
// label, shortening the label so it stops before `limit_x`. Drawing it here
// rather than passing it to the widget means the budget is computed against the
// row's real rect, and it adds no ImGui item that could steal the row's clicks.
// Returns true when the label was shortened.
bool DrawRowLabel(const char* icon, const std::string& label, float label_x, float limit_x) {
    const ImVec2 item_min = ImGui::GetItemRectMin();
    const ImVec2 item_max = ImGui::GetItemRectMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    const float text_y = item_min.y + (item_max.y - item_min.y - ImGui::GetTextLineHeight()) * 0.5f;

    float x = label_x;
    if (icon != nullptr && icon[0] != '\0') {
        draw_list->AddText(ImVec2(x, text_y), color, icon);
        x += ImGui::CalcTextSize(icon).x + ImGui::CalcTextSize("  ").x;
    }
    return ui::widgets::AddTextEllipsized(draw_list, ImVec2(x, text_y), color, label, limit_x - x);
}

bool NavigationRow(const char* id,
                   const char* icon,
                   const std::string& label,
                   bool selected,
                   std::string_view trailing = {},
                   ImU32 trailing_color = 0) {
    ImGui::PushID(id);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float label_x = ImGui::GetCursorScreenPos().x + style.FramePadding.x;
    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(ui::WithAlpha(ui::GetTheme().accent, 0.22f)));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                          ImGui::ColorConvertU32ToFloat4(ui::WithAlpha(ui::GetTheme().accent_hover, 0.18f)));
    const bool clicked = ImGui::Selectable("##row", selected, 0, ImVec2(0.0f, 28.0f));
    ImGui::PopStyleColor(2);

    const float limit_x = TrailingTextLeft(trailing, 0.0f) - (trailing.empty() ? 0.0f : style.ItemSpacing.x);
    const bool truncated = DrawRowLabel(icon, label, label_x, limit_x);
    ui::widgets::TooltipWhenTruncated(truncated, label);
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
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (default_open)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;

    ImGui::PushID(id);
    const float label_x = ImGui::GetCursorScreenPos().x + ImGui::GetTreeNodeToLabelSpacing();
    const bool open = ImGui::TreeNodeEx("section", flags, "%s", "");
    const ImVec2 child_cursor = ImGui::GetCursorPos();
    // Geometric rather than IsItemHovered(): the add button is drawn over this
    // row, so once the pointer reaches it ImGui reports the button as hovered
    // and the row as not — which would make the button flicker out from under
    // the cursor that is trying to click it.
    const bool row_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                             ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    // The count and the add button share one right-hand slot — the count reads
    // as status, the button replaces it on hover. Sharing keeps the label's
    // budget constant, so the label cannot re-truncate as the pointer moves
    // onto the row, and it hands ~a button's width back to the label.
    const std::string count_text = CountText(count);
    const float slot_width = std::max(ImGui::CalcTextSize(count_text.c_str()).x, ImGui::GetFrameHeight());
    const float limit_x =
        ImGui::GetItemRectMax().x - ImGui::GetStyle().FramePadding.x - slot_width - ImGui::GetStyle().ItemSpacing.x;
    ui::widgets::TooltipWhenTruncated(DrawRowLabel(icon, label, label_x, limit_x), label);

    const bool show_add_button = static_cast<bool>(add_action) && row_hovered;
    if (show_add_button) {
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
    } else {
        DrawTrailingText(count_text, ui::GetTheme().text_muted);
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
    RenderPackageGroup(
        "##grp_terminology", AF_TR("Terminology Packages"), terminology_packages, entry, callbacks, parent_path);
    RenderPackageGroup("##grp_interfaces", AF_TR("Interfaces"), interfaces, entry, callbacks, parent_path);
    RenderPackageGroup("##grp_bindings", AF_TR("Bindings"), bindings, entry, callbacks, parent_path);
    RenderPackageGroup("##grp_other", AF_TR("Other Packages"), other_packages, entry, callbacks, parent_path);
}

void RenderPackageNode(const core::ProjectFileEntry& entry,
                       const sacm::SacmPackageTreeNode& node,
                       const ProjectExplorerPanelCallbacks& callbacks,
                       const std::string& tree_path) {
    const bool has_children = !node.children.empty();
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
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
    for (const core::ProjectFileEntry* entry : EntriesWithRole(*model.project, core::ProjectFileRole::SacmArgument)) {
        RenderSimpleFile(*entry, model, callbacks, ICON_FA_BULLSEYE, FriendlyArgumentName(*entry, model));
    }
    if (model.summary.argument_files == 0)
        ImGui::TextDisabled("%s", AF_TR("No arguments yet.").c_str());
    EndSection();
}

void RenderEvidence(const ProjectExplorerPanelModel& model, const ProjectExplorerPanelCallbacks& callbacks) {
    const std::size_t evidence_attention = model.summary.unlinked_evidence;
    if (!BeginSection("evidence",
                      ICON_FA_DATABASE,
                      AF_TR("Evidence"),
                      model.summary.evidence,
                      true,
                      callbacks.add_evidence_register)) {
        return;
    }
    const std::string evidence_badge =
        evidence_attention == 0 ? CountText(model.summary.evidence) : ui::i18n::trf("{0} unlinked", evidence_attention);
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
    if (!BeginSection("reviews", ICON_FA_COMMENTS, AF_TR("Reviews"), model.summary.open_review_items, true)) {
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
                      model.summary.broken_proposals == 0 ? CountText(proposal_count)
                                                          : ui::i18n::trf("{0} broken", model.summary.broken_proposals),
                      proposal_color) &&
        callbacks.open_reviews) {
        callbacks.open_reviews();
    }
    EndSection();
}

void RenderConformance(const ProjectExplorerPanelModel& model, const ProjectExplorerPanelCallbacks& callbacks) {
    if (!BeginSection("conformance",
                      ICON_FA_TASKS,
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
        RenderSimpleFile(entry, model, callbacks, ICON_FA_CHECK_SQUARE, entry.relativePath.stem().generic_string());
    }
    if (!rendered && MatchesSearch(AF_TR("Start Conformance Assessment"))) {
        if (NavigationRow("start_conformance", ICON_FA_PLUS_SQUARE, AF_TR("Start Conformance Assessment"), false) &&
            callbacks.add_j3377_cae_register) {
            callbacks.add_j3377_cae_register();
        }
    }
    EndSection();
}

void RenderReports(const ProjectExplorerPanelModel& model, const ProjectExplorerPanelCallbacks& callbacks) {
    if (!BeginSection("reports", ICON_FA_FILE_PDF, AF_TR("Reports"), model.summary.exported_reports, false)) {
        return;
    }
    if (NavigationRow("report_builder", ICON_FA_FILE_ALT, AF_TR("Report Builder"), false) &&
        callbacks.show_not_implemented) {
        callbacks.show_not_implemented("Report Builder");
    }
    for (const core::ProjectFileEntry* entry : EntriesWithRole(*model.project, core::ProjectFileRole::ExportedReport)) {
        if (!EntryMatchesSearch(*entry))
            continue;
        ImGui::PushID(entry->relativePath.generic_string().c_str());
        if (NavigationRow("report", ICON_FA_FILE_PDF, entry->relativePath.filename().generic_string(), false) &&
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
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (!has_tree)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    ImGui::PushID(entry.relativePath.generic_string().c_str());
    const std::string label = entry.relativePath.generic_string();
    const bool open = ImGui::TreeNodeEx("advanced_file", flags, "%s  %s", ICON_FA_FILE_CODE, label.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen() && callbacks.open_file)
        callbacks.open_file(entry);

    if ((entry.role == core::ProjectFileRole::ReviewProposal || entry.role == core::ProjectFileRole::ExportedReport) &&
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
    // ICON_FA_CERTIFICATE, not ICON_FA_SHIELD_ALT: the latter is a FontAwesome 5
    // name that the bundled 4.x font has no glyph for, so it rendered as the
    // missing-glyph box rather than an icon.
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::GetTheme().accent), "%s", ICON_FA_CERTIFICATE);
    ImGui::SameLine();
    {
        ui::fonts::Scoped strong(ui::fonts::Role::BodyStrong);
        ImGui::TextWrapped("%s", project.name.c_str());
    }

    // Attention needs to look different from "all clear" — both were TextDisabled,
    // so an alert count read as quietly as its absence.
    // Braced so the font is popped before EndChild — an ImGui child must be
    // closed with the font stack at the depth it was opened with.
    {
        const std::size_t attention = model.summary.attention_count();
        ui::fonts::Scoped caption(ui::fonts::Role::Caption);
        if (attention == 0) {
            ImGui::TextDisabled("%s", AF_TR("No open project alerts.").c_str());
        } else {
            ImGui::TextColored(
                ui::GetAttentionColor(),
                "%s",
                ui::i18n::trnf(
                    "{0} item needs attention", "{0} items need attention", static_cast<int>(attention), attention)
                    .c_str());
        }
    }
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
    ImGui::InputTextWithHint(
        "##case_search", AF_TR("Search project...").c_str(), g_search_buffer.data(), g_search_buffer.size());
    ImGui::Separator();

    if (NavigationRow(
            "overview", ICON_FA_HOME, AF_TR("Overview"), model.overview_selected, CountText(model.summary.elements)) &&
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
    ui::fonts::Push(ui::fonts::Role::Title);
    ImGui::Begin((AF_TR("Case Explorer") + "###" + kCaseExplorerTitle).c_str(), nullptr, panel_flags);
    ui::fonts::Pop();

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
