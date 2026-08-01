#include "ui/gsn/gsn_acp_decorator.h"

#include "ui/gsn/gsn_dpi.h"
#include "ui/i18n/localization.h"
#include "ui/theme.h"

#include <algorithm>
#include <cfloat>

namespace ui::gsn {

namespace {

std::string EdgeKey(const std::string& parent_id, const std::string& child_id) {
    return parent_id + "\x1f" + child_id;
}

std::string AcpIncompleteReason(const parser::AcpRecord& acp) {
    if (acp.resolution_kind == "text") {
        if (acp.text.empty())
            return AF_TR("Text confidence argument is empty.");
        if (acp.confidence_claim_id.empty())
            return AF_TR("Native text confidence claim is missing.");
        return {};
    }
    if (acp.resolution_kind == "topGoalReference") {
        if (acp.argument_package_id.empty() || acp.top_goal_id.empty())
            return AF_TR("Confidence argument tree is not linked.");
        return {};
    }
    return AF_TR("No confidence argument has been selected.");
}

bool AcpRecordIsComplete(const parser::AcpRecord& acp) {
    return AcpIncompleteReason(acp).empty();
}

std::string AcpDisplayLabel(const parser::AcpRecord& acp) {
    std::string label = acp.id;
    const std::string name = acp.name.empty() ? acp.id : acp.name;
    if (!name.empty() && name != acp.id) {
        label += ": ";
        label += name;
    }
    return label;
}

const char* AcpModeDescription(const parser::AcpRecord& acp) {
    if (acp.resolution_kind == "text")
        return "Text confidence argument";
    if (acp.resolution_kind == "topGoalReference")
        return "Separate confidence argument tree";
    return "Incomplete";
}

ImU32 AcpFillColor(const parser::AcpRecord& acp) {
    const Theme& theme = GetTheme();
    if (!AcpRecordIsComplete(acp))
        return theme.warning;
    if (acp.resolution_kind == "topGoalReference")
        return theme.accent;
    return theme.success;
}

void DrawAcpAlertBadge(ImDrawList* draw_list, ImVec2 box_min, ImVec2 box_max, float scale) {
    const Theme& theme = GetTheme();
    const float radius = 5.5f * scale;
    const ImVec2 center(box_max.x - radius * 0.15f, box_min.y + radius * 0.15f);
    draw_list->AddCircleFilled(center, radius, theme.warning);
    draw_list->AddCircle(center, radius, theme.canvas_bg, 12, 1.2f * scale);
    ImFont* font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize() * 0.78f * scale;
    const char* mark = "!";
    const ImVec2 mark_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, mark);
    draw_list->AddText(font,
                       font_size,
                       ImVec2(center.x - mark_size.x * 0.5f, center.y - mark_size.y * 0.58f),
                       InkOn(theme.warning),
                       mark);
}

} // namespace

std::unordered_map<std::string, std::vector<parser::AcpRecord>>
BuildRelationshipAcpLookup(const parser::AssuranceCase* active_case) {
    std::unordered_map<std::string, std::vector<parser::AcpRecord>> acps_by_relationship;
    if (!active_case)
        return acps_by_relationship;
    for (const parser::AcpRecord& acp : active_case->acps) {
        if (acp.target_kind == "relationship" && !acp.target_id.empty())
            acps_by_relationship[acp.target_id].push_back(acp);
    }
    return acps_by_relationship;
}

std::unordered_map<std::string, std::vector<parser::AcpRecord>>
BuildElementAcpLookup(const parser::AssuranceCase* active_case) {
    std::unordered_map<std::string, std::vector<parser::AcpRecord>> acps_by_element;
    if (!active_case)
        return acps_by_element;
    for (const parser::AcpRecord& acp : active_case->acps) {
        if (acp.target_kind == "element" && !acp.target_id.empty())
            acps_by_element[acp.target_id].push_back(acp);
    }
    return acps_by_element;
}

std::unordered_map<std::string, const core::acp::AcpRelationshipTarget*>
BuildRelationshipTargetLookup(const std::vector<core::acp::AcpRelationshipTarget>& targets) {
    std::unordered_map<std::string, const core::acp::AcpRelationshipTarget*> target_by_edge;
    for (const core::acp::AcpRelationshipTarget& target : targets) {
        if (!target.parent_id.empty() && !target.child_id.empty())
            target_by_edge[EdgeKey(target.parent_id, target.child_id)] = &target;
    }
    return target_by_edge;
}

void DrawAcpRelationshipDecorator(ImDrawList* draw_list,
                                  ImVec2 center,
                                  float zoom,
                                  const core::acp::AcpRelationshipTarget& target,
                                  const std::vector<parser::AcpRecord>& acps,
                                  const ElementContextActions& actions,
                                  UiState& ui_state) {
    if (acps.empty())
        return;

    const bool selected = std::any_of(
        acps.begin(), acps.end(), [&](const parser::AcpRecord& acp) { return acp.id == ui_state.selected_acp_id; });
    const parser::AcpRecord& display_acp = acps.front();
    const bool complete = AcpRecordIsComplete(display_acp);
    const std::string label = AcpDisplayLabel(display_acp);

    const Theme& theme = GetTheme();
    const float scale = DpiScale() * zoom;
    const ImVec2 half_size(8.0f * scale, 8.0f * scale);
    const float rounding = 2.0f * scale;
    const float hit_pad = 2.0f * scale;
    const ImU32 fill = AcpFillColor(display_acp);
    const ImU32 outline = selected ? theme.accent : theme.border_strong;
    ImFont* font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize() * zoom;
    const ImVec2 text_size =
        zoom >= 0.45f ? font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label.c_str()) : ImVec2(0, 0);

    const ImVec2 box_min(center.x - half_size.x, center.y - half_size.y);
    const ImVec2 box_max(center.x + half_size.x, center.y + half_size.y);
    draw_list->AddRectFilled(box_min, box_max, fill, rounding);
    draw_list->AddRect(box_min, box_max, outline, rounding, 0, selected ? 2.4f * scale : 1.4f * scale);
    if (!complete)
        DrawAcpAlertBadge(draw_list, box_min, box_max, scale);

    if (zoom >= 0.45f && !label.empty()) {
        const ImVec2 text_pos(box_max.x + 6.0f * scale, center.y - text_size.y * 0.5f);
        draw_list->AddText(font, font_size, text_pos, theme.text_primary, label.c_str());
    }

    ImGui::SetCursorScreenPos(ImVec2(box_min.x - hit_pad, box_min.y - hit_pad));
    ImGui::SetNextItemAllowOverlap();
    const std::string widget_id = "ACP##" + target.relationship_id + "##" + target.parent_id + "##" + target.child_id;
    ImGui::InvisibleButton(
        widget_id.c_str(),
        ImVec2((half_size.x + hit_pad) * 2.0f + text_size.x + 8.0f * scale, (half_size.y + hit_pad) * 2.0f));
    if (ImGui::IsItemClicked()) {
        ui_state.selected_acp_id = acps.front().id;
        ui_state.selected_element_id.clear();
        ui_state.selected_relationship_id.clear();
        ui_state.selected_relationship_edge_key.clear();
    }
    if (ImGui::BeginPopupContextItem(widget_id.c_str())) {
        if (!acps.empty()) {
            const std::string acp_id = acps.front().id;
            ui_state.selected_acp_id = acp_id;
            ui_state.selected_element_id.clear();
            ui_state.selected_relationship_id.clear();
            ui_state.selected_relationship_edge_key.clear();
            ImGui::TextUnformatted(acp_id.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem(AF_TR("Remove ACP").c_str(), nullptr, false, static_cast<bool>(actions.remove_acp))) {
                actions.remove_acp(acp_id);
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(
            (acps.size() == 1 ? AF_TR("Assurance Claim Point") : AF_TR("Assurance Claim Points")).c_str());
        ImGui::Separator();
        ImGui::TextUnformatted(ui::i18n::trf("Target: {0}", target.summary).c_str());
        ImGui::TextUnformatted(ui::i18n::trf("SACM relationship: {0}", target.relationship_id).c_str());
        for (const parser::AcpRecord& acp : acps) {
            ImGui::Text("%s", AcpDisplayLabel(acp).c_str());
            ImGui::TextDisabled("%s", AF_TR(AcpModeDescription(acp)).c_str());
            const std::string reason = AcpIncompleteReason(acp);
            if (!reason.empty())
                ImGui::TextDisabled("%s", reason.c_str());
        }
        ImGui::EndTooltip();
    }
}

void DrawAcpElementDecorator(ImDrawList* draw_list,
                             const LayoutNode& node,
                             ImVec2 node_min,
                             ImVec2 node_max,
                             float zoom,
                             const std::vector<parser::AcpRecord>& acps,
                             const ElementContextActions& actions,
                             UiState& ui_state) {
    if (acps.empty())
        return;

    const bool selected = std::any_of(
        acps.begin(), acps.end(), [&](const parser::AcpRecord& acp) { return acp.id == ui_state.selected_acp_id; });
    const parser::AcpRecord& display_acp = acps.front();
    const bool complete = AcpRecordIsComplete(display_acp);
    const std::string label = AcpDisplayLabel(display_acp);

    const Theme& theme = GetTheme();
    const float scale = DpiScale() * zoom;
    const ImVec2 half_size(8.0f * scale, 8.0f * scale);
    const float gap = 4.0f * scale;
    const float rounding = 2.0f * scale;
    const float hit_pad = 2.0f * scale;
    const ImVec2 center((node_min.x + node_max.x) * 0.5f, node_max.y + gap + half_size.y);
    const ImVec2 box_min(center.x - half_size.x, center.y - half_size.y);
    const ImVec2 box_max(center.x + half_size.x, center.y + half_size.y);
    const ImU32 fill = AcpFillColor(display_acp);
    const ImU32 outline = selected ? theme.accent : theme.border_strong;
    ImFont* font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize() * zoom;
    const ImVec2 text_size =
        zoom >= 0.45f ? font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label.c_str()) : ImVec2(0, 0);

    draw_list->AddRectFilled(box_min, box_max, fill, rounding);
    draw_list->AddRect(box_min, box_max, outline, rounding, 0, selected ? 2.4f * scale : 1.4f * scale);
    if (!complete)
        DrawAcpAlertBadge(draw_list, box_min, box_max, scale);

    if (zoom >= 0.45f && !label.empty()) {
        const ImVec2 text_pos(box_max.x + 6.0f * scale, center.y - text_size.y * 0.5f);
        draw_list->AddText(font, font_size, text_pos, theme.text_primary, label.c_str());
    }

    ImGui::SetCursorScreenPos(ImVec2(box_min.x - hit_pad, box_min.y - hit_pad));
    ImGui::SetNextItemAllowOverlap();
    const std::string widget_id = "ACP element##" + node.id;
    ImGui::InvisibleButton(
        widget_id.c_str(),
        ImVec2((half_size.x + hit_pad) * 2.0f + text_size.x + 8.0f * scale, (half_size.y + hit_pad) * 2.0f));
    if (ImGui::IsItemClicked()) {
        ui_state.selected_acp_id = acps.front().id;
        ui_state.selected_element_id.clear();
        ui_state.selected_relationship_id.clear();
        ui_state.selected_relationship_edge_key.clear();
    }
    if (ImGui::BeginPopupContextItem(widget_id.c_str())) {
        const std::string acp_id = acps.front().id;
        ui_state.selected_acp_id = acp_id;
        ui_state.selected_element_id.clear();
        ui_state.selected_relationship_id.clear();
        ui_state.selected_relationship_edge_key.clear();
        ImGui::TextUnformatted(acp_id.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem(AF_TR("Remove ACP").c_str(), nullptr, false, static_cast<bool>(actions.remove_acp))) {
            actions.remove_acp(acp_id);
        }
        ImGui::EndPopup();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(
            (acps.size() == 1 ? AF_TR("Assurance Claim Point") : AF_TR("Assurance Claim Points")).c_str());
        ImGui::Separator();
        ImGui::TextUnformatted(ui::i18n::trf("Target element: {0}", node.id).c_str());
        for (const parser::AcpRecord& acp : acps) {
            ImGui::Text("%s", AcpDisplayLabel(acp).c_str());
            ImGui::TextDisabled("%s", AF_TR(AcpModeDescription(acp)).c_str());
            const std::string reason = AcpIncompleteReason(acp);
            if (!reason.empty())
                ImGui::TextDisabled("%s", reason.c_str());
        }
        ImGui::EndTooltip();
    }
}

bool RenderAcpRelationshipContextMenu(const core::acp::AcpRelationshipTarget* target,
                                      const std::vector<parser::AcpRecord>* acps,
                                      const ElementContextActions& actions,
                                      UiState& ui_state,
                                      const std::string& edge_key,
                                      const std::string& parent_id,
                                      const std::string& child_id,
                                      bool edge_picked) {
    const std::string widget_id =
        target ? "ACP edge##" + target->relationship_id + "##" + target->parent_id + "##" + target->child_id
               : "ACP edge##" + edge_key;
    const std::string popup_id = widget_id + " popup";
    bool consumed_context_click = false;
    if (target && edge_picked && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ui_state.selected_relationship_id = target->relationship_id;
        ui_state.selected_relationship_edge_key = edge_key;
        ui_state.selected_element_id.clear();
        ui_state.selected_acp_id.clear();
    }

    if (edge_picked && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ui_state.selected_relationship_id = target ? target->relationship_id : std::string{};
        ui_state.selected_relationship_edge_key = edge_key;
        ui_state.selected_element_id.clear();
        ui_state.selected_acp_id.clear();
        ImGui::OpenPopup(popup_id.c_str());
        consumed_context_click = true;
    }

    if (ImGui::BeginPopup(popup_id.c_str())) {
        consumed_context_click = true;
        const std::string summary =
            target ? target->summary : ui::i18n::trf("Relationship {0} -> {1}", parent_id, child_id);
        ImGui::TextUnformatted(summary.c_str());
        ImGui::Separator();
        const bool has_existing_acp = acps && !acps->empty();
        if (has_existing_acp) {
            for (const parser::AcpRecord& acp : *acps) {
                const std::string label = ui::i18n::trf("Select {0}", acp.id);
                if (ImGui::MenuItem(label.c_str())) {
                    ui_state.selected_acp_id = acp.id;
                    ui_state.selected_element_id.clear();
                    ui_state.selected_relationship_id.clear();
                    ui_state.selected_relationship_edge_key.clear();
                }
            }
            ImGui::Separator();
        }
        const bool can_create_acp =
            target && target->eligible_for_acp && static_cast<bool>(actions.add_acp_to_relationship);
        const bool can_warn_for_blocked_acp =
            (!target || !target->eligible_for_acp) && static_cast<bool>(actions.set_status);
        if (ImGui::MenuItem(AF_TR("Add ACP").c_str(),
                            nullptr,
                            false,
                            !has_existing_acp && (can_create_acp || can_warn_for_blocked_acp))) {
            if (can_create_acp) {
                actions.add_acp_to_relationship(target->relationship_id);
            } else if (actions.set_status) {
                const std::string blocked_reason = target && !target->blocked_reason.empty()
                                                       ? target->blocked_reason
                                                       : AF_TR("ACP is not supported for this relationship.");
                actions.set_status(ui::i18n::trf("Add ACP failed: {0}", blocked_reason));
            }
        }
        // GSN v3 dialectic: challenge the relationship itself (not its endpoints).
        if (target) {
            ImGui::Separator();
            if (ImGui::MenuItem(AF_TR("Add Counter Argument").c_str(),
                                nullptr,
                                false,
                                static_cast<bool>(actions.add_counter_argument_to_relationship)))
                actions.add_counter_argument_to_relationship(target->relationship_id);
            if (ImGui::MenuItem(AF_TR("Add Counter Evidence").c_str(),
                                nullptr,
                                false,
                                static_cast<bool>(actions.add_counter_evidence_to_relationship)))
                actions.add_counter_evidence_to_relationship(target->relationship_id);
            // Withdrawing the relationship is what corrects a wrongly-connected
            // argument. Without it the tool could report that an edge breaks a
            // GSN rule and leave no way to act on it.
            ImGui::Separator();
            if (ImGui::MenuItem(AF_TR("Remove relationship").c_str(),
                                nullptr,
                                false,
                                static_cast<bool>(actions.remove_relationship)))
                actions.remove_relationship(target->relationship_id);
        }
        ImGui::EndPopup();
    }
    return consumed_context_click;
}

} // namespace ui::gsn
