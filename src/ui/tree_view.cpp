#include "ui/tree_view.h"
#include "ui/ui_state.h"
#include "app/app_runtime.h"
#include "imgui.h"

namespace ui {

// Render the shared "Add" submenu used by both the tree and the canvas
// context menus. Selecting an item dispatches via app::RequestAddChild,
// which targets the currently selected element.
void RenderAddElementMenu() {
    if (ImGui::BeginMenu("Add")) {
        if (ImGui::MenuItem("Goal"))          app::RequestAddChild(core::NewElementKind::Goal);
        if (ImGui::MenuItem("Strategy"))      app::RequestAddChild(core::NewElementKind::Strategy);
        if (ImGui::MenuItem("Solution"))      app::RequestAddChild(core::NewElementKind::Solution);
        if (ImGui::MenuItem("Context"))       app::RequestAddChild(core::NewElementKind::Context);
        if (ImGui::MenuItem("Assumption"))    app::RequestAddChild(core::NewElementKind::Assumption);
        if (ImGui::MenuItem("Justification")) app::RequestAddChild(core::NewElementKind::Justification);
        ImGui::Separator();
        if (ImGui::MenuItem("Challenge"))     app::RequestNotImplemented("Challenge");
        ImGui::EndMenu();
    }
}

// Render the shared "Remove" item used by both the tree and the canvas
// context menus. Dispatches via app::RequestRemoveSelected, which targets the
// currently selected element and prompts for confirmation if it has children.
void RenderRemoveElementMenuItem() {
    if (ImGui::MenuItem("Remove")) app::RequestRemoveSelected();
}

static const char* RoleLabel(core::NodeRole role) {
    switch (role) {
        case core::NodeRole::Claim:         return "[Claim]";
        case core::NodeRole::Strategy:      return "[Strategy]";
        case core::NodeRole::Solution:      return "[Solution]";
        case core::NodeRole::Context:       return "[Context]";
        case core::NodeRole::Assumption:    return "[Assumption]";
        case core::NodeRole::Justification: return "[Justification]";
        default:                            return "[Other]";
    }
}

static ImVec4 RoleColor(core::NodeRole role) {
    switch (role) {
        case core::NodeRole::Claim:         return ImVec4(0.4f, 0.8f, 0.4f, 1.0f);
        case core::NodeRole::Strategy:      return ImVec4(0.4f, 0.6f, 0.9f, 1.0f);
        case core::NodeRole::Solution:      return ImVec4(0.9f, 0.7f, 0.3f, 1.0f);
        case core::NodeRole::Context:       return ImVec4(0.78f, 0.78f, 0.78f, 1.0f);
        case core::NodeRole::Assumption:    return ImVec4(0.94f, 0.63f, 0.63f, 1.0f);
        case core::NodeRole::Justification: return ImVec4(0.7f, 0.78f, 0.94f, 1.0f);
        default:                            return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

// Extract the short display name from a TreeNode label (text before the first newline).
static std::string ShortName(const core::TreeNode* node) {
    const std::string& label = node->label;
    auto pos = label.find('\n');
    if (pos != std::string::npos)
        return label.substr(0, pos);
    return label;
}

static void RenderTreeNode(const core::TreeNode* node) {
    UiState& state = GetUiState();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_OpenOnDoubleClick
                             | ImGuiTreeNodeFlags_SpanAvailWidth
                             | ImGuiTreeNodeFlags_DefaultOpen;

    bool has_children = !node->group1_children.empty() || !node->group2_attachments.empty();
    if (!has_children)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    if (state.selected_element_id == node->id)
        flags |= ImGuiTreeNodeFlags_Selected;

    // Color the role tag
    ImVec4 color = RoleColor(node->role);
    std::string display = std::string(RoleLabel(node->role)) + " " + ShortName(node);

    ImGui::PushStyleColor(ImGuiCol_Text, color);
    bool open = ImGui::TreeNodeEx(node->id.c_str(), flags, "%s", display.c_str());
    ImGui::PopStyleColor();

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        state.selected_element_id = node->id;
        state.center_on_selection = true;
    }

    // Right-click context menu: select the node, then offer the Add submenu.
    if (ImGui::BeginPopupContextItem(node->id.c_str())) {
        state.selected_element_id = node->id;
        RenderAddElementMenu();
        ImGui::Separator();
        RenderRemoveElementMenuItem();
        ImGui::EndPopup();
    }

    if (has_children && open) {
        // Group1 children (structural)
        for (const auto* child : node->group1_children) {
            RenderTreeNode(child);
        }
        // Group2 attachments (contextual)
        for (const auto* attachment : node->group2_attachments) {
            RenderTreeNode(attachment);
        }
        ImGui::TreePop();
    }
}

void ShowTreeViewPanel(const core::AssuranceTree* tree) {
    if (!tree || !tree->root) {
        ImGui::TextDisabled("No safety case loaded.");
        return;
    }

    if (ImGui::BeginChild("TreeViewScroll", ImVec2(0, 0), false)) {
        RenderTreeNode(tree->root);

        // Show orphan nodes if any
        if (!tree->orphans.empty()) {
            ImGui::Separator();
            if (ImGui::TreeNodeEx("##orphans", ImGuiTreeNodeFlags_None, "Orphans (%d)", (int)tree->orphans.size())) {
                for (const auto* orphan : tree->orphans) {
                    RenderTreeNode(orphan);
                }
                ImGui::TreePop();
            }
        }
    }
    ImGui::EndChild();
}

}  // namespace ui
