#include "ui/tree_view.h"
#include "ui/theme.h"
#include "ui/ui_state.h"
#include "app/app_runtime.h"
#include "core/element_factory.h"
#include "imgui.h"
#include <cstdio>
#include <string>

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

// Render the shared "Remove" submenu used by both the tree and the canvas
// context menus. Two modes are offered, each labeled with the count of
// elements that would be removed (computed via core::PlanRemoval). The
// "descendants" option is disabled when it would not remove anything more
// than the "this node only" option.
void RenderRemoveSubmenu() {
    const std::string& selected_id = GetUiState().selected_element_id;
    if (ImGui::BeginMenu("Remove")) {
        if (selected_id.empty()) {
            ImGui::TextDisabled("No element selected.");
            ImGui::EndMenu();
            return;
        }

        // We need access to the model to compute counts. Without it the menu
        // shows the items in a disabled state.
        const parser::AssuranceCase* ac = app::GetActiveAssuranceCase();
        auto count_for = [&](core::RemoveMode mode) -> int {
            if (!ac) return 0;
            return static_cast<int>(core::PlanRemoval(*ac, selected_id, mode).size());
        };

        const int n_only        = count_for(core::RemoveMode::NodeOnly);
        const int n_descendants = count_for(core::RemoveMode::NodeAndDescendants);

        char label[96];

        std::snprintf(label, sizeof(label), "This node only (%d)", n_only);
        if (ImGui::MenuItem(label, nullptr, false, n_only > 0)) {
            app::RequestRemove(core::RemoveMode::NodeOnly);
        }

        std::snprintf(label, sizeof(label), "Node and descendants (%d)", n_descendants);
        if (ImGui::MenuItem(label, nullptr, false, n_descendants > n_only)) {
            app::RequestRemove(core::RemoveMode::NodeAndDescendants);
        }

        ImGui::EndMenu();
    }
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
    const Theme& t = GetTheme();
    switch (role) {
        case core::NodeRole::Claim:         return ImGui::ColorConvertU32ToFloat4(t.node_claim);
        case core::NodeRole::Strategy:      return ImGui::ColorConvertU32ToFloat4(t.node_strategy);
        case core::NodeRole::Solution:      return ImGui::ColorConvertU32ToFloat4(t.node_solution);
        case core::NodeRole::Context:       return ImGui::ColorConvertU32ToFloat4(t.node_context);
        case core::NodeRole::Assumption:    return ImGui::ColorConvertU32ToFloat4(t.node_assumption);
        case core::NodeRole::Justification: return ImGui::ColorConvertU32ToFloat4(t.node_justification);
        default:                            return ImGui::ColorConvertU32ToFloat4(t.text_secondary);
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

    // Render arrow + selection background only; the visible label is drawn
    // directly onto the draw list so no extra ImGui items are created that
    // could intercept hover / click events on the tree node.
    bool open = ImGui::TreeNodeEx(node->id.c_str(), flags, "%s", "");

    bool clicked = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();

    // Capture item rect before BeginPopupContextItem advances the last item.
    ImVec2 item_min  = ImGui::GetItemRectMin();
    ImVec2 item_size = ImGui::GetItemRectSize();

    bool popup_open = ImGui::BeginPopupContextItem(node->id.c_str());

    // Overlay the colored [Tag] and node name using AddText (no new ImGui
    // items, so clicks/right-clicks always land on the tree node).
    {
        float text_x = item_min.x + ImGui::GetTreeNodeToLabelSpacing();
        float text_y = item_min.y + (item_size.y - ImGui::GetTextLineHeight()) * 0.5f;

        ImDrawList* dl   = ImGui::GetWindowDrawList();
        ImFont*     font = ImGui::GetFont();
        float font_size  = ImGui::GetFontSize();

        const char* tag  = RoleLabel(node->role);
        ImU32 tag_col    = ImGui::ColorConvertFloat4ToU32(RoleColor(node->role));
        dl->AddText(font, font_size, ImVec2(text_x, text_y), tag_col, tag);

        float tag_w   = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, tag).x;
        float space_w = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, " ").x;

        std::string name  = ShortName(node);
        ImU32       name_col = ImGui::GetColorU32(ImGuiCol_Text);
        dl->AddText(font, font_size, ImVec2(text_x + tag_w + space_w, text_y),
                    name_col, name.c_str());
    }

    if (clicked) {
        state.selected_element_id = node->id;
        state.center_on_selection = true;
    }

    if (popup_open) {
        state.selected_element_id = node->id;
        RenderAddElementMenu();
        ImGui::Separator();
        RenderRemoveSubmenu();
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
