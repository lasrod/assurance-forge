#include "ui/gsn/gsn_canvas_renderer.h"

#include "core/acp/acp_relationship_index.h"
#include "core/terminology_scope_service.h"
#include "ui/gsn/gsn_canvas.h" // for DrawGsnNode
#include "ui/gsn/gsn_dpi.h"
#include "ui/gsn/gsn_layout.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <imgui.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui::gsn {

// ===== Edge rendering constants =====
static constexpr float kArrowSize = 9.0f;          // arrowhead triangle side length
static constexpr float kArrowOutlineWidth = 1.5f;  // hollow arrowhead outline thickness
static constexpr float kSolidEdgeWidth = 2.2f;     // Group1 solid line thickness
static constexpr float kDashedEdgeWidth = 1.8f;    // Group2 dashed line thickness
static constexpr float kDashLength = 6.0f;         // dash on-length for dashed lines
static constexpr float kDashGap = 4.0f;            // dash off-length for dashed lines
static constexpr float kStubLength = 12.0f;        // straight segment at each end of a Bezier curve
static constexpr float kVerticalControlPct = 0.4f; // Bezier control point distance (fraction of vertical span)
static constexpr float kScrollPadding = 40.0f;     // extra padding beyond outermost node for scrolling
static constexpr int kBezierSamples = 64;          // arc-length sample count for dashed Bezier rendering
static constexpr float kCullMarginPx = 120.0f;     // screen-space culling margin around viewport

// Edge colors are sourced from the theme on every call so they update if the
// theme is ever swapped at runtime.
static ImU32 Group1EdgeColor() {
    return GetTheme().edge_group1;
}
static ImU32 Group2EdgeColor() {
    return GetTheme().edge_group2;
}

static bool AcpRecordIsInstantiated(const parser::AcpRecord& acp) {
    return acp.resolution_kind == "text" || acp.resolution_kind == "topGoalReference";
}

static std::string EdgeKey(const std::string& parent_id, const std::string& child_id) {
    return parent_id + "\x1f" + child_id;
}

static std::unordered_map<std::string, std::vector<parser::AcpRecord>>
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

static std::unordered_map<std::string, std::vector<parser::AcpRecord>>
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

static std::unordered_map<std::string, const core::acp::AcpRelationshipTarget*>
BuildRelationshipTargetLookup(const std::vector<core::acp::AcpRelationshipTarget>& targets) {
    std::unordered_map<std::string, const core::acp::AcpRelationshipTarget*> target_by_edge;
    for (const core::acp::AcpRelationshipTarget& target : targets) {
        if (!target.parent_id.empty() && !target.child_id.empty())
            target_by_edge[EdgeKey(target.parent_id, target.child_id)] = &target;
    }
    return target_by_edge;
}

static void DrawAcpRelationshipDecorator(ImDrawList* draw_list,
                                         ImVec2 center,
                                         float zoom,
                                         const core::acp::AcpRelationshipTarget& target,
                                         const std::vector<parser::AcpRecord>& acps,
                                         const ElementContextActions& actions,
                                         UiState& ui_state) {
    if (acps.empty())
        return;

    const bool selected = std::any_of(acps.begin(), acps.end(), [&](const parser::AcpRecord& acp) {
        return acp.id == ui_state.selected_acp_id;
    });
    const bool instantiated = std::any_of(acps.begin(), acps.end(), [](const parser::AcpRecord& acp) {
        return AcpRecordIsInstantiated(acp);
    });

    const Theme& theme = GetTheme();
    const float scale = DpiScale() * zoom;
    const ImVec2 half_size(20.0f * scale, 11.0f * scale);
    const float rounding = 2.0f * scale;
    const float hit_pad = 2.0f * scale;
    const ImU32 fill = instantiated ? theme.success : theme.warning;
    const ImU32 outline = selected ? theme.accent : theme.border_strong;

    const ImVec2 box_min(center.x - half_size.x, center.y - half_size.y);
    const ImVec2 box_max(center.x + half_size.x, center.y + half_size.y);
    draw_list->AddRectFilled(box_min, box_max, fill, rounding);
    draw_list->AddRect(box_min, box_max, outline, rounding, 0, selected ? 2.4f * scale : 1.4f * scale);

    if (zoom >= 0.6f && !acps.front().id.empty()) {
        const std::string label = acps.front().id;
        ImFont* font = ImGui::GetFont();
        const float font_size = ImGui::GetFontSize() * zoom;
        const ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label.c_str());
        const ImVec2 text_pos(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f);
        draw_list->AddText(font, font_size, text_pos, InkOn(fill), label.c_str());
    }

    ImGui::SetCursorScreenPos(ImVec2(box_min.x - hit_pad, box_min.y - hit_pad));
    ImGui::SetNextItemAllowOverlap();
    const std::string widget_id = "ACP##" + target.relationship_id + "##" + target.parent_id + "##" + target.child_id;
    ImGui::InvisibleButton(widget_id.c_str(), ImVec2((half_size.x + hit_pad) * 2.0f, (half_size.y + hit_pad) * 2.0f));
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
            if (ImGui::MenuItem("Remove ACP", nullptr, false, static_cast<bool>(actions.remove_acp))) {
                actions.remove_acp(acp_id);
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(acps.size() == 1 ? "Assurance Claim Point" : "Assurance Claim Points");
        ImGui::Separator();
        ImGui::Text("Target: %s", target.summary.c_str());
        ImGui::Text("SACM relationship: %s", target.relationship_id.c_str());
        for (const parser::AcpRecord& acp : acps) {
            ImGui::Text("%s: %s", acp.id.c_str(), AcpRecordIsInstantiated(acp) ? "instantiated" : "uninstantiated");
        }
        ImGui::EndTooltip();
    }
}

static void DrawAcpElementDecorator(ImDrawList* draw_list,
                                    const LayoutNode& node,
                                    ImVec2 node_min,
                                    ImVec2 node_max,
                                    float zoom,
                                    const std::vector<parser::AcpRecord>& acps,
                                    const ElementContextActions& actions,
                                    UiState& ui_state) {
    if (acps.empty())
        return;

    const bool selected = std::any_of(acps.begin(), acps.end(), [&](const parser::AcpRecord& acp) {
        return acp.id == ui_state.selected_acp_id;
    });
    const bool instantiated = std::any_of(acps.begin(), acps.end(), [](const parser::AcpRecord& acp) {
        return AcpRecordIsInstantiated(acp);
    });

    const Theme& theme = GetTheme();
    const float scale = DpiScale() * zoom;
    const ImVec2 half_size(20.0f * scale, 11.0f * scale);
    const float gap = 4.0f * scale;
    const float rounding = 2.0f * scale;
    const float hit_pad = 2.0f * scale;
    const ImVec2 center((node_min.x + node_max.x) * 0.5f, node_max.y + gap + half_size.y);
    const ImVec2 box_min(center.x - half_size.x, center.y - half_size.y);
    const ImVec2 box_max(center.x + half_size.x, center.y + half_size.y);
    const ImU32 fill = instantiated ? theme.success : theme.warning;
    const ImU32 outline = selected ? theme.accent : theme.border_strong;

    draw_list->AddRectFilled(box_min, box_max, fill, rounding);
    draw_list->AddRect(box_min, box_max, outline, rounding, 0, selected ? 2.4f * scale : 1.4f * scale);

    if (zoom >= 0.6f && !acps.front().id.empty()) {
        const std::string label = acps.front().id;
        ImFont* font = ImGui::GetFont();
        const float font_size = ImGui::GetFontSize() * zoom;
        const ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label.c_str());
        const ImVec2 text_pos(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f);
        draw_list->AddText(font, font_size, text_pos, InkOn(fill), label.c_str());
    }

    ImGui::SetCursorScreenPos(ImVec2(box_min.x - hit_pad, box_min.y - hit_pad));
    ImGui::SetNextItemAllowOverlap();
    const std::string widget_id = "ACP element##" + node.id;
    ImGui::InvisibleButton(widget_id.c_str(), ImVec2((half_size.x + hit_pad) * 2.0f, (half_size.y + hit_pad) * 2.0f));
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
        if (ImGui::MenuItem("Remove ACP", nullptr, false, static_cast<bool>(actions.remove_acp))) {
            actions.remove_acp(acp_id);
        }
        ImGui::EndPopup();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(acps.size() == 1 ? "Assurance Claim Point" : "Assurance Claim Points");
        ImGui::Separator();
        ImGui::Text("Target element: %s", node.id.c_str());
        for (const parser::AcpRecord& acp : acps) {
            ImGui::Text("%s: %s", acp.id.c_str(), AcpRecordIsInstantiated(acp) ? "instantiated" : "uninstantiated");
        }
        ImGui::EndTooltip();
    }
}

static bool RenderAcpRelationshipContextMenu(const core::acp::AcpRelationshipTarget* target,
                                             const std::vector<parser::AcpRecord>* acps,
                                             const ElementContextActions& actions,
                                             UiState& ui_state,
                                             const std::string& edge_key,
                                             const std::string& parent_id,
                                             const std::string& child_id,
                                             ImVec2 edge_min,
                                             ImVec2 edge_max) {
    const float min_hit = DpiSize(24.0f);
    const float width = std::max(min_hit, edge_max.x - edge_min.x);
    const float height = std::max(min_hit, edge_max.y - edge_min.y);
    ImGui::SetCursorScreenPos(edge_min);
    ImGui::SetNextItemAllowOverlap();
    const std::string widget_id = target ? "ACP edge##" + target->relationship_id + "##" + target->parent_id + "##" +
                                               target->child_id
                                         : "ACP edge##" + edge_key;
    const std::string popup_id = widget_id + " popup";
    ImGui::InvisibleButton(widget_id.c_str(), ImVec2(width, height));
    bool consumed_context_click = false;
    if (target && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        ui_state.selected_relationship_id = target->relationship_id;
        ui_state.selected_relationship_edge_key = edge_key;
        ui_state.selected_element_id.clear();
        ui_state.selected_acp_id.clear();
    }

    const bool edge_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) ||
                              ImGui::IsMouseHoveringRect(edge_min, edge_max, true);
    if (edge_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ui_state.selected_relationship_id = target ? target->relationship_id : std::string{};
        ui_state.selected_relationship_edge_key = edge_key;
        ui_state.selected_element_id.clear();
        ui_state.selected_acp_id.clear();
        ImGui::OpenPopup(popup_id.c_str());
        consumed_context_click = true;
    }

    if (ImGui::BeginPopup(popup_id.c_str())) {
        consumed_context_click = true;
        const std::string summary = target ? target->summary : "Relationship " + parent_id + " -> " + child_id;
        ImGui::TextUnformatted(summary.c_str());
        ImGui::Separator();
        const bool has_existing_acp = acps && !acps->empty();
        if (has_existing_acp) {
            for (const parser::AcpRecord& acp : *acps) {
                const std::string label = "Select " + acp.id;
                if (ImGui::MenuItem(label.c_str())) {
                    ui_state.selected_acp_id = acp.id;
                    ui_state.selected_element_id.clear();
                    ui_state.selected_relationship_id.clear();
                    ui_state.selected_relationship_edge_key.clear();
                }
            }
            ImGui::Separator();
        }
        const bool can_create_acp = target && target->eligible_for_acp && static_cast<bool>(actions.add_acp_to_relationship);
        const bool can_warn_for_blocked_acp = (!target || !target->eligible_for_acp) && static_cast<bool>(actions.set_status);
        if (ImGui::MenuItem("Add ACP", nullptr, false, !has_existing_acp && (can_create_acp || can_warn_for_blocked_acp))) {
            if (can_create_acp) {
                actions.add_acp_to_relationship(target->relationship_id);
            } else if (actions.set_status) {
                const std::string blocked_reason = target && !target->blocked_reason.empty()
                                                       ? target->blocked_reason
                                                       : "ACP is not supported for this relationship.";
                actions.set_status("Add ACP failed: " + blocked_reason);
            }
        }
        ImGui::EndPopup();
    }
    return consumed_context_click;
}

static bool RectsIntersect(ImVec2 a_min, ImVec2 a_max, ImVec2 b_min, ImVec2 b_max) {
    return a_max.x >= b_min.x && a_min.x <= b_max.x && a_max.y >= b_min.y && a_min.y <= b_max.y;
}

static void ExpandRectToInclude(ImVec2 point, ImVec2& out_min, ImVec2& out_max) {
    out_min.x = std::min(out_min.x, point.x);
    out_min.y = std::min(out_min.y, point.y);
    out_max.x = std::max(out_max.x, point.x);
    out_max.y = std::max(out_max.y, point.y);
}

// ===== Zoom constants =====
static constexpr float kZoomMin = 0.25f; // minimum zoom level (25%)
static constexpr float kZoomMax = 3.0f;  // maximum zoom level (300%)
static constexpr float kZoomStep = 0.1f; // zoom increment per step (10%)

// ===== Arrowhead helpers =====

// Compute the two base corners of an arrowhead triangle given its tip,
// a unit direction vector, and side length.
static void
ComputeArrowBasePoints(ImVec2 tip, float dir_x, float dir_y, float size, ImVec2& out_left, ImVec2& out_right) {
    // Perpendicular to the direction vector
    float perp_x = -dir_y;
    float perp_y = dir_x;
    float half = size * 0.5f;
    out_left = ImVec2(tip.x - dir_x * size + perp_x * half, tip.y - dir_y * size + perp_y * half);
    out_right = ImVec2(tip.x - dir_x * size - perp_x * half, tip.y - dir_y * size - perp_y * half);
}

// Draw a solid (filled) arrowhead at 'tip' pointing in direction (dir_x, dir_y).
static void
DrawSolidArrow(ImDrawList* draw_list, ImVec2 tip, float dir_x, float dir_y, ImU32 color, float size = kArrowSize) {
    float length = sqrtf(dir_x * dir_x + dir_y * dir_y);
    if (length < 1.0f)
        return;
    dir_x /= length;
    dir_y /= length;
    ImVec2 base_left, base_right;
    ComputeArrowBasePoints(tip, dir_x, dir_y, size, base_left, base_right);
    draw_list->AddTriangleFilled(tip, base_left, base_right, color);
}

// Draw a hollow (outline-only) arrowhead at 'tip' pointing in direction (dir_x, dir_y).
static void DrawHollowArrow(ImDrawList* draw_list,
                            ImVec2 tip,
                            float dir_x,
                            float dir_y,
                            ImU32 color,
                            float size = kArrowSize,
                            float thickness = kArrowOutlineWidth) {
    float length = sqrtf(dir_x * dir_x + dir_y * dir_y);
    if (length < 1.0f)
        return;
    dir_x /= length;
    dir_y /= length;
    ImVec2 base_left, base_right;
    ComputeArrowBasePoints(tip, dir_x, dir_y, size, base_left, base_right);
    draw_list->AddTriangle(tip, base_left, base_right, color, thickness);
}

// ===== Bezier curve helpers =====

// Evaluate a cubic Bezier at parameter t âˆˆ [0,1].
static ImVec2 EvalBezier(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t) {
    float u = 1.0f - t;
    float uu = u * u, uuu = uu * u;
    float tt = t * t, ttt = tt * t;
    return ImVec2(uuu * p0.x + 3 * uu * t * p1.x + 3 * u * tt * p2.x + ttt * p3.x,
                  uuu * p0.y + 3 * uu * t * p1.y + 3 * u * tt * p2.y + ttt * p3.y);
}

// Draw a solid cubic Bezier curve.
static void DrawSolidBezier(
    ImDrawList* draw_list, ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, ImU32 color, float thickness = kSolidEdgeWidth) {
    draw_list->AddBezierCubic(p0, p1, p2, p3, color, thickness);
}

// Interpolate a point along a sampled polyline at the given arc-length distance.
static ImVec2
InterpolateAtArcLength(const ImVec2* samples, const float* cumulative_lengths, int sample_count, float target_arc) {
    for (int i = 1; i <= sample_count; ++i) {
        if (cumulative_lengths[i] >= target_arc) {
            float segment_len = cumulative_lengths[i] - cumulative_lengths[i - 1];
            float fraction = (segment_len < 1e-6f) ? 0.0f : (target_arc - cumulative_lengths[i - 1]) / segment_len;
            return ImVec2(samples[i - 1].x + fraction * (samples[i].x - samples[i - 1].x),
                          samples[i - 1].y + fraction * (samples[i].y - samples[i - 1].y));
        }
    }
    return samples[sample_count];
}

// Draw a dashed cubic Bezier curve using arc-length parameterized sampling.
static void DrawDashedBezier(ImDrawList* draw_list,
                             ImVec2 p0,
                             ImVec2 p1,
                             ImVec2 p2,
                             ImVec2 p3,
                             ImU32 color,
                             float thickness = kDashedEdgeWidth,
                             float dash_on = kDashLength,
                             float dash_off = kDashGap) {
    // Sample the curve into a polyline and compute cumulative arc lengths
    ImVec2 samples[kBezierSamples + 1];
    float cumulative_lengths[kBezierSamples + 1];
    samples[0] = p0;
    cumulative_lengths[0] = 0.0f;
    for (int i = 1; i <= kBezierSamples; ++i) {
        float t = (float)i / (float)kBezierSamples;
        samples[i] = EvalBezier(p0, p1, p2, p3, t);
        float dx = samples[i].x - samples[i - 1].x;
        float dy = samples[i].y - samples[i - 1].y;
        cumulative_lengths[i] = cumulative_lengths[i - 1] + sqrtf(dx * dx + dy * dy);
    }
    float total_arc = cumulative_lengths[kBezierSamples];
    if (total_arc < 1.0f)
        return;

    // Walk along the curve alternating between drawing and skipping
    float arc_pos = 0.0f;
    bool is_visible = true;
    while (arc_pos < total_arc) {
        float segment_end = arc_pos + (is_visible ? dash_on : dash_off);
        if (segment_end > total_arc)
            segment_end = total_arc;

        if (is_visible) {
            ImVec2 dash_start = InterpolateAtArcLength(samples, cumulative_lengths, kBezierSamples, arc_pos);
            ImVec2 dash_end = InterpolateAtArcLength(samples, cumulative_lengths, kBezierSamples, segment_end);
            draw_list->AddLine(dash_start, dash_end, color, thickness);
        }
        arc_pos = segment_end;
        is_visible = !is_visible;
    }
}

static void DrawDashedLine(
    ImDrawList* draw_list, ImVec2 start, ImVec2 end, ImU32 color, float thickness, float dash_on, float dash_off) {
    float dx = end.x - start.x;
    float dy = end.y - start.y;
    float length = sqrtf(dx * dx + dy * dy);
    if (length < 1.0f)
        return;

    float dir_x = dx / length;
    float dir_y = dy / length;
    float pos = 0.0f;
    bool is_visible = true;
    while (pos < length) {
        float segment_end = pos + (is_visible ? dash_on : dash_off);
        if (segment_end > length)
            segment_end = length;

        if (is_visible) {
            ImVec2 dash_start(start.x + dir_x * pos, start.y + dir_y * pos);
            ImVec2 dash_end(start.x + dir_x * segment_end, start.y + dir_y * segment_end);
            draw_list->AddLine(dash_start, dash_end, color, thickness);
        }

        pos = segment_end;
        is_visible = !is_visible;
    }
}

GsnCanvas::GsnCanvas() {}

void GsnCanvas::RebuildNodeLookup() {
    node_by_id_.clear();
    node_by_id_.reserve(layout_nodes_.size());
    for (const auto& node : layout_nodes_) {
        node_by_id_[node.id] = &node;
    }
}

void GsnCanvas::SetTree(const core::AssuranceTree& tree) {
    LayoutEngine le;
    layout_nodes_ = le.ComputeLayout(tree);
    RebuildNodeLookup();
}

void GsnCanvas::SetElements(const std::vector<CanvasElement>& elements) {
    elements_ = elements;
    LayoutEngine le;
    layout_nodes_ = le.ComputeLayout(elements_);
    RebuildNodeLookup();
}

void GsnCanvas::ZoomIn() {
    zoom_level_ = std::min(zoom_level_ + kZoomStep, kZoomMax);
}

void GsnCanvas::ZoomOut() {
    zoom_level_ = std::max(zoom_level_ - kZoomStep, kZoomMin);
}

void GsnCanvas::ResetZoom() {
    zoom_level_ = 1.0f;
}

void GsnCanvas::ZoomAtPoint(float new_zoom, ImVec2 focus_content) {
    float old_zoom = zoom_level_;
    zoom_level_ = std::max(kZoomMin, std::min(new_zoom, kZoomMax));
    // Adjust view_offset_ so the content point under the mouse stays fixed on screen.
    // screen_pos = canvas_origin + content * zoom - view_offset
    // We want the same screen_pos before and after:
    //   view_offset_new = view_offset_old + focus * (new_zoom - old_zoom)
    view_offset_.x += focus_content.x * (zoom_level_ - old_zoom);
    view_offset_.y += focus_content.y * (zoom_level_ - old_zoom);
}

void GsnCanvas::Pan(float dx, float dy) {
    view_offset_.x += dx;
    view_offset_.y += dy;
}

void GsnCanvas::GetContentBounds(ImVec2& out_min, ImVec2& out_max) const {
    if (layout_nodes_.empty()) {
        out_min = ImVec2(0, 0);
        out_max = ImVec2(0, 0);
        return;
    }
    float min_x = FLT_MAX, min_y = FLT_MAX;
    float max_x = -FLT_MAX, max_y = -FLT_MAX;
    for (const auto& node : layout_nodes_) {
        if (node.position.x < min_x)
            min_x = node.position.x;
        if (node.position.y < min_y)
            min_y = node.position.y;
        float r = node.position.x + node.size.x;
        float b = node.position.y + node.size.y;
        if (r > max_x)
            max_x = r;
        if (b > max_y)
            max_y = b;
    }
    // Add some padding around the content
    float pad = DpiSize(kScrollPadding);
    out_min = ImVec2(min_x - pad, min_y - pad);
    out_max = ImVec2(max_x + pad, max_y + pad);
}

// ===== Edge drawing helpers =====

// Compute the screen-space connection points for a parentâ†’child edge.
// Group1 edges go from parent's bottom center to child's top center.
static void ComputeGroup1Endpoints(
    const LayoutNode& parent, const LayoutNode& child, ImVec2 origin, float zoom, ImVec2& out_start, ImVec2& out_end) {
    out_start = ImVec2(origin.x + (parent.position.x + parent.size.x * 0.5f) * zoom,
                       origin.y + (parent.position.y + parent.size.y) * zoom);
    out_end = ImVec2(origin.x + (child.position.x + child.size.x * 0.5f) * zoom, origin.y + child.position.y * zoom);
}

// Draw a Group1 (structural) edge: straight stubs â†’ solid Bezier â†’ solid arrowhead.
static void DrawGroup1Edge(ImDrawList* draw_list, ImVec2 parent_bottom, ImVec2 child_top, float zoom) {
    float scale = DpiScale() * zoom;
    float scaled_stub = kStubLength * scale;
    float scaled_edge_width = kSolidEdgeWidth * scale;
    float scaled_arrow = kArrowSize * scale;

    ImVec2 stub_start(parent_bottom.x, parent_bottom.y + scaled_stub);
    ImVec2 stub_end(child_top.x, child_top.y - scaled_stub);

    float vertical_span = fabsf(stub_end.y - stub_start.y);
    ImVec2 ctrl_1(stub_start.x, stub_start.y + vertical_span * kVerticalControlPct);
    ImVec2 ctrl_2(stub_end.x, stub_end.y - vertical_span * kVerticalControlPct);

    ImU32 col = Group1EdgeColor();
    draw_list->AddLine(parent_bottom, stub_start, col, scaled_edge_width);
    DrawSolidBezier(draw_list, stub_start, ctrl_1, ctrl_2, stub_end, col, scaled_edge_width);
    draw_list->AddLine(stub_end, child_top, col, scaled_edge_width);
    DrawSolidArrow(draw_list, child_top, 0.0f, 1.0f, col, scaled_arrow);
}

static void
ComputeGroup1EdgeBounds(ImVec2 parent_bottom, ImVec2 child_top, float zoom, ImVec2& out_min, ImVec2& out_max) {
    float scale = DpiScale() * zoom;
    float scaled_stub = kStubLength * scale;
    float scaled_edge_width = kSolidEdgeWidth * scale;
    float scaled_arrow = kArrowSize * scale;

    ImVec2 stub_start(parent_bottom.x, parent_bottom.y + scaled_stub);
    ImVec2 stub_end(child_top.x, child_top.y - scaled_stub);
    float vertical_span = fabsf(stub_end.y - stub_start.y);
    ImVec2 ctrl_1(stub_start.x, stub_start.y + vertical_span * kVerticalControlPct);
    ImVec2 ctrl_2(stub_end.x, stub_end.y - vertical_span * kVerticalControlPct);

    out_min = parent_bottom;
    out_max = parent_bottom;
    ExpandRectToInclude(child_top, out_min, out_max);
    ExpandRectToInclude(stub_start, out_min, out_max);
    ExpandRectToInclude(stub_end, out_min, out_max);
    ExpandRectToInclude(ctrl_1, out_min, out_max);
    ExpandRectToInclude(ctrl_2, out_min, out_max);

    float pad = std::max(scaled_edge_width, scaled_arrow) + 1.0f;
    out_min.x -= pad;
    out_min.y -= pad;
    out_max.x += pad;
    out_max.y += pad;
}

// Compute screen-space endpoints for a Group2 (side-attached) edge.
// Parent side â†’ attachment nearest edge, depending on which side.
static void ComputeGroup2Endpoints(const LayoutNode& parent,
                                   const LayoutNode& attachment,
                                   ImVec2 origin,
                                   float zoom,
                                   ImVec2& out_parent_side,
                                   ImVec2& out_attachment_edge) {
    if (attachment.is_left_side) {
        out_parent_side =
            ImVec2(origin.x + parent.position.x * zoom, origin.y + (parent.position.y + parent.size.y * 0.5f) * zoom);
        out_attachment_edge = ImVec2(origin.x + (attachment.position.x + attachment.size.x) * zoom,
                                     origin.y + (attachment.position.y + attachment.size.y * 0.5f) * zoom);
    } else {
        out_parent_side = ImVec2(origin.x + (parent.position.x + parent.size.x) * zoom,
                                 origin.y + (parent.position.y + parent.size.y * 0.5f) * zoom);
        out_attachment_edge = ImVec2(origin.x + attachment.position.x * zoom,
                                     origin.y + (attachment.position.y + attachment.size.y * 0.5f) * zoom);
    }
}

// Draw a Group2 (contextual) edge: dashed stubs â†’ dashed Bezier â†’ hollow arrowhead.
static void
DrawGroup2Edge(ImDrawList* draw_list, ImVec2 parent_side, ImVec2 attachment_edge, bool is_left_side, float zoom) {
    // Sign encodes horizontal direction: -1 toward left, +1 toward right
    float horizontal_sign = is_left_side ? -1.0f : 1.0f;
    float scale = DpiScale() * zoom;
    float scaled_stub = kStubLength * scale;
    float scaled_edge_width = kDashedEdgeWidth * scale;
    float scaled_dash = kDashLength * scale;
    float scaled_gap = kDashGap * scale;

    ImVec2 stub_start(parent_side.x + horizontal_sign * scaled_stub, parent_side.y);
    ImVec2 stub_end(attachment_edge.x - horizontal_sign * scaled_stub, attachment_edge.y);

    float horizontal_span = fabsf(stub_end.x - stub_start.x) * 0.5f;
    ImVec2 ctrl_1(stub_start.x + horizontal_sign * horizontal_span, stub_start.y);
    ImVec2 ctrl_2(stub_end.x - horizontal_sign * horizontal_span, stub_end.y);

    ImU32 col = Group2EdgeColor();
    DrawDashedLine(draw_list, parent_side, stub_start, col, scaled_edge_width, scaled_dash, scaled_gap);
    DrawDashedBezier(draw_list, stub_start, ctrl_1, ctrl_2, stub_end, col, scaled_edge_width, scaled_dash, scaled_gap);
    DrawDashedLine(draw_list, stub_end, attachment_edge, col, scaled_edge_width, scaled_dash, scaled_gap);
    // Arrow points into the attachment node
    DrawHollowArrow(
        draw_list, attachment_edge, horizontal_sign, 0.0f, col, kArrowSize * scale, kArrowOutlineWidth * scale);
}

static void ComputeGroup2EdgeBounds(
    ImVec2 parent_side, ImVec2 attachment_edge, bool is_left_side, float zoom, ImVec2& out_min, ImVec2& out_max) {
    float horizontal_sign = is_left_side ? -1.0f : 1.0f;
    float scale = DpiScale() * zoom;
    float scaled_stub = kStubLength * scale;
    float scaled_edge_width = kDashedEdgeWidth * scale;
    float scaled_arrow = kArrowSize * scale;

    ImVec2 stub_start(parent_side.x + horizontal_sign * scaled_stub, parent_side.y);
    ImVec2 stub_end(attachment_edge.x - horizontal_sign * scaled_stub, attachment_edge.y);
    float horizontal_span = fabsf(stub_end.x - stub_start.x) * 0.5f;
    ImVec2 ctrl_1(stub_start.x + horizontal_sign * horizontal_span, stub_start.y);
    ImVec2 ctrl_2(stub_end.x - horizontal_sign * horizontal_span, stub_end.y);

    out_min = parent_side;
    out_max = parent_side;
    ExpandRectToInclude(attachment_edge, out_min, out_max);
    ExpandRectToInclude(stub_start, out_min, out_max);
    ExpandRectToInclude(stub_end, out_min, out_max);
    ExpandRectToInclude(ctrl_1, out_min, out_max);
    ExpandRectToInclude(ctrl_2, out_min, out_max);

    float pad = std::max(scaled_edge_width, scaled_arrow) + 1.0f;
    out_min.x -= pad;
    out_min.y -= pad;
    out_max.x += pad;
    out_max.y += pad;
}

static void DrawGroup1EdgeHighlight(ImDrawList* draw_list, ImVec2 parent_bottom, ImVec2 child_top, float zoom) {
    float scale = DpiScale() * zoom;
    float scaled_stub = kStubLength * scale;
    float thickness = 5.0f * scale;

    ImVec2 stub_start(parent_bottom.x, parent_bottom.y + scaled_stub);
    ImVec2 stub_end(child_top.x, child_top.y - scaled_stub);
    float vertical_span = fabsf(stub_end.y - stub_start.y);
    ImVec2 ctrl_1(stub_start.x, stub_start.y + vertical_span * kVerticalControlPct);
    ImVec2 ctrl_2(stub_end.x, stub_end.y - vertical_span * kVerticalControlPct);

    ImU32 color = WithAlpha(GetTheme().accent, 0.82f);
    draw_list->AddLine(parent_bottom, stub_start, color, thickness);
    draw_list->AddBezierCubic(stub_start, ctrl_1, ctrl_2, stub_end, color, thickness);
    draw_list->AddLine(stub_end, child_top, color, thickness);
}

static void DrawGroup2EdgeHighlight(ImDrawList* draw_list,
                                    ImVec2 parent_side,
                                    ImVec2 attachment_edge,
                                    bool is_left_side,
                                    float zoom) {
    float horizontal_sign = is_left_side ? -1.0f : 1.0f;
    float scale = DpiScale() * zoom;
    float scaled_stub = kStubLength * scale;
    float thickness = 4.5f * scale;

    ImVec2 stub_start(parent_side.x + horizontal_sign * scaled_stub, parent_side.y);
    ImVec2 stub_end(attachment_edge.x - horizontal_sign * scaled_stub, attachment_edge.y);
    float horizontal_span = fabsf(stub_end.x - stub_start.x) * 0.5f;
    ImVec2 ctrl_1(stub_start.x + horizontal_sign * horizontal_span, stub_start.y);
    ImVec2 ctrl_2(stub_end.x - horizontal_sign * horizontal_span, stub_end.y);

    ImU32 color = WithAlpha(GetTheme().accent, 0.78f);
    draw_list->AddLine(parent_side, stub_start, color, thickness);
    draw_list->AddBezierCubic(stub_start, ctrl_1, ctrl_2, stub_end, color, thickness);
    draw_list->AddLine(stub_end, attachment_edge, color, thickness);
}

static bool RelationshipEdgeSelected(const UiState& ui_state,
                                     const core::acp::AcpRelationshipTarget* target,
                                     const std::string& edge_key) {
    return target && ui_state.selected_relationship_id == target->relationship_id &&
           ui_state.selected_relationship_edge_key == edge_key;
}

// ===== Main rendering =====

void GsnCanvas::Render(UiState& ui_state,
                       const parser::AssuranceCase* active_case,
                       const ElementContextActions& actions,
                       const sacm::AssuranceCasePackage* terminology_package) {
    CanvasRenderStats frame_stats{};

    std::optional<core::TerminologyService> terminology_service;
    if (terminology_package) {
        terminology_service.emplace(*terminology_package);
    }
    const core::TerminologyService* terminology_service_ptr =
        terminology_service.has_value() ? &terminology_service.value() : nullptr;
    terminology_card_state_.clicked_term_this_frame = false;
    terminology_card_state_.card_hovered_this_frame = false;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    float zoom = zoom_level_;

    // Apply our own view offset to the drawing origin (replaces ImGui scroll)
    ImVec2 origin(canvas_pos.x - view_offset_.x, canvas_pos.y - view_offset_.y);

    const float cull_margin = DpiSize(kCullMarginPx);
    ImVec2 window_pos = ImGui::GetWindowPos();
    ImVec2 content_min = ImGui::GetWindowContentRegionMin();
    ImVec2 content_max = ImGui::GetWindowContentRegionMax();
    ImVec2 viewport_min(window_pos.x + content_min.x, window_pos.y + content_min.y);
    ImVec2 viewport_max(window_pos.x + content_max.x, window_pos.y + content_max.y);
    ImVec2 cull_min(viewport_min.x - cull_margin, viewport_min.y - cull_margin);
    ImVec2 cull_max(viewport_max.x + cull_margin, viewport_max.y + cull_margin);

    const std::vector<core::acp::AcpRelationshipTarget> acp_targets =
        active_case ? core::acp::BuildAcpRelationshipTargets(*active_case)
                    : std::vector<core::acp::AcpRelationshipTarget>{};
    const auto acp_target_by_edge = BuildRelationshipTargetLookup(acp_targets);
    const auto acp_by_relationship = BuildRelationshipAcpLookup(active_case);
    const auto acp_by_element = BuildElementAcpLookup(active_case);

    // Draw edges first (beneath nodes)
    for (const auto& child_node : layout_nodes_) {
        if (child_node.parent_id.empty())
            continue;

        auto parent_it = node_by_id_.find(child_node.parent_id);
        if (parent_it == node_by_id_.end())
            continue;
        const LayoutNode& parent_node = *parent_it->second;

        if (child_node.group == ElementGroup::Group2) {
            ImVec2 parent_side, attachment_edge;
            ComputeGroup2Endpoints(parent_node, child_node, origin, zoom, parent_side, attachment_edge);
            ImVec2 edge_min, edge_max;
            ComputeGroup2EdgeBounds(parent_side, attachment_edge, child_node.is_left_side, zoom, edge_min, edge_max);
            if (!RectsIntersect(edge_min, edge_max, cull_min, cull_max)) {
                ++frame_stats.edges_culled;
                continue;
            }
            DrawGroup2Edge(draw_list, parent_side, attachment_edge, child_node.is_left_side, zoom);
            const std::string edge_key = EdgeKey(parent_node.id, child_node.id);
            const auto target_it = acp_target_by_edge.find(edge_key);
            const core::acp::AcpRelationshipTarget* acp_target =
                target_it == acp_target_by_edge.end() ? nullptr : target_it->second;
            const std::vector<parser::AcpRecord>* edge_acps = nullptr;
            if (acp_target) {
                const auto acp_it = acp_by_relationship.find(acp_target->relationship_id);
                if (acp_it != acp_by_relationship.end())
                    edge_acps = &acp_it->second;
            }
            if (RelationshipEdgeSelected(ui_state, acp_target, edge_key))
                DrawGroup2EdgeHighlight(draw_list, parent_side, attachment_edge, child_node.is_left_side, zoom);
            frame_stats.relationship_context_menu_active =
                RenderAcpRelationshipContextMenu(acp_target,
                                                 edge_acps,
                                                 actions,
                                                 ui_state,
                                                 edge_key,
                                                 parent_node.id,
                                                 child_node.id,
                                                 edge_min,
                                                 edge_max) ||
                frame_stats.relationship_context_menu_active;
            if (acp_target && acp_target->eligible_for_acp && edge_acps) {
                DrawAcpRelationshipDecorator(draw_list,
                                             ImVec2((parent_side.x + attachment_edge.x) * 0.5f,
                                                    (parent_side.y + attachment_edge.y) * 0.5f),
                                             zoom,
                                             *acp_target,
                                             *edge_acps,
                                             actions,
                                             ui_state);
            }
            ++frame_stats.edges_drawn;
        } else {
            ImVec2 parent_bottom, child_top;
            ComputeGroup1Endpoints(parent_node, child_node, origin, zoom, parent_bottom, child_top);
            ImVec2 edge_min, edge_max;
            ComputeGroup1EdgeBounds(parent_bottom, child_top, zoom, edge_min, edge_max);
            if (!RectsIntersect(edge_min, edge_max, cull_min, cull_max)) {
                ++frame_stats.edges_culled;
                continue;
            }
            DrawGroup1Edge(draw_list, parent_bottom, child_top, zoom);
            const std::string edge_key = EdgeKey(parent_node.id, child_node.id);
            const auto target_it = acp_target_by_edge.find(edge_key);
            const core::acp::AcpRelationshipTarget* acp_target =
                target_it == acp_target_by_edge.end() ? nullptr : target_it->second;
            const std::vector<parser::AcpRecord>* edge_acps = nullptr;
            if (acp_target) {
                const auto acp_it = acp_by_relationship.find(acp_target->relationship_id);
                if (acp_it != acp_by_relationship.end())
                    edge_acps = &acp_it->second;
            }
            if (RelationshipEdgeSelected(ui_state, acp_target, edge_key))
                DrawGroup1EdgeHighlight(draw_list, parent_bottom, child_top, zoom);
            frame_stats.relationship_context_menu_active =
                RenderAcpRelationshipContextMenu(acp_target,
                                                 edge_acps,
                                                 actions,
                                                 ui_state,
                                                 edge_key,
                                                 parent_node.id,
                                                 child_node.id,
                                                 edge_min,
                                                 edge_max) ||
                frame_stats.relationship_context_menu_active;
            if (acp_target && acp_target->eligible_for_acp && edge_acps) {
                DrawAcpRelationshipDecorator(draw_list,
                                             ImVec2((parent_bottom.x + child_top.x) * 0.5f,
                                                    (parent_bottom.y + child_top.y) * 0.5f),
                                             zoom,
                                             *acp_target,
                                             *edge_acps,
                                             actions,
                                             ui_state);
            }
            ++frame_stats.edges_drawn;
        }
    }

    // Draw nodes on top of edges
    for (const auto& node : layout_nodes_) {
        ImVec2 node_min(origin.x + node.position.x * zoom, origin.y + node.position.y * zoom);
        ImVec2 node_max(node_min.x + node.size.x * zoom, node_min.y + node.size.y * zoom);
        if (!RectsIntersect(node_min, node_max, cull_min, cull_max)) {
            ++frame_stats.nodes_culled;
            continue;
        }

        GsnNode gsn_node;
        gsn_node.id = node.id;
        switch (node.role) {
        case ElementRole::Claim:
            gsn_node.type = "Claim";
            break;
        case ElementRole::Strategy:
            gsn_node.type = "Strategy";
            break;
        case ElementRole::Solution:
            gsn_node.type = "Solution";
            break;
        case ElementRole::Context:
            gsn_node.type = "Context";
            break;
        case ElementRole::Assumption:
            gsn_node.type = "Assumption";
            break;
        case ElementRole::Justification:
            gsn_node.type = "Justification";
            break;
        case ElementRole::Evidence:
            gsn_node.type = "Evidence";
            break;
        default:
            gsn_node.type = "Other";
            break;
        }
        gsn_node.position = node.position;
        gsn_node.size = node.size;
        gsn_node.label = node.label;
        gsn_node.label_secondary = node.label_secondary;
        gsn_node.undeveloped = node.undeveloped;
        DrawGsnNode(gsn_node,
                    origin,
                    ui_state,
                    active_case,
                    actions,
                    terminology_service_ptr,
                    terminology_package,
                    &terminology_card_state_,
                    zoom);
        const auto element_acps = acp_by_element.find(node.id);
        if (element_acps != acp_by_element.end()) {
            DrawAcpElementDecorator(draw_list, node, node_min, node_max, zoom, element_acps->second, actions, ui_state);
        }
        ++frame_stats.nodes_drawn;
    }

    RenderPinnedTerminologyCard(terminology_card_state_, terminology_package, actions);
    if (terminology_card_state_.pinned && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !terminology_card_state_.clicked_term_this_frame && !terminology_card_state_.card_hovered_this_frame &&
        !ImGui::IsAnyItemHovered()) {
        terminology_card_state_.pinned = false;
    }

    last_render_stats_ = frame_stats;
}

bool GsnCanvas::CenterOnNode(const std::string& node_id, ImVec2 viewport_size) {
    for (const auto& node : layout_nodes_) {
        if (node.id == node_id) {
            // Center of the node in layout-space (unzoomed)
            float cx = node.position.x + node.size.x * 0.5f;
            float cy = node.position.y + node.size.y * 0.5f;
            // Set view_offset so the node center maps to viewport center
            view_offset_.x = cx * zoom_level_ - viewport_size.x * 0.5f;
            view_offset_.y = cy * zoom_level_ - viewport_size.y * 0.5f;
            return true;
        }
    }
    return false;
}

bool GsnCanvas::CenterOnIds(const std::unordered_set<std::string>& ids, ImVec2 viewport_size) {
    if (ids.empty())
        return false;

    bool found_any = false;
    float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    for (const auto& node : layout_nodes_) {
        if (!ids.count(node.id))
            continue;
        const float nx0 = node.position.x;
        const float ny0 = node.position.y;
        const float nx1 = nx0 + node.size.x;
        const float ny1 = ny0 + node.size.y;
        if (!found_any) {
            min_x = nx0;
            min_y = ny0;
            max_x = nx1;
            max_y = ny1;
            found_any = true;
        } else {
            if (nx0 < min_x)
                min_x = nx0;
            if (ny0 < min_y)
                min_y = ny0;
            if (nx1 > max_x)
                max_x = nx1;
            if (ny1 > max_y)
                max_y = ny1;
        }
    }
    if (!found_any)
        return false;

    // Pad the AABB so the marked nodes don't sit flush with the viewport edge.
    const float padding = DpiSize(80.0f); // layout-space pixels
    const float aabb_w = std::max(1.0f, (max_x - min_x) + padding * 2.0f);
    const float aabb_h = std::max(1.0f, (max_y - min_y) + padding * 2.0f);

    // Compute zoom that fits AABB in viewport, then clamp.
    const float zoom_x = viewport_size.x / aabb_w;
    const float zoom_y = viewport_size.y / aabb_h;
    float new_zoom = std::min(zoom_x, zoom_y);
    // Clamp to a sensible range; do not zoom further IN than 1.0 (no need).
    if (new_zoom > 1.0f)
        new_zoom = 1.0f;
    if (new_zoom < 0.1f)
        new_zoom = 0.1f;
    zoom_level_ = new_zoom;

    const float cx = (min_x + max_x) * 0.5f;
    const float cy = (min_y + max_y) * 0.5f;
    view_offset_.x = cx * zoom_level_ - viewport_size.x * 0.5f;
    view_offset_.y = cy * zoom_level_ - viewport_size.y * 0.5f;
    return true;
}

} // namespace ui::gsn
