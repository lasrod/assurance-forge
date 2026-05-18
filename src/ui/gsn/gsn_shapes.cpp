#include "ui/gsn/gsn_shapes.h"

#include "ui/gsn/gsn_dpi.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>

namespace ui::gsn {

namespace {

constexpr int kShadowLayers = 3;
constexpr int kCircleSegments = 48;
constexpr float kOutlineThickness = 1.0f;
constexpr float kUndDiamondRadius = 24.0f;
constexpr float kUndGap = 0.50f;
constexpr float kDetailedNodeZoom = 0.70f;
constexpr float kUndLabelZoom = 0.55f;

bool ShouldDrawDetailedNodeEffects(float zoom) {
    return zoom >= kDetailedNodeZoom;
}

int CircleSegmentsForZoom(float zoom) {
    if (zoom < 0.50f)
        return 16;
    if (zoom < 0.85f)
        return 24;
    return kCircleSegments;
}

ImU32 OutlineColor() {
    return WithAlpha(GetTheme().border_strong, 0.85f);
}

// Soft 3-layer drop shadow under a rounded rectangle.
void DrawRectShadow(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, float rounding, float zoom) {
    const Theme& th = GetTheme();
    float scale = DpiScale() * zoom;
    for (int i = 0; i < kShadowLayers; ++i) {
        float oy = (i + 1) * th.shadow_offset * scale;
        float ox = oy * 0.25f;
        float alpha_mul = th.shadow_alpha_top * (1.0f - (float)i / (float)kShadowLayers);
        ImU32 col = WithAlpha(IM_COL32(0, 0, 0, 255), alpha_mul);
        draw_list->AddRectFilled(
            ImVec2(top_left.x + ox, top_left.y + oy), ImVec2(bottom_right.x + ox, bottom_right.y + oy), col, rounding);
    }
}

// Soft 3-layer drop shadow under a circle.
void DrawCircleShadow(ImDrawList* draw_list, ImVec2 center, float radius, float zoom) {
    const Theme& th = GetTheme();
    float scale = DpiScale() * zoom;
    for (int i = 0; i < kShadowLayers; ++i) {
        float oy = (i + 1) * th.shadow_offset * scale;
        float ox = oy * 0.25f;
        float alpha_mul = th.shadow_alpha_top * (1.0f - (float)i / (float)kShadowLayers);
        ImU32 col = WithAlpha(IM_COL32(0, 0, 0, 255), alpha_mul);
        draw_list->AddCircleFilled(ImVec2(center.x + ox, center.y + oy), radius, col, kCircleSegments);
    }
}

// Soft drop shadow under an arbitrary convex polygon (up to 8 verts).
void DrawPolyShadow(ImDrawList* draw_list, const ImVec2* points, int count, float zoom) {
    const Theme& th = GetTheme();
    float scale = DpiScale() * zoom;
    for (int i = 0; i < kShadowLayers; ++i) {
        float oy = (i + 1) * th.shadow_offset * scale;
        float ox = oy * 0.25f;
        float alpha_mul = th.shadow_alpha_top * (1.0f - (float)i / (float)kShadowLayers);
        ImU32 col = WithAlpha(IM_COL32(0, 0, 0, 255), alpha_mul);
        ImVec2 shifted[8];
        int n = count > 8 ? 8 : count;
        for (int k = 0; k < n; ++k)
            shifted[k] = ImVec2(points[k].x + ox, points[k].y + oy);
        draw_list->AddConvexPolyFilled(shifted, n, col);
    }
}

// Thin top highlight + subtle bottom shading inside a rounded rect.
// Draws a full-size rounded rect (so ImGui doesn't clamp the rounding) and
// clips it to the band's vertical slice. The band edges then perfectly trace
// the shape's curvature - which matters for stadiums whose end-cap radius is
// far larger than the band height.
void AddInteriorShading(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 base_color, float rounding) {
    float h = bottom_right.y - top_left.y;
    if (h < 6.0f)
        return;
    ImU32 highlight = WithAlpha(ShadeColor(base_color, 0.25f), 0.55f);
    ImU32 shade = WithAlpha(ShadeColor(base_color, -0.25f), 0.35f);
    float band_h = h * 0.18f;

    // Top highlight band
    draw_list->PushClipRect(ImVec2(top_left.x, top_left.y), ImVec2(bottom_right.x, top_left.y + band_h), true);
    draw_list->AddRectFilled(top_left, bottom_right, highlight, rounding);
    draw_list->PopClipRect();

    // Bottom shade band
    draw_list->PushClipRect(ImVec2(top_left.x, bottom_right.y - band_h), ImVec2(bottom_right.x, bottom_right.y), true);
    draw_list->AddRectFilled(top_left, bottom_right, shade, rounding);
    draw_list->PopClipRect();
}

} // namespace

void DrawParallelogram(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color, float zoom) {
    float skew = (bottom_right.x - top_left.x) * kParallelogramSkew;
    float outline = DpiSize(kOutlineThickness) * zoom;
    ImVec2 corners[4] = {
        ImVec2(top_left.x + skew, top_left.y),         // top-left (inset right)
        ImVec2(bottom_right.x, top_left.y),            // top-right
        ImVec2(bottom_right.x - skew, bottom_right.y), // bottom-right (inset left)
        ImVec2(top_left.x, bottom_right.y)             // bottom-left
    };
    if (ShouldDrawDetailedNodeEffects(zoom))
        DrawPolyShadow(draw_list, corners, 4, zoom);
    draw_list->AddConvexPolyFilled(corners, 4, fill_color);
    if (ShouldDrawDetailedNodeEffects(zoom)) {
        ImU32 hl = WithAlpha(ShadeColor(fill_color, 0.30f), 0.55f);
        ImVec2 hl_pts[4] = {corners[0],
                            corners[1],
                            ImVec2(corners[1].x - skew * 0.15f, corners[1].y + (bottom_right.y - top_left.y) * 0.18f),
                            ImVec2(corners[0].x - skew * 0.15f, corners[0].y + (bottom_right.y - top_left.y) * 0.18f)};
        draw_list->AddConvexPolyFilled(hl_pts, 4, hl);
    }
    draw_list->AddPolyline(corners, 4, OutlineColor(), ImDrawFlags_Closed, outline);
}

void DrawStadium(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color, float zoom) {
    float rounding = (bottom_right.y - top_left.y) * 0.5f;
    float outline = DpiSize(kOutlineThickness) * zoom;
    if (ShouldDrawDetailedNodeEffects(zoom))
        DrawRectShadow(draw_list, top_left, bottom_right, rounding, zoom);
    draw_list->AddRectFilled(top_left, bottom_right, fill_color, rounding);
    if (ShouldDrawDetailedNodeEffects(zoom))
        AddInteriorShading(draw_list, top_left, bottom_right, fill_color, rounding);
    draw_list->AddRect(top_left, bottom_right, OutlineColor(), rounding, 0, outline);
}

void DrawCircle(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color, float zoom) {
    float width = bottom_right.x - top_left.x;
    float height = bottom_right.y - top_left.y;
    ImVec2 center((top_left.x + bottom_right.x) * 0.5f, (top_left.y + bottom_right.y) * 0.5f);
    float radius = (width < height ? width : height) * 0.5f;
    float outline = DpiSize(kOutlineThickness) * zoom;
    int segments = CircleSegmentsForZoom(zoom);
    if (ShouldDrawDetailedNodeEffects(zoom))
        DrawCircleShadow(draw_list, center, radius, zoom);
    draw_list->AddCircleFilled(center, radius, fill_color, segments);
    if (ShouldDrawDetailedNodeEffects(zoom)) {
        ImU32 hl = WithAlpha(ShadeColor(fill_color, 0.35f), 0.45f);
        draw_list->AddCircleFilled(
            ImVec2(center.x - radius * 0.18f, center.y - radius * 0.30f), radius * 0.55f, hl, segments);
    }
    draw_list->AddCircle(center, radius, OutlineColor(), segments, outline);
}

void DrawRoundedRect(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color, float zoom) {
    float rounding = DpiSize(kClaimRounding) * zoom;
    float outline = DpiSize(kOutlineThickness) * zoom;
    if (ShouldDrawDetailedNodeEffects(zoom))
        DrawRectShadow(draw_list, top_left, bottom_right, rounding, zoom);
    draw_list->AddRectFilled(top_left, bottom_right, fill_color, rounding);
    if (ShouldDrawDetailedNodeEffects(zoom))
        AddInteriorShading(draw_list, top_left, bottom_right, fill_color, rounding);
    draw_list->AddRect(top_left, bottom_right, OutlineColor(), rounding, 0, outline);
}

void DrawUndevelopedMarker(ImDrawList* draw_list,
                           const GsnNode& node,
                           ImVec2 top_left,
                           ImVec2 bottom_right,
                           float zoom) {
    if (!node.undeveloped)
        return;

    float radius = DpiSize(kUndDiamondRadius) * zoom;
    float gap = DpiSize(kUndGap) * zoom;
    ImVec2 center((top_left.x + bottom_right.x) * 0.5f, bottom_right.y + gap + radius);
    ImVec2 diamond[4] = {ImVec2(center.x, center.y - radius),
                         ImVec2(center.x + radius, center.y),
                         ImVec2(center.x, center.y + radius),
                         ImVec2(center.x - radius, center.y)};
    if (ShouldDrawDetailedNodeEffects(zoom))
        DrawPolyShadow(draw_list, diamond, 4, zoom);
    ImU32 und_fill = IM_COL32(245, 247, 252, 255); // near-white for high contrast
    ImU32 und_ink = InkOn(und_fill);
    draw_list->AddConvexPolyFilled(diamond, 4, und_fill);
    draw_list->AddPolyline(diamond, 4, OutlineColor(), ImDrawFlags_Closed, DpiSize(kOutlineThickness) * zoom);

    if (zoom < kUndLabelZoom)
        return;

    const char* und = "UND";
    ImFont* font = ImGui::GetFont();
    float desired_font_size = ImGui::GetFontSize() * zoom * 1.4f;

    // Keep the label readable at normal zoom levels, but do not let a fixed
    // minimum font size outgrow the zoom-scaled diamond marker.
    ImVec2 unit_text_size = font->CalcTextSizeA(1.0f, FLT_MAX, 0.0f, und);
    float max_text_extent = radius * 1.8f;
    float max_font_size_from_width =
        (unit_text_size.x > 0.0f) ? (max_text_extent / unit_text_size.x) : desired_font_size;
    float max_font_size_from_height =
        (unit_text_size.y > 0.0f) ? (max_text_extent / unit_text_size.y) : desired_font_size;
    float max_font_size = std::min(max_font_size_from_width, max_font_size_from_height);
    float min_font_size = std::min(DpiSize(10.0f), max_font_size);
    float font_size = std::clamp(desired_font_size, min_font_size, max_font_size);
    ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, und);
    ImVec2 text_pos(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f);
    draw_list->AddText(font, font_size, text_pos, und_ink, und);
}

void DrawReviewScopeHighlight(ImDrawList* draw_list,
                              ImVec2 top_left,
                              ImVec2 bottom_right,
                              float zoom,
                              bool primary) {
    const Theme& theme = GetTheme();
    float scale = DpiScale() * zoom;
    float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 4.0f);
    float alpha = primary ? 0.40f + pulse * 0.20f : 0.22f + pulse * 0.14f;
    float pad = (primary ? 7.0f : 5.0f) * scale;
    float thickness = (primary ? 2.2f : 1.6f) * scale;
    draw_list->AddRect(ImVec2(top_left.x - pad, top_left.y - pad),
                       ImVec2(bottom_right.x + pad, bottom_right.y + pad),
                       WithAlpha(theme.accent, alpha),
                       DpiSize(kClaimRounding) * zoom + pad,
                       0,
                       thickness);
}

} // namespace ui::gsn
