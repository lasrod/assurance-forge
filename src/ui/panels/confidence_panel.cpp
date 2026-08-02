#include "ui/panels/confidence_panel.h"

#include "ui/confidence_model.h"
#include "ui/fonts.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/i18n/localization.h"
#include "ui/theme.h"

#include "hello_imgui/icons_font_awesome_4.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace ui::panels {

namespace {

constexpr float kTrianglePadding = 28.0f;
constexpr float kTriangleMinHeight = 190.0f;
constexpr float kTriangleMaxHeight = 260.0f;

void ConfidenceFieldLabel(std::string_view label) {
    const Theme& theme = GetTheme();
    fonts::Scoped caption(fonts::Role::Caption);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.text_secondary));
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::PopStyleColor();
}

void ConfidenceMetadataRow(std::string_view label, std::string_view value) {
    ConfidenceFieldLabel(label);
    ImGui::SameLine(0.0f, 7.0f);
    fonts::Scoped strong(fonts::Role::BodyStrong);
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
}

void ConfidenceSectionHeader() {
    const Theme& theme = GetTheme();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.accent), "%s", ICON_FA_CHECK_CIRCLE);
    ImGui::SameLine(0.0f, 7.0f);
    {
        fonts::Scoped strong(fonts::Role::BodyStrong);
        ImGui::TextUnformatted(AF_TR("Confidence").c_str());
    }
    ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::ColorConvertU32ToFloat4(WithAlpha(theme.border, 0.72f)));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
}

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

void DrawOpinionSliderBar(const char* label, SubjectiveOpinion& opinion, OpinionComponent component, ImU32 color) {
    const Theme& theme = GetTheme();
    NormalizeOpinion(opinion);

    float value = opinion.belief;
    if (component == OpinionComponent::Disbelief)
        value = opinion.disbelief;
    else if (component == OpinionComponent::Uncertainty)
        value = opinion.uncertainty;
    value = ClampConfidenceValue(value);

    ImGui::PushID(label);
    const std::string display_label = AF_TR(label);
    const float line_height = ImGui::GetTextLineHeight();
    const float available_width = ImGui::GetContentRegionAvail().x;
    const float value_width = ImGui::CalcTextSize("0.00").x;
    const float label_width = ImGui::CalcTextSize(display_label.c_str()).x;
    const float bar_height = std::max(8.0f, line_height * 0.52f);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(display_label.c_str());

    const bool value_fits_on_label_line =
        label_width + ImGui::GetStyle().ItemSpacing.x + value_width <= available_width;
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

    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float t = (ImGui::GetIO().MousePos.x - cursor.x) / std::max(1.0f, size.x);
        const float next_value = ClampConfidenceValue(t);
        SetOpinionComponent(opinion, component, next_value);
    }

    if (hovered || active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float y = cursor.y + (line_height - bar_height) * 0.5f;
    const ImVec2 min(cursor.x, y);
    const ImVec2 max(cursor.x + size.x, y + bar_height);
    const float fill_x = min.x + size.x * value;
    draw_list->AddRectFilled(
        min, max, WithAlpha(theme.surface_3, hovered || active ? 0.86f : 0.60f), bar_height * 0.5f);
    draw_list->AddRectFilled(min, ImVec2(fill_x, max.y), WithAlpha(color, active ? 1.0f : 0.88f), bar_height * 0.5f);
    draw_list->AddCircleFilled(ImVec2(fill_x, (min.y + max.y) * 0.5f), active ? 6.0f : 4.8f, theme.text_primary, 18);
    draw_list->AddCircleFilled(ImVec2(fill_x, (min.y + max.y) * 0.5f), active ? 4.0f : 3.0f, color, 18);

    if (hovered || active)
        ImGui::SetTooltip("%s", ui::i18n::trf("Drag to adjust {0}", display_label).c_str());

    ImGui::PopID();
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

void DrawModeSelector(ElementConfidence& confidence) {
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    const float button_width = std::max(92.0f, (available - gap) * 0.5f);

    if (DrawSegmentButton(
            AF_TR("Direct value").c_str(), confidence.mode == ConfidenceInputMode::DirectValue, button_width)) {
        confidence.mode = ConfidenceInputMode::DirectValue;
    }
    ImGui::SameLine();
    if (DrawSegmentButton(
            AF_TR("Opinion triangle").c_str(), confidence.mode == ConfidenceInputMode::OpinionTriangle, button_width)) {
        confidence.mode = ConfidenceInputMode::OpinionTriangle;
    }
}

bool NearlyEqual(float lhs, float rhs) {
    return std::fabs(lhs - rhs) <= 0.0001f;
}

bool ConfidenceChanged(const ElementConfidence& lhs, const ElementConfidence& rhs) {
    return lhs.enabled != rhs.enabled || lhs.mode != rhs.mode || !NearlyEqual(lhs.direct_value, rhs.direct_value) ||
           !NearlyEqual(lhs.opinion.belief, rhs.opinion.belief) ||
           !NearlyEqual(lhs.opinion.disbelief, rhs.opinion.disbelief) ||
           !NearlyEqual(lhs.opinion.uncertainty, rhs.opinion.uncertainty) ||
           !NearlyEqual(lhs.opinion.base_rate, rhs.opinion.base_rate);
}

const char* MethodLabel(ConfidenceInputMode mode) {
    return mode == ConfidenceInputMode::DirectValue ? "Fixed value" : "Jøsang opinion";
}

void DrawProjectedConfidence(float value) {
    const Theme& theme = GetTheme();
    value = ClampConfidenceValue(value);
    ImGui::Spacing();
    ImGui::TextUnformatted(AF_TR("Projected confidence").c_str());

    if (ui::gsn::g_BoldFont)
        ImGui::PushFont(ui::gsn::g_BoldFont);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.accent_hover));
    ImGui::Text("%.2f", value);
    ImGui::PopStyleColor();
    if (ui::gsn::g_BoldFont)
        ImGui::PopFont();

    ImGui::ProgressBar(value, ImVec2(-1.0f, 8.0f), "");
}

void DrawFinalConfidence(float value, bool active) {
    const Theme& theme = GetTheme();
    value = ClampConfidenceValue(value);
    ConfidenceFieldLabel(AF_TR("Confidence"));
    fonts::Push(fonts::Role::BodyStrong);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImGui::ColorConvertU32ToFloat4(active ? theme.accent_hover : WithAlpha(theme.text_secondary, 0.58f)));
    ImGui::Text("%.2f", value);
    ImGui::PopStyleColor();
    fonts::Pop();
    ImGui::PushStyleColor(ImGuiCol_FrameBg,
                          ImGui::ColorConvertU32ToFloat4(WithAlpha(theme.surface_3, active ? 0.68f : 0.28f)));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                          ImGui::ColorConvertU32ToFloat4(
                              WithAlpha(active ? theme.accent : theme.text_secondary, active ? 0.90f : 0.22f)));
    ImGui::ProgressBar(value, ImVec2(-1.0f, 10.0f), "");
    ImGui::PopStyleColor(2);
}

void DrawOpinionTriangle(const char* id, SubjectiveOpinion& opinion, const ImVec2& requested_size) {
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
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const float rounding = theme.rounding_panel;
    const ImVec2 surface_min = origin;
    const ImVec2 surface_max(origin.x + size.x, origin.y + size.y);
    const float help_radius = 9.0f;
    const ImVec2 help_center(surface_max.x - help_radius - 10.0f, surface_min.y + help_radius + 10.0f);
    const ImVec2 help_min(help_center.x - help_radius - 2.0f, help_center.y - help_radius - 2.0f);
    const ImVec2 help_max(help_center.x + help_radius + 2.0f, help_center.y + help_radius + 2.0f);
    const bool help_hovered = ImGui::IsMouseHoveringRect(help_min, help_max, true);
    const ImGuiID suppress_triangle_drag_id = ImGui::GetID("##suppress_triangle_drag");
    if (ImGui::IsItemActivated())
        ImGui::GetStateStorage()->SetBool(suppress_triangle_drag_id, help_hovered);
    if (!active)
        ImGui::GetStateStorage()->SetBool(suppress_triangle_drag_id, false);
    const bool suppress_triangle_drag = ImGui::GetStateStorage()->GetBool(suppress_triangle_drag_id, false);
    const bool pressed_or_dragged = active && !suppress_triangle_drag && ImGui::IsMouseDown(ImGuiMouseButton_Left);

    draw_list->AddRectFilled(
        surface_min, surface_max, WithAlpha(theme.surface_2, hovered || active ? 0.96f : 0.78f), rounding);
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

    draw_list->AddLine(
        uncertainty, ImVec2((disbelief.x + belief.x) * 0.5f, bottom), WithAlpha(theme.border_strong, 0.44f), 1.2f);
    draw_list->AddLine(disbelief,
                       ImVec2((uncertainty.x + belief.x) * 0.5f, (uncertainty.y + belief.y) * 0.5f),
                       WithAlpha(theme.border_strong, 0.36f),
                       1.0f);
    draw_list->AddLine(belief,
                       ImVec2((uncertainty.x + disbelief.x) * 0.5f, (uncertainty.y + disbelief.y) * 0.5f),
                       WithAlpha(theme.border_strong, 0.36f),
                       1.0f);
    draw_list->AddTriangle(uncertainty, disbelief, belief, WithAlpha(theme.border_strong, 0.95f), 1.8f);

    const bool labels_fit = size.x >= 230.0f;
    DrawTextCentered(draw_list,
                     ImVec2(uncertainty.x, std::max(origin.y + 10.0f, uncertainty.y - 15.0f)),
                     AF_TR("Uncertainty").c_str(),
                     theme.text_secondary);
    DrawTextCentered(draw_list,
                     ImVec2(disbelief.x + (labels_fit ? 22.0f : 12.0f), disbelief.y + 16.0f),
                     AF_TR("Disbelief").c_str(),
                     theme.text_secondary);
    DrawTextCentered(draw_list,
                     ImVec2(belief.x - (labels_fit ? 16.0f : 8.0f), belief.y + 16.0f),
                     AF_TR("Belief").c_str(),
                     theme.text_secondary);

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

    if (pressed_or_dragged) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        SubjectiveOpinion next = OpinionFromPoint(
            ToConfidencePoint(mouse), uncertainty_vertex, disbelief_vertex, belief_vertex, opinion.base_rate);
        opinion = next;
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
        ImGui::SetTooltip("%s", AF_TR("Jøsang's opinion triangle").c_str());
    } else if (hovered || active) {
        ImGui::SetTooltip("%s",
                          ui::i18n::trf("Belief {0:.2f}\nDisbelief {1:.2f}\nUncertainty {2:.2f}",
                                        opinion.belief,
                                        opinion.disbelief,
                                        opinion.uncertainty)
                              .c_str());
    }

    ImGui::PopID();
}

void DrawDirectMode(ElementConfidence& confidence) {
    confidence.direct_value = ClampConfidenceValue(confidence.direct_value);
    ImGui::TextUnformatted(AF_TR("Confidence").c_str());
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##direct_confidence", &confidence.direct_value, 0.0f, 1.0f, "%.2f"))
        confidence.direct_value = ClampConfidenceValue(confidence.direct_value);
    DrawProjectedConfidence(confidence.direct_value);
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
    const std::string base_rate_tooltip =
        AF_TR("Base rate controls how much unresolved uncertainty counts toward projected "
              "confidence.\nProjected confidence = belief + base rate * uncertainty.");
    ImGui::TextUnformatted(ui::i18n::trf("Base rate {0:.2f}", confidence.opinion.base_rate).c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", base_rate_tooltip.c_str());
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##base_rate", &confidence.opinion.base_rate, 0.0f, 1.0f, "%.2f"))
        confidence.opinion.base_rate = ClampConfidenceValue(confidence.opinion.base_rate);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", base_rate_tooltip.c_str());

    DrawProjectedConfidence(confidence.opinion.ProjectedConfidence());
}

} // namespace

bool ShowConfidencePanel(const ConfidencePanelModel& model, const ConfidencePanelCallbacks& callbacks) {
    const Theme& theme = GetTheme();
    ConfidenceSectionHeader();
    bool changed = false;

    if (!model.storage_warning.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.warning));
        ImGui::TextWrapped("%s", model.storage_warning.c_str());
        ImGui::PopStyleColor();
        if (callbacks.backup_invalid_and_reset && ImGui::Button(AF_TR("Back up and start new confidence file").c_str()))
            changed = callbacks.backup_invalid_and_reset() || changed;
        return changed;
    }

    if (!model.has_assessment) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.text_secondary));
        ImGui::TextWrapped("%s", AF_TR("No confidence assessment stored for this element.").c_str());
        ImGui::PopStyleColor();

        if (ImGui::Button(AF_TR("Add fixed confidence").c_str()) && callbacks.add_confidence)
            changed = callbacks.add_confidence(ConfidenceInputMode::DirectValue) || changed;
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Add Jøsang confidence").c_str()) && callbacks.add_confidence)
            changed = callbacks.add_confidence(ConfidenceInputMode::OpinionTriangle) || changed;
        return changed;
    }

    ElementConfidence confidence = model.confidence;
    const ElementConfidence before = confidence;

    DrawFinalConfidence(model.expected_confidence, confidence.enabled);
    ImGui::Spacing();
    // Translate the caller-supplied labels defensively: AF_TR on an
    // already-translated string falls back to itself, so this is safe whether
    // the caller passed an English msgid or a pre-translated label.
    const std::string method =
        model.method_label.empty() ? AF_TR(MethodLabel(confidence.mode)) : AF_TR(model.method_label);
    ConfidenceMetadataRow(AF_TR("Method"), method);
    const std::string status = model.status_label.empty() ? AF_TR("Active") : AF_TR(model.status_label);
    ConfidenceMetadataRow(AF_TR("Status"), status);

    if (model.stale) {
        const std::string warning =
            AF_TR("This confidence assessment may be stale because the element changed after the value was stored.");
        const ImGuiStyle& style = ImGui::GetStyle();
        const float warning_child_width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float warning_wrap_width = std::max(1.0f, warning_child_width - style.WindowPadding.x * 2.0f);
        const float warning_text_height = ImGui::CalcTextSize(warning.c_str(), nullptr, false, warning_wrap_width).y;
        const float warning_height = warning_text_height + style.WindowPadding.y * 2.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
                              ImGui::ColorConvertU32ToFloat4(LerpColor(theme.surface_1, theme.warning, 0.09f)));
        ImGui::BeginChild("##stale_confidence_notice",
                          ImVec2(0.0f, warning_height),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.warning));
        ImGui::TextWrapped("%s", warning.c_str());
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        if (ImGui::Button(AF_TR("Mark as reviewed").c_str()) && callbacks.mark_reviewed)
            changed = callbacks.mark_reviewed() || changed;
    }

    bool enabled = confidence.enabled;
    bool active_toggled = false;
    if (ImGui::Checkbox(AF_TR("Enable confidence for this element").c_str(), &enabled)) {
        confidence.enabled = enabled;
        active_toggled = true;
        if (callbacks.set_active)
            changed = callbacks.set_active(enabled) || changed;
    }

    if (!confidence.enabled)
        return changed;

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted(AF_TR("Mode").c_str());
    DrawModeSelector(confidence);
    ImGui::Spacing();

    if (confidence.mode == ConfidenceInputMode::DirectValue)
        DrawDirectMode(confidence);
    else
        DrawOpinionMode(confidence);

    ElementConfidence value_before = before;
    ElementConfidence value_after = confidence;
    if (active_toggled)
        value_after.enabled = value_before.enabled;
    if (ConfidenceChanged(value_before, value_after) && callbacks.save_confidence)
        changed = callbacks.save_confidence(confidence) || changed;

    return changed;
}

} // namespace ui::panels
