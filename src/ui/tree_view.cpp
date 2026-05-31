#include "ui/tree_view.h"

#include "hello_imgui/icons_font_awesome_4.h"
#include "ui/i18n/localization.h"
#include "ui/theme.h"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

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
// Picks the secondary-language label when the canvas language toggle is active and
// a translation exists, so the navigator stays in lockstep with the GSN canvas.
static std::string ShortName(const core::TreeNode* node, const UiState& state) {
    const bool use_secondary = state.show_secondary_language && !node->label_secondary.empty();
    const std::string& label = use_secondary ? node->label_secondary : node->label;
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

// Renders one tree row. Returns true when the node is open and has children, meaning the caller
// should render its children and then emit a matching ImGui::TreePop.
static bool RenderSingleTreeNode(const core::TreeNode* node,
                                 const parser::AssuranceCase* active_case,
                                 UiState& state,
                                 const ElementContextActions& actions,
                                 const TreeEditActions* tree_edit_actions) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_AllowOverlap |
                               ImGuiTreeNodeFlags_DefaultOpen;

    bool has_children = !node->group1_children.empty() || !node->group2_attachments.empty();
    if (!has_children)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    if (state.selected_element_id == node->id)
        flags |= ImGuiTreeNodeFlags_Selected;

    constexpr float kArrowIconGapTightenPx = 6.0f;
    const float label_x = ImGui::GetCursorScreenPos().x +
                          std::max(0.0f, ImGui::GetTreeNodeToLabelSpacing() - kArrowIconGapTightenPx);

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
            ImGui::TextUnformatted(ShortName(node, state).c_str());
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

    // Capture the tree window's draw list BEFORE BeginPopupContextItem. When the right-click popup
    // opens, ImGui pushes the popup window as the current window and GetWindowDrawList() would
    // return the popup's draw list, causing the row's overlay icon and label to be drawn into the
    // popup instead of the tree row.
    ImDrawList* tree_dl = ImGui::GetWindowDrawList();

    bool popup_open = ImGui::BeginPopupContextItem(node->id.c_str());

    // Overlay the colored role icon and node name using AddText (no new ImGui
    // items, so clicks/right-clicks always land on the tree node).
    {
        float text_y = item_min.y + (item_size.y - ImGui::GetTextLineHeight()) * 0.5f;

        ImDrawList* dl = tree_dl;
        ImFont* font = ImGui::GetFont();
        float font_size = ImGui::GetFontSize();

        const char* role_icon = RoleIcon(node->role);
        ImU32 tag_col = ImGui::ColorConvertFloat4ToU32(RoleColor(node->role));
        dl->AddText(font, font_size, ImVec2(label_x, text_y), tag_col, role_icon);

        float tag_w = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, role_icon).x;
        float space_w = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, " ").x;

        std::string name = ShortName(node, state);
        ImU32 name_col = ImGui::GetColorU32(ImGuiCol_Text);
        dl->AddText(font, font_size, ImVec2(label_x + tag_w + space_w, text_y), name_col, name.c_str());
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

    // Column 1: per-element problem badge sourced from the ProblemsManager
    // (single source of truth, populated into ui_state.element_badge_summaries
    // every frame). The badge lives in its own table column so it never
    // overlaps the node label.
    ImGui::TableSetColumnIndex(1);
    {
        auto it = state.element_badge_summaries.find(node->id);
        if (it != state.element_badge_summaries.end()) {
            const core::ElementBadgeSummary& summary = it->second;
            const Theme& theme = GetTheme();
            ImU32 color = theme.accent;
            const char* glyph = "i";
            switch (summary.highest_severity) {
            case core::ProblemSeverity::Error:
                color = theme.danger;
                glyph = "!";
                break;
            case core::ProblemSeverity::Warning:
                color = theme.warning;
                glyph = "!";
                break;
            case core::ProblemSeverity::Info:
            default:
                color = theme.accent;
                glyph = "i";
                break;
            }
            const float radius = ImGui::GetFontSize() * 0.45f;
            const ImVec2 button_size(radius * 2.0f + 2.0f, radius * 2.0f + 2.0f);
            const ImVec2 button_pos = ImGui::GetCursorScreenPos();
            ImGui::PushID(node->id.c_str());
            const bool badge_clicked =
                ImGui::InvisibleButton("##tree_badge", button_size, ImGuiButtonFlags_AllowOverlap);
            ImGui::PopID();
            const bool badge_hovered = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 center(button_pos.x + button_size.x * 0.5f, button_pos.y + button_size.y * 0.5f);
            dl->AddCircleFilled(center, radius, color);
            ImFont* font = ImGui::GetFont();
            const float gf = ImGui::GetFontSize() * 0.8f;
            const ImVec2 ts = font->CalcTextSizeA(gf, FLT_MAX, 0.0f, glyph);
            dl->AddText(font, gf, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), IM_COL32_WHITE, glyph);
            if (badge_hovered) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                const int problem_count = static_cast<int>(summary.problem_count);
                const std::string tooltip =
                    ui::i18n::trnf("{0} problem · top: {1}\nClick to open the Problems panel.",
                                   "{0} problems · top: {1}\nClick to open the Problems panel.",
                                   problem_count,
                                   problem_count,
                                   summary.top_problem_message);
                ImGui::SetTooltip("%s", tooltip.c_str());
            }
            if (badge_clicked) {
                FocusProblemInPanel(state, summary.top_problem_id, node->id);
            }
        }
    }

    // The caller renders children (then calls TreePop) when the node is open and has children.
    return has_children && open;
}

// Iterative depth-first walk of the tree. A recursive renderer would overflow the call stack on
// very deep argument chains (the tree is expanded by default), silently terminating the app. An
// explicit work stack is used instead; a null entry is a sentinel that emits the matching
// ImGui::TreePop once a subtree's children have all been rendered.
static void RenderTreeNode(const core::TreeNode* root,
                           const parser::AssuranceCase* active_case,
                           UiState& state,
                           const ElementContextActions& actions,
                           const TreeEditActions* tree_edit_actions) {
    std::vector<const core::TreeNode*> stack;
    stack.push_back(root);

    while (!stack.empty()) {
        const core::TreeNode* node = stack.back();
        stack.pop_back();

        if (node == nullptr) {
            ImGui::TreePop();
            continue;
        }

        if (!RenderSingleTreeNode(node, active_case, state, actions, tree_edit_actions))
            continue;

        // Render group1 children (structural) then group2 attachments (contextual), then TreePop.
        // Push in reverse so they render in the original order; the null sentinel runs last.
        stack.push_back(nullptr);
        for (auto it = node->group2_attachments.rbegin(); it != node->group2_attachments.rend(); ++it)
            stack.push_back(*it);
        for (auto it = node->group1_children.rbegin(); it != node->group1_children.rend(); ++it)
            stack.push_back(*it);
    }
}

void ShowTreeViewPanel(const core::AssuranceTree* tree,
                       const parser::AssuranceCase* active_case,
                       UiState& state,
                       const ElementContextActions& actions,
                       const TreeEditActions* tree_edit_actions) {
    if (!tree || !tree->root) {
        ImGui::TextDisabled("%s", AF_TR("No safety case loaded.").c_str());
        return;
    }

    const ImVec2 window_padding = ImGui::GetStyle().WindowPadding;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.0f, window_padding.y));
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 8.0f);
    if (ImGui::BeginChild("TreeViewScroll", ImVec2(0, 0), false)) {
        // Two-column table: stretchy tree column + fixed-width badge column on
        // the right so the alert badge doesn't overlap the node label.
        const float badge_col_width = ImGui::GetFontSize() * 1.4f;
        constexpr ImGuiTableFlags kTreeTableFlags =
            ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("##tree_view_table", 2, kTreeTableFlags)) {
            ImGui::TableSetupColumn("##tree", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##badge", ImGuiTableColumnFlags_WidthFixed, badge_col_width);

            RenderTreeNode(tree->root, active_case, state, actions, tree_edit_actions);

            // Show orphan nodes if any
            if (!tree->orphans.empty()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Separator();
                if (ImGui::TreeNodeEx("##orphans", ImGuiTreeNodeFlags_SpanAllColumns, "%s",
                                      ui::i18n::trf("Orphans ({0})", (int)tree->orphans.size()).c_str())) {
                    for (const auto* orphan : tree->orphans) {
                        RenderTreeNode(orphan, active_case, state, actions, tree_edit_actions);
                    }
                    ImGui::TreePop();
                }
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

} // namespace ui
