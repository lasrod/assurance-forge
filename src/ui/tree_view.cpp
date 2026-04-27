#include "ui/tree_view.h"
#include "ui/theme.h"
#include "imgui.h"
#include <string>

namespace ui {

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

static void RenderTreeNode(const core::TreeNode* node,
                           const parser::AssuranceCase* active_case,
                           UiState& state,
                           const ElementContextActions& actions) {
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
        RenderAddElementMenu(actions);
        ImGui::Separator();
        RenderRemoveSubmenu(active_case, state.selected_element_id, actions);
        ImGui::EndPopup();
    }

    if (has_children && open) {
        // Group1 children (structural)
        for (const auto* child : node->group1_children) {
            RenderTreeNode(child, active_case, state, actions);
        }
        // Group2 attachments (contextual)
        for (const auto* attachment : node->group2_attachments) {
            RenderTreeNode(attachment, active_case, state, actions);
        }
        ImGui::TreePop();
    }
}

void ShowTreeViewPanel(const core::AssuranceTree* tree,
                       const parser::AssuranceCase* active_case,
                       UiState& state,
                       const ElementContextActions& actions) {
    if (!tree || !tree->root) {
        ImGui::TextDisabled("No safety case loaded.");
        return;
    }

    if (ImGui::BeginChild("TreeViewScroll", ImVec2(0, 0), false)) {
        RenderTreeNode(tree->root, active_case, state, actions);

        // Show orphan nodes if any
        if (!tree->orphans.empty()) {
            ImGui::Separator();
            if (ImGui::TreeNodeEx("##orphans", ImGuiTreeNodeFlags_None, "Orphans (%d)", (int)tree->orphans.size())) {
                for (const auto* orphan : tree->orphans) {
                    RenderTreeNode(orphan, active_case, state, actions);
                }
                ImGui::TreePop();
            }
        }
    }
    ImGui::EndChild();
}

}  // namespace ui
