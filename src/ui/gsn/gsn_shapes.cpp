#include "ui/gsn/gsn_shapes.h"

#include "core/perf/frame_profiler.h"
#include "ui/gsn/gsn_canvas_renderer.h"
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

bool ShouldDrawShadows(float zoom) {
    return core::perf::GetPerfToggles().node_shadows && zoom >= kDetailedNodeZoom;
}

bool ShouldDrawShading(float zoom) {
    return core::perf::GetPerfToggles().node_interior_shading && zoom >= kDetailedNodeZoom;
}

int CircleSegmentsForZoom(float zoom) {
    if (!core::perf::GetPerfToggles().high_segment_circles)
        return 12;
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
    if (ShouldDrawShadows(zoom)) {
        DrawPolyShadow(draw_list, corners, 4, zoom);
        if (auto* stats = CurrentRenderStats())
            ++stats->shadows_drawn;
    }
    draw_list->AddConvexPolyFilled(corners, 4, fill_color);
    if (ShouldDrawShading(zoom)) {
        ImU32 hl = WithAlpha(ShadeColor(fill_color, 0.30f), 0.55f);
        // Keep the band's bottom edge on the parallelogram's slanted sides: at
        // fraction `band_frac` down, the left side has moved right by
        // skew*(1-band_frac) and the right side has moved left by skew*band_frac.
        const float band_frac = 0.18f;
        float band_y = top_left.y + (bottom_right.y - top_left.y) * band_frac;
        ImVec2 hl_pts[4] = {corners[0],
                            corners[1],
                            ImVec2(bottom_right.x - skew * band_frac, band_y),
                            ImVec2(top_left.x + skew * (1.0f - band_frac), band_y)};
        draw_list->AddConvexPolyFilled(hl_pts, 4, hl);
        if (auto* stats = CurrentRenderStats())
            ++stats->interior_shading_drawn;
    }
    draw_list->AddPolyline(corners, 4, OutlineColor(), ImDrawFlags_Closed, outline);
}

void DrawStadium(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color, float zoom) {
    float rounding = DpiSize(kContextRounding) * zoom;
    float outline = DpiSize(kOutlineThickness) * zoom;
    if (ShouldDrawShadows(zoom)) {
        DrawRectShadow(draw_list, top_left, bottom_right, rounding, zoom);
        if (auto* stats = CurrentRenderStats())
            ++stats->shadows_drawn;
    }
    draw_list->AddRectFilled(top_left, bottom_right, fill_color, rounding);
    if (ShouldDrawShading(zoom)) {
        AddInteriorShading(draw_list, top_left, bottom_right, fill_color, rounding);
        if (auto* stats = CurrentRenderStats()) {
            ++stats->interior_shading_drawn;
            stats->clip_rect_pushes += 2;
        }
    }
    draw_list->AddRect(top_left, bottom_right, OutlineColor(), rounding, 0, outline);
}

void DrawCircle(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color, float zoom) {
    float width = bottom_right.x - top_left.x;
    float height = bottom_right.y - top_left.y;
    ImVec2 center((top_left.x + bottom_right.x) * 0.5f, (top_left.y + bottom_right.y) * 0.5f);
    float radius = (width < height ? width : height) * 0.5f;
    float outline = DpiSize(kOutlineThickness) * zoom;
    int segments = CircleSegmentsForZoom(zoom);
    if (ShouldDrawShadows(zoom)) {
        DrawCircleShadow(draw_list, center, radius, zoom);
        if (auto* stats = CurrentRenderStats())
            ++stats->shadows_drawn;
    }
    draw_list->AddCircleFilled(center, radius, fill_color, segments);
    if (ShouldDrawShading(zoom)) {
        ImU32 hl = WithAlpha(ShadeColor(fill_color, 0.35f), 0.45f);
        draw_list->AddCircleFilled(
            ImVec2(center.x - radius * 0.18f, center.y - radius * 0.30f), radius * 0.55f, hl, segments);
        if (auto* stats = CurrentRenderStats())
            ++stats->interior_shading_drawn;
    }
    draw_list->AddCircle(center, radius, OutlineColor(), segments, outline);
}

void DrawRoundedRect(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color, float zoom) {
    float rounding = DpiSize(kClaimRounding) * zoom;
    float outline = DpiSize(kOutlineThickness) * zoom;
    if (ShouldDrawShadows(zoom)) {
        DrawRectShadow(draw_list, top_left, bottom_right, rounding, zoom);
        if (auto* stats = CurrentRenderStats())
            ++stats->shadows_drawn;
    }
    draw_list->AddRectFilled(top_left, bottom_right, fill_color, rounding);
    if (ShouldDrawShading(zoom)) {
        AddInteriorShading(draw_list, top_left, bottom_right, fill_color, rounding);
        if (auto* stats = CurrentRenderStats()) {
            ++stats->interior_shading_drawn;
            stats->clip_rect_pushes += 2;
        }
    }
    draw_list->AddRect(top_left, bottom_right, OutlineColor(), rounding, 0, outline);
}

void DrawUndevelopedMarker(
    ImDrawList* draw_list, const GsnNode& node, ImVec2 top_left, ImVec2 bottom_right, float zoom) {
    if (!node.undeveloped)
        return;

    float radius = DpiSize(kUndDiamondRadius) * zoom;
    float gap = DpiSize(kUndGap) * zoom;
    ImVec2 center((top_left.x + bottom_right.x) * 0.5f, bottom_right.y + gap + radius);
    ImVec2 diamond[4] = {ImVec2(center.x, center.y - radius),
                         ImVec2(center.x + radius, center.y),
                         ImVec2(center.x, center.y + radius),
                         ImVec2(center.x - radius, center.y)};
    if (ShouldDrawShadows(zoom)) {
        DrawPolyShadow(draw_list, diamond, 4, zoom);
        if (auto* stats = CurrentRenderStats())
            ++stats->shadows_drawn;
    }
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

// Draw dashes along a (optionally closed) polyline with a continuous on/off
// phase carried across segment boundaries, so curves approximated by many short
// segments still dash evenly.
void DrawDashedPath(ImDrawList* draw_list,
                    const ImVec2* points,
                    int count,
                    bool closed,
                    ImU32 color,
                    float thickness,
                    float dash_on,
                    float dash_off) {
    if (count < 2 || dash_on <= 0.0f || dash_off <= 0.0f)
        return;
    const int segments = closed ? count : count - 1;
    float phase = 0.0f; // distance already drawn into the current span
    bool on = true;
    for (int i = 0; i < segments; ++i) {
        const ImVec2 a = points[i];
        const ImVec2 b = points[(i + 1) % count];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f)
            continue;
        const float ux = dx / len;
        const float uy = dy / len;
        float pos = 0.0f;
        while (pos < len) {
            const float span = (on ? dash_on : dash_off) - phase;
            const float step = std::min(span, len - pos);
            if (on) {
                const ImVec2 p0(a.x + ux * pos, a.y + uy * pos);
                const ImVec2 p1(a.x + ux * (pos + step), a.y + uy * (pos + step));
                draw_list->AddLine(p0, p1, color, thickness);
            }
            pos += step;
            phase += step;
            if (phase >= (on ? dash_on : dash_off) - 1e-4f) {
                on = !on;
                phase = 0.0f;
            }
        }
    }
}

// Warning "!" badge: a filled triangle with a dark exclamation mark, pinned over
// the node's top-left corner. The triangle shape is the primary (color-blind
// safe) cue; its amber fill is only a secondary aid for color-sighted users.
void DrawCounterWarningBadge(ImDrawList* draw_list, ImVec2 top_left, float zoom) {
    const float scale = DpiScale() * zoom;
    const float size = 20.0f * scale; // triangle base width / height
    const float half = size * 0.5f;
    // Sit the badge so it straddles the node's top-left corner.
    const ImVec2 center(top_left.x + half * 0.35f, top_left.y - half * 0.15f);

    const ImVec2 apex(center.x, center.y - half);
    const ImVec2 base_left(center.x - half, center.y + half * 0.72f);
    const ImVec2 base_right(center.x + half, center.y + half * 0.72f);

    const ImU32 fill = GetTheme().warning;
    const ImU32 ink = InkOn(fill);
    draw_list->AddTriangleFilled(apex, base_left, base_right, fill);
    draw_list->AddTriangle(apex, base_left, base_right, OutlineColor(), std::max(1.0f, 1.4f * scale));

    // Exclamation mark: a short stem with a dot beneath it.
    const ImVec2 stem_top(center.x, center.y - half * 0.30f);
    const ImVec2 stem_bottom(center.x, center.y + half * 0.18f);
    draw_list->AddLine(stem_top, stem_bottom, ink, std::max(1.5f, 2.0f * scale));
    draw_list->AddCircleFilled(ImVec2(center.x, center.y + half * 0.42f), std::max(1.0f, 1.5f * scale), ink);
}

void DrawCounterChallengeDecoration(
    ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, bool circular, float zoom) {
    const float scale = DpiScale() * zoom;
    // The dash pattern + extra thickness carry the meaning independent of color;
    // amber is a secondary cue for color-sighted reviewers.
    const ImU32 color = GetTheme().warning;
    const float thickness = std::max(2.0f, 2.4f * scale);
    const float dash_on = 6.0f * scale;
    const float dash_off = 4.0f * scale;
    const float pad = 2.0f * scale; // sit just outside the shape's hairline outline

    if (circular) {
        const float w = bottom_right.x - top_left.x;
        const float h = bottom_right.y - top_left.y;
        const ImVec2 center((top_left.x + bottom_right.x) * 0.5f, (top_left.y + bottom_right.y) * 0.5f);
        const float radius = std::min(w, h) * 0.5f + pad;
        ImVec2 ring[kCircleSegments];
        for (int i = 0; i < kCircleSegments; ++i) {
            const float t = (static_cast<float>(i) / static_cast<float>(kCircleSegments)) * 2.0f * 3.14159265f;
            ring[i] = ImVec2(center.x + std::cos(t) * radius, center.y + std::sin(t) * radius);
        }
        DrawDashedPath(draw_list, ring, kCircleSegments, /*closed=*/true, color, thickness, dash_on, dash_off);
    } else {
        const ImVec2 corners[4] = {
            ImVec2(top_left.x - pad, top_left.y - pad),
            ImVec2(bottom_right.x + pad, top_left.y - pad),
            ImVec2(bottom_right.x + pad, bottom_right.y + pad),
            ImVec2(top_left.x - pad, bottom_right.y + pad),
        };
        DrawDashedPath(draw_list, corners, 4, /*closed=*/true, color, thickness, dash_on, dash_off);
    }

    DrawCounterWarningBadge(draw_list, top_left, zoom);
}

void DrawReviewScopeHighlight(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, float zoom, bool primary) {
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
