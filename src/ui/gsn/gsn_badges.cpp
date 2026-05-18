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

bool IsMouseOverBadge(const BadgeRect& badge) {
    if (!ImGui::IsWindowHovered())
        return false;
    ImVec2 mouse = ImGui::GetIO().MousePos;
    return mouse.x >= badge.min.x && mouse.x <= badge.max.x && mouse.y >= badge.min.y && mouse.y <= badge.max.y;
}

void DrawBadgeShell(ImDrawList* draw_list, const BadgeRect& badge, ImU32 fill, float zoom) {
    float badge_size = badge.max.x - badge.min.x;
    float rounding = badge_size * 0.32f;
    draw_list->AddRectFilled(badge.min, badge.max, fill, rounding);
    draw_list->AddRect(badge.min, badge.max, ShadeColor(fill, -0.30f), rounding, 0, DpiSize(1.0f) * zoom);
}

void DrawCheckGlyph(ImDrawList* draw_list, const BadgeRect& badge, ImU32 color, float zoom) {
    float size = badge.max.x - badge.min.x;
    float stroke = std::max(DpiSize(1.8f) * zoom, 1.0f);
    ImVec2 a(badge.min.x + size * 0.27f, badge.min.y + size * 0.55f);
    ImVec2 b(badge.min.x + size * 0.43f, badge.min.y + size * 0.70f);
    ImVec2 c(badge.min.x + size * 0.74f, badge.min.y + size * 0.34f);
    draw_list->AddLine(a, b, color, stroke);
    draw_list->AddLine(b, c, color, stroke);
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

std::string ReviewBadgeTooltip(ElementReviewVisualStatus status, const ElementReviewVisualState& state) {
    switch (status) {
    case ElementReviewVisualStatus::AiRunning:
        return "AI review in progress.";
    case ElementReviewVisualStatus::ManualOk:
        return "Review status: manually marked OK";
    case ElementReviewVisualStatus::AiOk: {
        std::string tooltip = "Review status: AI review completed with no findings";
        if (!state.review_profile_name.empty())
            tooltip += "\nProfile: " + state.review_profile_name;
        else if (!state.review_profile_id.empty())
            tooltip += "\nProfile: " + state.review_profile_id;
        return tooltip;
    }
    case ElementReviewVisualStatus::Failed:
        return state.last_review_message.empty() ? "Review status: AI review failed\nSee Problems panel for details."
                                                 : state.last_review_message + "\nSee Problems panel for details.";
    case ElementReviewVisualStatus::None:
        break;
    }
    return {};
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

void DrawAttentionBadge(ImDrawList* draw_list, const BadgeRect& badge, float zoom) {
    const Theme& theme = GetTheme();
    DrawBadgeShell(draw_list, badge, theme.warning, zoom);

    const char* glyph = "!";
    ImFont* font = g_BoldFont ? g_BoldFont : ImGui::GetFont();
    float font_size = ImGui::GetFontSize() * zoom * kAttentionFontScale;
    ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, glyph);
    ImVec2 text_pos((badge.min.x + badge.max.x - text_size.x) * 0.5f,
                    (badge.min.y + badge.max.y - text_size.y) * 0.5f - DpiScale() * zoom * 0.5f);
    draw_list->AddText(font, font_size, text_pos, theme.ink_dark, glyph);
}

void DrawReviewBadge(ImDrawList* draw_list,
                     const BadgeRect& badge,
                     float zoom,
                     ElementReviewVisualStatus status,
                     const ElementReviewVisualState& state) {
    const Theme& theme = GetTheme();
    ImU32 fill = theme.surface_3;
    switch (status) {
    case ElementReviewVisualStatus::AiRunning:
        fill = theme.accent;
        break;
    case ElementReviewVisualStatus::AiOk:
        fill = theme.success;
        break;
    case ElementReviewVisualStatus::ManualOk:
        fill = theme.accent;
        break;
    case ElementReviewVisualStatus::Failed:
        fill = theme.danger;
        break;
    case ElementReviewVisualStatus::None:
        return;
    }

    DrawBadgeShell(draw_list, badge, fill, zoom);
    ImU32 ink = InkOn(fill);
    if (status == ElementReviewVisualStatus::Failed) {
        DrawCrossGlyph(draw_list, badge, ink, zoom);
    } else if (status == ElementReviewVisualStatus::AiRunning) {
        DrawSpinnerGlyph(draw_list, badge, ink, zoom);
    } else {
        DrawCheckGlyph(draw_list, badge, ink, zoom);
    }

    if (IsMouseOverBadge(badge)) {
        std::string tooltip = ReviewBadgeTooltip(status, state);
        if (!tooltip.empty())
            ImGui::SetTooltip("%s", tooltip.c_str());
    }
}

} // namespace ui::gsn
