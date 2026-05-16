#include "ui/panels/confidence_panel.h"

#include "imgui.h"
#include "ui/confidence_model.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/theme.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ui::panels {

namespace {

constexpr float kTrianglePadding = 28.0f;
constexpr float kTriangleMinHeight = 190.0f;
constexpr float kTriangleMaxHeight = 260.0f;

ImVec2 ToImVec2(ConfidencePoint point) {
    return ImVec2(point.x, point.y);
}

ConfidencePoint ToConfidencePoint(ImVec2 point) {
    return {point.x, point.y};
}

void DrawTextCentered(ImDrawList* draw_list, ImVec2 center, const char* text, ImU32 color) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    draw_list->AddText(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f), color, text);
}

bool DrawOpinionSliderBar(const char* label, SubjectiveOpinion& opinion, OpinionComponent component, ImU32 color) {
    const Theme& theme = GetTheme();
    NormalizeOpinion(opinion);

    float value = opinion.belief;
    if (component == OpinionComponent::Disbelief)
        value = opinion.disbelief;
    else if (component == OpinionComponent::Uncertainty)
        value = opinion.uncertainty;
    value = ClampConfidenceValue(value);

    ImGui::PushID(label);
    const float line_height = ImGui::GetTextLineHeight();
    const float available_width = ImGui::GetContentRegionAvail().x;
    const float value_width = ImGui::CalcTextSize("0.00").x;
    const float label_width = ImGui::CalcTextSize(label).x;
    const float bar_height = std::max(8.0f, line_height * 0.52f);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    const bool value_fits_on_label_line = label_width + ImGui::GetStyle().ItemSpacing.x + value_width <= available_width;
    if (value_fits_on_label_line) {
        const float value_x = std::max(label_width + ImGui::GetStyle().ItemSpacing.x, available_width - value_width);
        ImGui::SameLine(value_x);
    }
    ImGui::Text("%.2f", value);

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(24.0f, ImGui::GetContentRegionAvail().x), line_height + 2.0f);
    ImGui::InvisibleButton("##bar", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    bool changed = false;
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float t = (ImGui::GetIO().MousePos.x - cursor.x) / std::max(1.0f, size.x);
        const float next_value = ClampConfidenceValue(t);
        if (std::fabs(next_value - value) > 0.0005f) {
            SetOpinionComponent(opinion, component, next_value);
            changed = true;
        }
    }

    if (hovered || active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float y = cursor.y + (line_height - bar_height) * 0.5f;
    const ImVec2 min(cursor.x, y);
    const ImVec2 max(cursor.x + size.x, y + bar_height);
    const float fill_x = min.x + size.x * value;
    draw_list->AddRectFilled(min, max, WithAlpha(theme.surface_3, hovered || active ? 0.86f : 0.60f), bar_height * 0.5f);
    draw_list->AddRectFilled(min, ImVec2(fill_x, max.y), WithAlpha(color, active ? 1.0f : 0.88f), bar_height * 0.5f);
    draw_list->AddCircleFilled(ImVec2(fill_x, (min.y + max.y) * 0.5f), active ? 6.0f : 4.8f, theme.text_primary, 18);
    draw_list->AddCircleFilled(ImVec2(fill_x, (min.y + max.y) * 0.5f), active ? 4.0f : 3.0f, color, 18);

    if (hovered || active)
        ImGui::SetTooltip("Drag to adjust %s", label);

    ImGui::PopID();
    return changed;
}

bool DrawSegmentButton(const char* label, bool selected, float width) {
    const Theme& theme = GetTheme();
    const ImVec4 selected_color = ImGui::ColorConvertU32ToFloat4(WithAlpha(theme.accent, 0.86f));
    const ImVec4 selected_hover = ImGui::ColorConvertU32ToFloat4(theme.accent_hover);
    const ImVec4 normal_color = ImGui::ColorConvertU32ToFloat4(WithAlpha(theme.surface_3, 0.58f));
    const ImVec4 normal_hover = ImGui::ColorConvertU32ToFloat4(WithAlpha(theme.accent, 0.34f));
    const ImVec4 text_color = ImGui::ColorConvertU32ToFloat4(selected ? theme.text_primary : theme.text_secondary);

    ImGui::PushStyleColor(ImGuiCol_Button, selected ? selected_color : normal_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? selected_hover : normal_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, selected ? selected_hover : selected_color);
    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    const bool clicked = ImGui::Button(label, ImVec2(width, 0.0f));
    ImGui::PopStyleColor(4);
    return clicked;
}

bool DrawModeSelector(ElementConfidence& confidence) {
    bool changed = false;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    const float button_width = std::max(92.0f, (available - gap) * 0.5f);

    if (DrawSegmentButton("Direct value", confidence.mode == ConfidenceInputMode::DirectValue, button_width)) {
        confidence.mode = ConfidenceInputMode::DirectValue;
        changed = true;
    }
    ImGui::SameLine();
    if (DrawSegmentButton("Opinion triangle", confidence.mode == ConfidenceInputMode::OpinionTriangle, button_width)) {
        confidence.mode = ConfidenceInputMode::OpinionTriangle;
        changed = true;
    }
    return changed;
}

void DrawProjectedConfidence(float value) {
    const Theme& theme = GetTheme();
    value = ClampConfidenceValue(value);
    ImGui::Spacing();
    ImGui::TextUnformatted("Projected confidence");

    if (ui::gsn::g_BoldFont)
        ImGui::PushFont(ui::gsn::g_BoldFont);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.accent_hover));
    ImGui::Text("%.2f", value);
    ImGui::PopStyleColor();
    if (ui::gsn::g_BoldFont)
        ImGui::PopFont();

    ImGui::ProgressBar(value, ImVec2(-1.0f, 8.0f), "");
}

bool DrawOpinionTriangle(const char* id, SubjectiveOpinion& opinion, const ImVec2& requested_size) {
    const Theme& theme = GetTheme();
    NormalizeOpinion(opinion);

    ImVec2 size = requested_size;
    size.x = std::max(180.0f, size.x);
    size.y = std::max(kTriangleMinHeight, size.y);

    ImGui::PushID(id);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##triangle", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool pressed_or_dragged = active && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const float rounding = theme.rounding_panel;
    const ImVec2 surface_min = origin;
    const ImVec2 surface_max(origin.x + size.x, origin.y + size.y);
    const float help_radius = 9.0f;
    const ImVec2 help_center(surface_max.x - help_radius - 10.0f, surface_min.y + help_radius + 10.0f);
    const ImVec2 help_min(help_center.x - help_radius - 2.0f, help_center.y - help_radius - 2.0f);
    const ImVec2 help_max(help_center.x + help_radius + 2.0f, help_center.y + help_radius + 2.0f);
    const bool help_hovered = ImGui::IsMouseHoveringRect(help_min, help_max, true);

    draw_list->AddRectFilled(surface_min, surface_max, WithAlpha(theme.surface_2, hovered || active ? 0.96f : 0.78f), rounding);
    draw_list->AddRect(surface_min,
                       surface_max,
                       WithAlpha(hovered || active ? theme.accent : theme.border, hovered || active ? 0.78f : 0.72f),
                       rounding,
                       0,
                       hovered || active ? 1.6f : 1.0f);

    const float inner_width = std::max(80.0f, size.x - kTrianglePadding * 2.0f);
    const float inner_height = std::max(80.0f, size.y - kTrianglePadding * 2.0f - 14.0f);
    const float triangle_height = std::min(inner_height, inner_width * 0.86f);
    const float triangle_width = std::min(inner_width, triangle_height * 1.18f);
    const float left = origin.x + (size.x - triangle_width) * 0.5f;
    const float right = left + triangle_width;
    const float top = origin.y + kTrianglePadding;
    const float bottom = top + triangle_height;

    const ConfidencePoint uncertainty_vertex{origin.x + size.x * 0.5f, top};
    const ConfidencePoint disbelief_vertex{left, bottom};
    const ConfidencePoint belief_vertex{right, bottom};
    const ImVec2 uncertainty = ToImVec2(uncertainty_vertex);
    const ImVec2 disbelief = ToImVec2(disbelief_vertex);
    const ImVec2 belief = ToImVec2(belief_vertex);

    const ImVec2 centroid((uncertainty.x + disbelief.x + belief.x) / 3.0f,
                          (uncertainty.y + disbelief.y + belief.y) / 3.0f);
    draw_list->AddTriangleFilled(uncertainty, disbelief, belief, WithAlpha(theme.info, 0.10f));
    draw_list->AddTriangleFilled(disbelief, centroid, uncertainty, WithAlpha(theme.warning, 0.12f));
    draw_list->AddTriangleFilled(belief, uncertainty, centroid, WithAlpha(theme.success, 0.10f));

    for (int step = 1; step <= 4; ++step) {
        const float t = static_cast<float>(step) / 5.0f;
        const ImU32 grid_color = WithAlpha(theme.border_strong, 0.26f);

        const ConfidencePoint u_left{uncertainty_vertex.x * t + disbelief_vertex.x * (1.0f - t),
                                     uncertainty_vertex.y * t + disbelief_vertex.y * (1.0f - t)};
        const ConfidencePoint u_right{uncertainty_vertex.x * t + belief_vertex.x * (1.0f - t),
                                      uncertainty_vertex.y * t + belief_vertex.y * (1.0f - t)};
        draw_list->AddLine(ToImVec2(u_left), ToImVec2(u_right), grid_color, 1.0f);

        const ConfidencePoint d_top{disbelief_vertex.x * t + uncertainty_vertex.x * (1.0f - t),
                                    disbelief_vertex.y * t + uncertainty_vertex.y * (1.0f - t)};
        const ConfidencePoint d_bottom{disbelief_vertex.x * t + belief_vertex.x * (1.0f - t),
                                       disbelief_vertex.y * t + belief_vertex.y * (1.0f - t)};
        draw_list->AddLine(ToImVec2(d_top), ToImVec2(d_bottom), grid_color, 1.0f);

        const ConfidencePoint b_top{belief_vertex.x * t + uncertainty_vertex.x * (1.0f - t),
                                    belief_vertex.y * t + uncertainty_vertex.y * (1.0f - t)};
        const ConfidencePoint b_bottom{belief_vertex.x * t + disbelief_vertex.x * (1.0f - t),
                                       belief_vertex.y * t + disbelief_vertex.y * (1.0f - t)};
        draw_list->AddLine(ToImVec2(b_top), ToImVec2(b_bottom), grid_color, 1.0f);
    }

    draw_list->AddLine(uncertainty, ImVec2((disbelief.x + belief.x) * 0.5f, bottom), WithAlpha(theme.border_strong, 0.44f), 1.2f);
    draw_list->AddLine(disbelief, ImVec2((uncertainty.x + belief.x) * 0.5f, (uncertainty.y + belief.y) * 0.5f), WithAlpha(theme.border_strong, 0.36f), 1.0f);
    draw_list->AddLine(belief, ImVec2((uncertainty.x + disbelief.x) * 0.5f, (uncertainty.y + disbelief.y) * 0.5f), WithAlpha(theme.border_strong, 0.36f), 1.0f);
    draw_list->AddTriangle(uncertainty, disbelief, belief, WithAlpha(theme.border_strong, 0.95f), 1.8f);

    const bool labels_fit = size.x >= 230.0f;
    DrawTextCentered(draw_list, ImVec2(uncertainty.x, std::max(origin.y + 10.0f, uncertainty.y - 15.0f)), "Uncertainty", theme.text_secondary);
    DrawTextCentered(draw_list, ImVec2(disbelief.x + (labels_fit ? 22.0f : 12.0f), disbelief.y + 16.0f), "Disbelief", theme.text_secondary);
    DrawTextCentered(draw_list, ImVec2(belief.x - (labels_fit ? 16.0f : 8.0f), belief.y + 16.0f), "Belief", theme.text_secondary);

    if (labels_fit) {
        const auto marker_position = [centroid](ImVec2 vertex) {
            const float dx = centroid.x - vertex.x;
            const float dy = centroid.y - vertex.y;
            const float length = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
            constexpr float inset = 18.0f;
            return ImVec2(vertex.x + dx / length * inset, vertex.y + dy / length * inset);
        };
        DrawTextCentered(draw_list, marker_position(uncertainty), "1", WithAlpha(theme.text_muted, 0.80f));
        DrawTextCentered(draw_list, marker_position(disbelief), "1", WithAlpha(theme.text_muted, 0.80f));
        DrawTextCentered(draw_list, marker_position(belief), "1", WithAlpha(theme.text_muted, 0.80f));
        DrawTextCentered(draw_list, centroid, "0.33", WithAlpha(theme.text_muted, 0.70f));
    }

    bool changed = false;
    if (pressed_or_dragged) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        SubjectiveOpinion next = OpinionFromPoint(ToConfidencePoint(mouse),
                                                  uncertainty_vertex,
                                                  disbelief_vertex,
                                                  belief_vertex,
                                                  opinion.baseRate);
        if (std::fabs(next.belief - opinion.belief) > 0.0005f ||
            std::fabs(next.disbelief - opinion.disbelief) > 0.0005f ||
            std::fabs(next.uncertainty - opinion.uncertainty) > 0.0005f) {
            opinion = next;
            changed = true;
        }
    }

    const ConfidencePoint selected_point = OpinionToPoint(opinion, uncertainty_vertex, disbelief_vertex, belief_vertex);
    const ImVec2 marker = ToImVec2(selected_point);
    const float marker_radius = active ? 7.0f : hovered ? 6.5f : 5.8f;
    draw_list->AddCircleFilled(marker, marker_radius + 6.0f, WithAlpha(theme.accent_hover, active ? 0.24f : 0.14f), 32);
    draw_list->AddCircle(marker, marker_radius + 2.0f, WithAlpha(theme.text_primary, 0.88f), 32, 1.2f);
    draw_list->AddCircleFilled(marker, marker_radius, theme.accent_hover, 32);
    draw_list->AddCircleFilled(ImVec2(marker.x - marker_radius * 0.30f, marker.y - marker_radius * 0.34f),
                               marker_radius * 0.35f,
                               WithAlpha(theme.text_primary, 0.58f),
                               16);

    const ImU32 help_fill = help_hovered ? WithAlpha(theme.accent, 0.92f) : WithAlpha(theme.surface_3, 0.90f);
    const ImU32 help_border = help_hovered ? theme.accent_hover : WithAlpha(theme.border_strong, 0.82f);
    draw_list->AddCircleFilled(help_center, help_radius, help_fill, 24);
    draw_list->AddCircle(help_center, help_radius, help_border, 24, 1.2f);
    DrawTextCentered(draw_list, help_center, "?", theme.text_primary);

    if (help_hovered) {
        ImGui::SetTooltip("Jøsang's opinion triangle");
    } else if (hovered || active) {
        ImGui::SetTooltip("Belief %.2f\nDisbelief %.2f\nUncertainty %.2f", opinion.belief, opinion.disbelief, opinion.uncertainty);
    }

    ImGui::PopID();
    return changed;
}

void DrawDirectMode(ElementConfidence& confidence) {
    confidence.directValue = ClampConfidenceValue(confidence.directValue);
    ImGui::TextUnformatted("Confidence");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##direct_confidence", &confidence.directValue, 0.0f, 1.0f, "%.2f"))
        confidence.directValue = ClampConfidenceValue(confidence.directValue);
    DrawProjectedConfidence(confidence.directValue);
}

void DrawOpinionMode(ElementConfidence& confidence) {
    const Theme& theme = GetTheme();
    NormalizeOpinion(confidence.opinion);

    const float available_width = ImGui::GetContentRegionAvail().x;
    const float triangle_height = std::clamp(available_width * 0.78f, kTriangleMinHeight, kTriangleMaxHeight);
    DrawOpinionTriangle("opinion_triangle", confidence.opinion, ImVec2(available_width, triangle_height));

    ImGui::Spacing();
    DrawOpinionSliderBar("Belief", confidence.opinion, OpinionComponent::Belief, theme.success);
    DrawOpinionSliderBar("Disbelief", confidence.opinion, OpinionComponent::Disbelief, theme.warning);
    DrawOpinionSliderBar("Uncertainty", confidence.opinion, OpinionComponent::Uncertainty, theme.info);

    ImGui::Spacing();
    ImGui::Text("Base rate %.2f", confidence.opinion.baseRate);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When uncertainty remains, this is the assumed share treated as confidence in the projected value.");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##base_rate", &confidence.opinion.baseRate, 0.0f, 1.0f, "%.2f"))
        confidence.opinion.baseRate = ClampConfidenceValue(confidence.opinion.baseRate);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Projected confidence = belief + base rate * uncertainty.");

    DrawProjectedConfidence(confidence.opinion.ProjectedConfidence());
}

} // namespace

void ShowConfidencePanel(const std::string& element_id) {
    if (element_id.empty())
        return;

    UiState& state = GetUiState();
    ElementConfidence& confidence = state.confidence_states[element_id];

    const Theme& theme = GetTheme();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ui::gsn::g_BoldFont)
        ImGui::PushFont(ui::gsn::g_BoldFont);
    ImGui::TextUnformatted("Confidence");
    if (ui::gsn::g_BoldFont)
        ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.text_muted));
    ImGui::TextWrapped("Experimental confidence modeling");
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Checkbox("Enable confidence for this element", &confidence.enabled);

    if (!confidence.enabled) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.text_secondary));
        ImGui::TextWrapped("Confidence is not enabled for this element. Enable it to assign direct confidence or define a subjective opinion.");
        ImGui::PopStyleColor();
        return;
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Mode");
    DrawModeSelector(confidence);
    ImGui::Spacing();

    if (confidence.mode == ConfidenceInputMode::DirectValue)
        DrawDirectMode(confidence);
    else
        DrawOpinionMode(confidence);
}

} // namespace ui::panels