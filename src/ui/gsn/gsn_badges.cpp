#include "ui/gsn/gsn_badges.h"

#include "ui/gsn/gsn_canvas.h" // for g_BoldFont
#include "ui/gsn/gsn_dpi.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ui::gsn {

namespace {

constexpr float kAttentionBadgeSize = 18.0f;
constexpr float kAttentionBadgeGap = 6.0f;
constexpr float kAttentionFontScale = 0.95f;
constexpr float kPi = 3.14159265358979323846f;

bool IsMouseHoveringBadge(const BadgeRect& badge) {
    if (!ImGui::IsWindowHovered())
        return false;
    return IsPointInsideBadge(badge, ImGui::GetIO().MousePos);
}

void DrawBadgeShell(ImDrawList* draw_list, const BadgeRect& badge, ImU32 fill, float zoom) {
    float badge_size = badge.max.x - badge.min.x;
    float rounding = badge_size * 0.32f;
    draw_list->AddRectFilled(badge.min, badge.max, fill, rounding);
    draw_list->AddRect(badge.min, badge.max, ShadeColor(fill, -0.30f), rounding, 0, DpiSize(1.0f) * zoom);
}

void DrawCrossGlyph(ImDrawList* draw_list, const BadgeRect& badge, ImU32 color, float zoom) {
    float size = badge.max.x - badge.min.x;
    float stroke = std::max(DpiSize(1.8f) * zoom, 1.0f);
    float pad = size * 0.32f;
    draw_list->AddLine(
        ImVec2(badge.min.x + pad, badge.min.y + pad), ImVec2(badge.max.x - pad, badge.max.y - pad), color, stroke);
    draw_list->AddLine(
        ImVec2(badge.max.x - pad, badge.min.y + pad), ImVec2(badge.min.x + pad, badge.max.y - pad), color, stroke);
}

void DrawSpinnerGlyph(ImDrawList* draw_list, const BadgeRect& badge, ImU32 color, float zoom) {
    float size = badge.max.x - badge.min.x;
    float stroke = std::max(DpiSize(1.7f) * zoom, 1.0f);
    ImVec2 center((badge.min.x + badge.max.x) * 0.5f, (badge.min.y + badge.max.y) * 0.5f);
    float radius = size * 0.26f;
    float start = std::fmod((float)ImGui::GetTime() * 5.2f, kPi * 2.0f);
    float end = start + kPi * 1.45f;
    draw_list->PathClear();
    for (int i = 0; i <= 14; ++i) {
        float t = start + (end - start) * ((float)i / 14.0f);
        draw_list->PathLineTo(ImVec2(center.x + std::cos(t) * radius, center.y + std::sin(t) * radius));
    }
    draw_list->PathStroke(color, false, stroke);
}

// Draw a single character glyph (e.g. "!" / "i") centered inside the badge.
void DrawCenteredGlyph(ImDrawList* draw_list,
                       const BadgeRect& badge,
                       const char* glyph,
                       ImU32 color,
                       float zoom) {
    ImFont* font = g_BoldFont ? g_BoldFont : ImGui::GetFont();
    float font_size = ImGui::GetFontSize() * zoom * kAttentionFontScale;
    ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, glyph);
    ImVec2 text_pos((badge.min.x + badge.max.x - text_size.x) * 0.5f,
                    (badge.min.y + badge.max.y - text_size.y) * 0.5f - DpiScale() * zoom * 0.5f);
    draw_list->AddText(font, font_size, text_pos, color, glyph);
}

const char* SeverityLabel(core::ProblemSeverity severity) {
    switch (severity) {
    case core::ProblemSeverity::Error:
        return "error";
    case core::ProblemSeverity::Warning:
        return "warning";
    case core::ProblemSeverity::Info:
        return "info";
    }
    return "problem";
}

} // namespace

BadgeRect ComputeBadgeRect(ImVec2 top_left, ImVec2 bottom_right, float zoom, int slot, int slot_count) {
    float badge_size = DpiSize(kAttentionBadgeSize) * zoom;
    float badge_gap = DpiSize(kAttentionBadgeGap) * zoom;
    float center_x = (top_left.x + bottom_right.x) * 0.5f;
    float badge_y = top_left.y - badge_size * 0.45f;
    float row_width = badge_size * (float)slot_count + badge_gap * (float)std::max(0, slot_count - 1);
    float badge_x = center_x - row_width * 0.5f + (float)slot * (badge_size + badge_gap);
    ImVec2 badge_min(badge_x, badge_y);
    return BadgeRect{badge_min, ImVec2(badge_min.x + badge_size, badge_min.y + badge_size)};
}

bool IsPointInsideBadge(const BadgeRect& badge, ImVec2 point) {
    return point.x >= badge.min.x && point.x <= badge.max.x && point.y >= badge.min.y && point.y <= badge.max.y;
}

void DrawProblemBadge(ImDrawList* draw_list,
                     const BadgeRect& badge,
                     float zoom,
                     const core::ElementBadgeSummary& summary) {
    const Theme& theme = GetTheme();

    ImU32 fill = theme.warning;
    switch (summary.highest_severity) {
    case core::ProblemSeverity::Error:
        fill = theme.danger;
        break;
    case core::ProblemSeverity::Warning:
        fill = theme.warning;
        break;
    case core::ProblemSeverity::Info:
        fill = theme.accent;
        break;
    }

    DrawBadgeShell(draw_list, badge, fill, zoom);
    const ImU32 ink = InkOn(fill);
    switch (summary.highest_severity) {
    case core::ProblemSeverity::Error:
        DrawCrossGlyph(draw_list, badge, ink, zoom);
        break;
    case core::ProblemSeverity::Warning:
        DrawCenteredGlyph(draw_list, badge, "!", ink, zoom);
        break;
    case core::ProblemSeverity::Info:
        DrawCenteredGlyph(draw_list, badge, "i", ink, zoom);
        break;
    }

    if (IsMouseHoveringBadge(badge)) {
        std::string tooltip;
        if (summary.problem_count > 1) {
            tooltip = std::to_string(summary.problem_count) + " problems";
            tooltip += std::string{" · top "} + SeverityLabel(summary.highest_severity) + ": ";
        } else {
            tooltip = std::string{"1 "} + SeverityLabel(summary.highest_severity) + ": ";
        }
        tooltip += summary.top_problem_message.empty() ? std::string{"(no message)"} : summary.top_problem_message;
        tooltip += "\nClick to open in the Problems panel.";
        ImGui::SetTooltip("%s", tooltip.c_str());
    }
}

void DrawAiSpinnerBadge(ImDrawList* draw_list, const BadgeRect& badge, float zoom) {
    const Theme& theme = GetTheme();
    DrawBadgeShell(draw_list, badge, theme.accent, zoom);
    DrawSpinnerGlyph(draw_list, badge, InkOn(theme.accent), zoom);
    if (IsMouseHoveringBadge(badge)) {
        ImGui::SetTooltip("AI review in progress.");
    }
}

} // namespace ui::gsn
