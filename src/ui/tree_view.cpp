#include "ui/tree_view.h"

#include "hello_imgui/icons_font_awesome_4.h"
#include "ui/theme.h"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace ui {

static const char* RoleIcon(core::NodeRole role) {
    switch (role) {
    case core::NodeRole::Claim:
        return ICON_FA_BULLSEYE;
    case core::NodeRole::Strategy:
        return ICON_FA_RANDOM;
    case core::NodeRole::Solution:
        return ICON_FA_FILE;
    case core::NodeRole::Context:
        return ICON_FA_DOT_CIRCLE;
    case core::NodeRole::Assumption:
        return ICON_FA_DOT_CIRCLE;
    case core::NodeRole::Justification:
        return ICON_FA_DOT_CIRCLE;
    default:
        return ICON_FA_PROJECT_DIAGRAM;
    }
}

static ImVec4 RoleColor(core::NodeRole role) {
    const Theme& t = GetTheme();
    switch (role) {
    case core::NodeRole::Claim:
        return ImGui::ColorConvertU32ToFloat4(t.node_claim_text);
    case core::NodeRole::Strategy:
        return ImGui::ColorConvertU32ToFloat4(t.node_strategy_text);
    case core::NodeRole::Solution:
        return ImGui::ColorConvertU32ToFloat4(t.node_solution_text);
    case core::NodeRole::Context:
        return ImGui::ColorConvertU32ToFloat4(t.node_context_text);
    case core::NodeRole::Assumption:
        return ImGui::ColorConvertU32ToFloat4(t.node_assumption_text);
    case core::NodeRole::Justification:
        return ImGui::ColorConvertU32ToFloat4(t.node_justification_text);
    default:
        return ImGui::ColorConvertU32ToFloat4(t.accent);
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

static core::TreeDropMode DetectDropMode(const ImVec2& item_min, const ImVec2& item_size) {
    const float mouse_y = ImGui::GetMousePos().y;
    const float relative_y = item_size.y > 0.0f ? (mouse_y - item_min.y) / item_size.y : 0.5f;
    if (relative_y < 0.25f)
        return core::TreeDropMode::Before;
    if (relative_y > 0.75f)
        return core::TreeDropMode::After;
    return core::TreeDropMode::AsChild;
}

static void
DrawDropFeedback(const ImVec2& item_min, const ImVec2& item_max, core::TreeDropMode drop_mode, const ImU32 color) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (drop_mode == core::TreeDropMode::AsChild) {
        draw_list->AddRectFilled(item_min, item_max, WithAlpha(GetTheme().accent, 0.18f));
        draw_list->AddRect(item_min, item_max, color, 0.0f, 0, 1.5f);
        return;
    }

    const float y = drop_mode == core::TreeDropMode::Before ? item_min.y : item_max.y;
    draw_list->AddLine(ImVec2(item_min.x, y), ImVec2(item_max.x, y), color, 2.0f);
}

static std::string PayloadElementId(const ImGuiPayload* payload) {
    if (!payload || !payload->Data || payload->DataSize <= 0)
        return {};
    const char* begin = static_cast<const char*>(payload->Data);
    const char* end = static_cast<const char*>(std::memchr(begin, '\0', static_cast<size_t>(payload->DataSize)));
    if (!end)
        end = begin + payload->DataSize;
    return std::string(begin, end);
}

static void RenderTreeNode(const core::TreeNode* node,
                           const parser::AssuranceCase* active_case,
                           UiState& state,
                           const ElementContextActions& actions,
                           const TreeEditActions* tree_edit_actions) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;

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
    ImVec2 item_min = ImGui::GetItemRectMin();
    ImVec2 item_max = ImGui::GetItemRectMax();
    ImVec2 item_size = ImGui::GetItemRectSize();

    if (tree_edit_actions && tree_edit_actions->enabled()) {
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(core::AF_TREE_NODE_PAYLOAD, node->id.c_str(), node->id.size() + 1);
            ImGui::TextUnformatted(ShortName(node).c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(core::AF_TREE_NODE_PAYLOAD,
                                                                       ImGuiDragDropFlags_AcceptBeforeDelivery |
                                                                           ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            const std::string dragged_id = PayloadElementId(payload);
            if (!dragged_id.empty()) {
                const core::TreeDropMode drop_mode = DetectDropMode(item_min, item_size);
                core::TreeDropValidationResult validation =
                    tree_edit_actions->validate_drop(dragged_id, node->id, drop_mode);
                if (validation.allowed) {
                    DrawDropFeedback(item_min, item_max, drop_mode, ImGui::GetColorU32(ImGuiCol_DragDropTarget));
                    if (payload->IsDelivery()) {
                        tree_edit_actions->perform_drop(dragged_id, node->id, drop_mode);
                    }
                } else if (!validation.reason.empty()) {
                    ImGui::SetTooltip("%s", validation.reason.c_str());
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    bool popup_open = ImGui::BeginPopupContextItem(node->id.c_str());

    // Overlay the colored role icon and node name using AddText (no new ImGui
    // items, so clicks/right-clicks always land on the tree node).
    {
        constexpr float kArrowIconGapTightenPx = 6.0f;
        float text_x = std::max(item_min.x,
                                item_min.x + ImGui::GetTreeNodeToLabelSpacing() - kArrowIconGapTightenPx);
        float text_y = item_min.y + (item_size.y - ImGui::GetTextLineHeight()) * 0.5f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        float font_size = ImGui::GetFontSize();

        const char* role_icon = RoleIcon(node->role);
        ImU32 tag_col = ImGui::ColorConvertFloat4ToU32(RoleColor(node->role));
        dl->AddText(font, font_size, ImVec2(text_x, text_y), tag_col, role_icon);

        float tag_w = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, role_icon).x;
        float space_w = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, " ").x;

        std::string name = ShortName(node);
        ImU32 name_col = ImGui::GetColorU32(ImGuiCol_Text);
        dl->AddText(font, font_size, ImVec2(text_x + tag_w + space_w, text_y), name_col, name.c_str());
    }

    if (clicked) {
        state.selected_element_id = node->id;
        state.center_on_selection = true;
    }

    if (popup_open) {
        state.selected_element_id = node->id;
        RenderAddElementMenu(actions);
        RenderRemoveSubmenu(active_case, state.selected_element_id, actions);
        ImGui::Separator();
        RenderAiReviewMenu(actions);
        ImGui::EndPopup();
    }

    if (has_children && open) {
        // Group1 children (structural)
        for (const auto* child : node->group1_children) {
            RenderTreeNode(child, active_case, state, actions, tree_edit_actions);
        }
        // Group2 attachments (contextual)
        for (const auto* attachment : node->group2_attachments) {
            RenderTreeNode(attachment, active_case, state, actions, tree_edit_actions);
        }
        ImGui::TreePop();
    }
}

void ShowTreeViewPanel(const core::AssuranceTree* tree,
                       const parser::AssuranceCase* active_case,
                       UiState& state,
                       const ElementContextActions& actions,
                       const TreeEditActions* tree_edit_actions) {
    if (!tree || !tree->root) {
        ImGui::TextDisabled("No safety case loaded.");
        return;
    }

    const ImVec2 window_padding = ImGui::GetStyle().WindowPadding;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.0f, window_padding.y));
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 8.0f);
    if (ImGui::BeginChild("TreeViewScroll", ImVec2(0, 0), false)) {
        RenderTreeNode(tree->root, active_case, state, actions, tree_edit_actions);

        // Show orphan nodes if any
        if (!tree->orphans.empty()) {
            ImGui::Separator();
            if (ImGui::TreeNodeEx("##orphans", ImGuiTreeNodeFlags_None, "Orphans (%d)", (int)tree->orphans.size())) {
                for (const auto* orphan : tree->orphans) {
                    RenderTreeNode(orphan, active_case, state, actions, tree_edit_actions);
                }
                ImGui::TreePop();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

} // namespace ui
