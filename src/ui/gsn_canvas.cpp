#include "ui/gsn_canvas.h"
#include "ui/gsn_canvas_renderer.h"
#include "ui/theme.h"
#include "ui/tree_view.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <iostream>

namespace ui {

// g_BoldFont is defined in gsn_layout.cpp (shared between layout and drawing)

// ===== Node drawing constants =====
static constexpr float kTextPadding        = 6.0f;   // padding between shape edge and text
static constexpr float kMinTextWrap        = 40.0f;  // minimum text wrap width
static constexpr float kParallelogramSkew  = 0.15f;  // fraction of width used for skew inset
static constexpr float kCircleTextInset    = 0.29f;  // fraction of radius for circle text area (~1 - sqrt(2)/2)
static constexpr float kStadiumTextInset   = 0.15f;  // fraction of height for stadium shape text inset
static constexpr float kClaimRounding      = 8.0f;   // corner rounding for rectangular Claim nodes
static constexpr float kOutlineThickness   = 1.0f;   // shape outline stroke width (hairline)
static constexpr int   kCircleSegments     = 48;     // number of segments for circle rendering
static constexpr float kUndDiamondRadius   = 24.0f;
static constexpr float kUndGap             = 0.50f;

// Number of stacked offset layers used for soft drop shadows under nodes.
static constexpr int   kShadowLayers       = 3;

// Zoom step used by keyboard and button controls (matches renderer constant)
static constexpr float kZoomStep = 0.1f;

// Flag: true when mouse is hovering over overlay controls (zoom/language buttons).
// Set each frame before node rendering so that node clicks are suppressed.
static bool g_overlay_hovered = false;

// Single shared renderer instance used by the compatibility wrapper.
static GsnCanvas& GlobalRenderer() {
    static GsnCanvas instance;
    return instance;
}

// ===== Shape color mapping =====

static ImU32 ColorForType(const std::string& type) {
    const Theme& th = GetTheme();
    if (type == "Claim")         return th.node_claim;
    if (type == "Strategy")      return th.node_strategy;
    if (type == "Solution")      return th.node_solution;
    if (type == "Context")       return th.node_context;
    if (type == "Assumption")    return th.node_assumption;
    if (type == "Justification") return th.node_justification;
    if (type == "Evidence")      return th.node_evidence;
    return th.node_context;
}

static ImU32 OutlineColor() { return WithAlpha(GetTheme().border_strong, 0.85f); }

// Draw a soft 3-layer drop shadow under a rounded rectangle.
static void DrawRectShadow(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, float rounding) {
    const Theme& th = GetTheme();
    for (int i = 0; i < kShadowLayers; ++i) {
        float oy = (i + 1) * th.shadow_offset;
        float ox = oy * 0.25f;
        float alpha_mul = th.shadow_alpha_top * (1.0f - (float)i / (float)kShadowLayers);
        ImU32 col = WithAlpha(IM_COL32(0, 0, 0, 255), alpha_mul);
        draw_list->AddRectFilled(
            ImVec2(top_left.x + ox, top_left.y + oy),
            ImVec2(bottom_right.x + ox, bottom_right.y + oy),
            col, rounding);
    }
}

// Draw a soft 3-layer drop shadow under a circle.
static void DrawCircleShadow(ImDrawList* draw_list, ImVec2 center, float radius) {
    const Theme& th = GetTheme();
    for (int i = 0; i < kShadowLayers; ++i) {
        float oy = (i + 1) * th.shadow_offset;
        float ox = oy * 0.25f;
        float alpha_mul = th.shadow_alpha_top * (1.0f - (float)i / (float)kShadowLayers);
        ImU32 col = WithAlpha(IM_COL32(0, 0, 0, 255), alpha_mul);
        draw_list->AddCircleFilled(ImVec2(center.x + ox, center.y + oy), radius, col, kCircleSegments);
    }
}

// Draw a soft drop shadow under an arbitrary convex polygon.
static void DrawPolyShadow(ImDrawList* draw_list, const ImVec2* points, int count) {
    const Theme& th = GetTheme();
    for (int i = 0; i < kShadowLayers; ++i) {
        float oy = (i + 1) * th.shadow_offset;
        float ox = oy * 0.25f;
        float alpha_mul = th.shadow_alpha_top * (1.0f - (float)i / (float)kShadowLayers);
        ImU32 col = WithAlpha(IM_COL32(0, 0, 0, 255), alpha_mul);
        ImVec2 shifted[8];
        int n = count > 8 ? 8 : count;
        for (int k = 0; k < n; ++k) shifted[k] = ImVec2(points[k].x + ox, points[k].y + oy);
        draw_list->AddConvexPolyFilled(shifted, n, col);
    }
}

// Add a thin top highlight + subtle bottom shading inside a rounded rect.
// Draws a full-size rounded rect (so ImGui doesn't clamp the rounding) and
// clips it to the band's vertical slice. The band edges then perfectly trace
// the shape's curvature - which matters for stadiums whose end-cap radius is
// far larger than the band height.
static void AddInteriorShading(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right,
                               ImU32 base_color, float rounding) {
    float h = bottom_right.y - top_left.y;
    if (h < 6.0f) return;
    ImU32 highlight = WithAlpha(ShadeColor(base_color,  0.25f), 0.55f);
    ImU32 shade     = WithAlpha(ShadeColor(base_color, -0.25f), 0.35f);
    float band_h = h * 0.18f;

    // Top highlight band
    draw_list->PushClipRect(
        ImVec2(top_left.x, top_left.y),
        ImVec2(bottom_right.x, top_left.y + band_h), true);
    draw_list->AddRectFilled(top_left, bottom_right, highlight, rounding);
    draw_list->PopClipRect();

    // Bottom shade band
    draw_list->PushClipRect(
        ImVec2(top_left.x, bottom_right.y - band_h),
        ImVec2(bottom_right.x, bottom_right.y), true);
    draw_list->AddRectFilled(top_left, bottom_right, shade, rounding);
    draw_list->PopClipRect();
}

// ===== Shape drawing helpers =====

// Draw a parallelogram (Strategy shape) with inward-skewed top/bottom edges.
static void DrawParallelogram(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color) {
    float skew = (bottom_right.x - top_left.x) * kParallelogramSkew;
    ImVec2 corners[4] = {
        ImVec2(top_left.x + skew, top_left.y),         // top-left (inset right)
        ImVec2(bottom_right.x, top_left.y),            // top-right
        ImVec2(bottom_right.x - skew, bottom_right.y), // bottom-right (inset left)
        ImVec2(top_left.x, bottom_right.y)             // bottom-left
    };
    DrawPolyShadow(draw_list, corners, 4);
    draw_list->AddConvexPolyFilled(corners, 4, fill_color);
    // Subtle top highlight via a thin lighter strip across the inside top edge.
    ImU32 hl = WithAlpha(ShadeColor(fill_color, 0.30f), 0.55f);
    ImVec2 hl_pts[4] = {
        corners[0],
        corners[1],
        ImVec2(corners[1].x - skew * 0.15f, corners[1].y + (bottom_right.y - top_left.y) * 0.18f),
        ImVec2(corners[0].x - skew * 0.15f, corners[0].y + (bottom_right.y - top_left.y) * 0.18f)
    };
    draw_list->AddConvexPolyFilled(hl_pts, 4, hl);
    draw_list->AddPolyline(corners, 4, OutlineColor(), ImDrawFlags_Closed, kOutlineThickness);
}

// Draw a stadium / rounded rectangle (Context, Assumption, Justification shapes).
static void DrawStadium(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color) {
    float rounding = (bottom_right.y - top_left.y) * 0.5f;
    DrawRectShadow(draw_list, top_left, bottom_right, rounding);
    draw_list->AddRectFilled(top_left, bottom_right, fill_color, rounding);
    AddInteriorShading(draw_list, top_left, bottom_right, fill_color, rounding);
    draw_list->AddRect(top_left, bottom_right, OutlineColor(), rounding, 0, kOutlineThickness);
}

// Draw a circle (Solution, Evidence shapes) centered in the bounding box.
static void DrawCircle(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color) {
    float width  = bottom_right.x - top_left.x;
    float height = bottom_right.y - top_left.y;
    ImVec2 center((top_left.x + bottom_right.x) * 0.5f,
                  (top_left.y + bottom_right.y) * 0.5f);
    float radius = (width < height ? width : height) * 0.5f;
    DrawCircleShadow(draw_list, center, radius);
    draw_list->AddCircleFilled(center, radius, fill_color, kCircleSegments);
    // Soft inner highlight: an offset lighter circle clipped within the disc.
    ImU32 hl = WithAlpha(ShadeColor(fill_color, 0.35f), 0.45f);
    draw_list->AddCircleFilled(
        ImVec2(center.x - radius * 0.18f, center.y - radius * 0.30f),
        radius * 0.55f, hl, kCircleSegments);
    draw_list->AddCircle(center, radius, OutlineColor(), kCircleSegments, kOutlineThickness);
}

// Draw a rounded rectangle (Claim / default shape).
static void DrawRoundedRect(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color) {
    DrawRectShadow(draw_list, top_left, bottom_right, kClaimRounding);
    draw_list->AddRectFilled(top_left, bottom_right, fill_color, kClaimRounding);
    AddInteriorShading(draw_list, top_left, bottom_right, fill_color, kClaimRounding);
    draw_list->AddRect(top_left, bottom_right, OutlineColor(), kClaimRounding, 0, kOutlineThickness);
}

static void DrawUndevelopedMarker(ImDrawList* draw_list, const GsnNode& node,
                                  ImVec2 top_left, ImVec2 bottom_right, float zoom) {
    if (!node.undeveloped) return;

    float radius = kUndDiamondRadius * zoom;
    float gap = kUndGap * zoom;
    ImVec2 center((top_left.x + bottom_right.x) * 0.5f, bottom_right.y + gap + radius);
    ImVec2 diamond[4] = {
        ImVec2(center.x, center.y - radius),
        ImVec2(center.x + radius, center.y),
        ImVec2(center.x, center.y + radius),
        ImVec2(center.x - radius, center.y)
    };
    DrawPolyShadow(draw_list, diamond, 4);
    ImU32 und_fill = IM_COL32(245, 247, 252, 255); // near-white for high contrast
    ImU32 und_ink  = InkOn(und_fill);
    draw_list->AddConvexPolyFilled(diamond, 4, und_fill);
    draw_list->AddPolyline(diamond, 4, OutlineColor(), ImDrawFlags_Closed, kOutlineThickness);

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
    float min_font_size = std::min(10.0f, max_font_size);
    float font_size = std::clamp(desired_font_size, min_font_size, max_font_size);
    ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, und);
    ImVec2 text_pos(center.x - text_size.x * 0.5f,
                    center.y - text_size.y * 0.5f);
    draw_list->AddText(font, font_size, text_pos, und_ink, und);
}

// ===== Text layout helper =====

// Compute the horizontal text region (left edge and wrap width) for a given node shape.
// All outputs are in screen-space (already scaled by zoom).
static void ComputeTextRegion(const GsnNode& node, ImVec2 top_left, ImVec2 bottom_right,
                              float zoom, float& out_text_left, float& out_text_wrap) {
    float scaled_padding = kTextPadding * zoom;
    float scaled_width  = node.size.x * zoom;
    float scaled_height = node.size.y * zoom;

    out_text_left = top_left.x + scaled_padding;
    out_text_wrap = scaled_width - scaled_padding * 2.0f;

    if (node.type == "Strategy") {
        float skew = scaled_width * kParallelogramSkew;
        out_text_left = top_left.x + skew + scaled_padding;
        out_text_wrap = scaled_width - skew * 2.0f - scaled_padding * 2.0f;
    } else if (node.type == "Solution" || node.type == "Evidence") {
        float center_x = (top_left.x + bottom_right.x) * 0.5f;
        float radius = (scaled_width < scaled_height ? scaled_width : scaled_height) * 0.5f;
        float inset = radius * kCircleTextInset;
        out_text_left = center_x - radius + inset + scaled_padding;
        out_text_wrap = (radius - inset) * 2.0f - scaled_padding * 2.0f;
    } else if (node.type == "Context" || node.type == "Assumption" || node.type == "Justification") {
        float inset = scaled_height * kStadiumTextInset;
        out_text_left = top_left.x + inset + scaled_padding;
        out_text_wrap = scaled_width - inset * 2.0f - scaled_padding * 2.0f;
    }

    float scaled_min_wrap = kMinTextWrap * zoom;
    if (out_text_wrap < scaled_min_wrap) out_text_wrap = scaled_min_wrap;
}

// Draw the node label: bold first line (ID: Name), normal text for rest (description).
// Text is vertically centered within the node bounding box.
static void DrawNodeLabel(ImDrawList* draw_list, const GsnNode& node,
                          ImVec2 top_left, ImVec2 bottom_right,
                          float text_left, float text_wrap, float zoom,
                          ImU32 ink_color) {
    ImFont* bold_font   = g_BoldFont ? g_BoldFont : ImGui::GetFont();
    ImFont* normal_font = ImGui::GetFont();
    float font_size = ImGui::GetFontSize() * zoom;
    float scaled_padding = kTextPadding * zoom;

    // Pick label based on language toggle
    const UiState& state = GetUiState();
    const std::string& active_label = (state.show_secondary_language && !node.label_secondary.empty())
                                      ? node.label_secondary : node.label;
    const char* label_start = active_label.c_str();
    const char* first_newline = strchr(label_start, '\n');

    // Measure both parts for vertical centering
    ImVec2 bold_text_size(0, 0);
    ImVec2 rest_text_size(0, 0);
    if (first_newline) {
        bold_text_size = bold_font->CalcTextSizeA(font_size, FLT_MAX, text_wrap, label_start, first_newline);
        rest_text_size = normal_font->CalcTextSizeA(font_size, FLT_MAX, text_wrap, first_newline + 1, nullptr);
    } else {
        bold_text_size = bold_font->CalcTextSizeA(font_size, FLT_MAX, text_wrap, label_start, nullptr);
    }

    float scaled_node_height = node.size.y * zoom;
    float total_text_height = bold_text_size.y + rest_text_size.y;
    float text_y = top_left.y + (scaled_node_height - total_text_height) * 0.5f;
    if (text_y < top_left.y + scaled_padding) text_y = top_left.y + scaled_padding;

    // Bold first line
    draw_list->AddText(bold_font, font_size, ImVec2(text_left, text_y), ink_color,
                       label_start, first_newline ? first_newline : nullptr, text_wrap);
    // Normal rest
    if (first_newline) {
        draw_list->AddText(normal_font, font_size, ImVec2(text_left, text_y + bold_text_size.y), ink_color,
                           first_newline + 1, nullptr, text_wrap);
    }
}

// ===== Main node drawing function =====

void DrawGsnNode(const GsnNode& node, ImVec2 canvas_origin, float zoom) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 top_left  = ImVec2(canvas_origin.x + node.position.x * zoom,
                              canvas_origin.y + node.position.y * zoom);
    ImVec2 bottom_right = ImVec2(top_left.x + node.size.x * zoom,
                                 top_left.y + node.size.y * zoom);
    ImVec2 scaled_size = ImVec2(node.size.x * zoom, node.size.y * zoom);

    ImU32 fill_color = ColorForType(node.type);

    // If this node is marked for pending removal, override the fill with a
    // strong red tint so the user can see exactly what will be removed.
    const bool marked_for_removal =
        GetUiState().marked_for_removal.count(node.id) > 0;
    if (marked_for_removal) {
        fill_color = GetTheme().danger;
    }

    // Subtle hover brighten so nodes feel responsive without shifting layout.
    {
        ImVec2 mouse = ImGui::GetIO().MousePos;
        if (!g_overlay_hovered &&
            mouse.x >= top_left.x && mouse.x <= bottom_right.x &&
            mouse.y >= top_left.y && mouse.y <= bottom_right.y) {
            fill_color = ShadeColor(fill_color, 0.06f);
        }
    }

    // Draw the GSN shape
    if (node.type == "Strategy") {
        DrawParallelogram(draw_list, top_left, bottom_right, fill_color);
    } else if (node.type == "Context" || node.type == "Assumption" || node.type == "Justification") {
        DrawStadium(draw_list, top_left, bottom_right, fill_color);
    } else if (node.type == "Solution" || node.type == "Evidence") {
        DrawCircle(draw_list, top_left, bottom_right, fill_color);
    } else {
        DrawRoundedRect(draw_list, top_left, bottom_right, fill_color);
    }

    // Draw label text
    float text_left, text_wrap;
    ComputeTextRegion(node, top_left, bottom_right, zoom, text_left, text_wrap);
    ImU32 ink = marked_for_removal ? GetTheme().text_primary : InkOn(fill_color);
    DrawNodeLabel(draw_list, node, top_left, bottom_right, text_left, text_wrap, zoom, ink);
    DrawUndevelopedMarker(draw_list, node, top_left, bottom_right, zoom);

    // Invisible button for hit-testing.
    // SetNextItemAllowOverlap lets overlay buttons (zoom/language) receive clicks
    // even when they overlap a node's hit area.
    ImGui::SetCursorScreenPos(top_left);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton(node.id.c_str(), scaled_size);
    if (ImGui::IsItemClicked() && !g_overlay_hovered) {
        GetUiState().selected_element_id = node.id;
    }

    // Right-click context menu: select the node, then offer the Add submenu.
    if (ImGui::BeginPopupContextItem(node.id.c_str())) {
        GetUiState().selected_element_id = node.id;
        RenderAddElementMenu();
        ImGui::Separator();
        RenderRemoveSubmenu();
        ImGui::EndPopup();
    }

    // Highlight selected node with a soft accent glow ring (3 concentric rects,
    // decreasing alpha) instead of a hard outline.
    if (GetUiState().selected_element_id == node.id) {
        const Theme& th_sel = GetTheme();
        for (int i = 0; i < 3; ++i) {
            float pad = 2.0f + (float)i * 2.0f;
            float alpha = 0.55f - (float)i * 0.15f;
            draw_list->AddRect(
                ImVec2(top_left.x - pad, top_left.y - pad),
                ImVec2(bottom_right.x + pad, bottom_right.y + pad),
                WithAlpha(th_sel.accent, alpha),
                kClaimRounding + pad, 0, 1.5f);
        }
    }

    // Marked-for-removal border (drawn after the selection highlight so a
    // selected & marked node still looks unambiguously red).
    if (marked_for_removal) {
        const Theme& th_rm = GetTheme();
        draw_list->AddRect(
            ImVec2(top_left.x - 3.0f, top_left.y - 3.0f),
            ImVec2(bottom_right.x + 3.0f, bottom_right.y + 3.0f),
            ShadeColor(th_rm.danger, -0.20f), kClaimRounding + 3.0f, 0, 2.5f);
    }
}

void ShowGsnCanvasContent() {
    // Child region with clipping; we manage our own pan/zoom offset
    // so no ImGui scrollbars are needed.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(GetTheme().canvas_bg));
    ImGui::BeginChild("gsn_canvas_child", ImVec2(0, 0), false,
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();

        // --- Zoom & pan input handling ---
        GsnCanvas& renderer = GlobalRenderer();
        ImVec2 child_pos = ImGui::GetWindowPos();

        // --- Background dot grid (drawn behind everything else) ---
        {
            const Theme& th_grid = GetTheme();
            ImDrawList* bg = ImGui::GetWindowDrawList();
            ImVec2 sz = ImGui::GetWindowSize();
            float zoom = renderer.GetZoom();
            ImVec2 offset = renderer.GetViewOffset();
            float spacing = th_grid.canvas_grid_spacing * zoom;
            // Raise skip threshold to avoid merging dots and excessive draw calls
            // at low zoom levels. Pre-check total dot count so we either draw the
            // full grid or skip it entirely (avoids mid-loop cutoff artifacts).
            constexpr float kMinSpacing = 10.0f;
            constexpr int   kMaxDots    = 4000;
            if (spacing >= kMinSpacing) {
                int est_x = static_cast<int>(sz.x / spacing) + 1;
                int est_y = static_cast<int>(sz.y / spacing) + 1;
                if (est_x * est_y <= kMaxDots) {
                    float start_x = -fmodf(offset.x, spacing);
                    float start_y = -fmodf(offset.y, spacing);
                    int ix = (int)floorf(offset.x / spacing);
                    int iy0 = (int)floorf(offset.y / spacing);
                    float dot = std::max(1.0f, zoom * 0.9f);
                    for (float x = start_x; x < sz.x; x += spacing, ++ix) {
                        int iy = iy0;
                        for (float y = start_y; y < sz.y; y += spacing, ++iy) {
                            bool major = (ix % 4 == 0) && (iy % 4 == 0);
                            ImU32 c = major ? th_grid.canvas_grid_major : th_grid.canvas_grid_minor;
                            ImVec2 p(child_pos.x + x, child_pos.y + y);
                            bg->AddRectFilled(
                                ImVec2(p.x - dot * 0.5f, p.y - dot * 0.5f),
                                ImVec2(p.x + dot * 0.5f, p.y + dot * 0.5f),
                                c);
                        }
                    }
                }
            }
        }

        // Center on selected element if requested (e.g. from tree view click)
        {
            UiState& state = GetUiState();
            if (state.center_on_selection && !state.selected_element_id.empty()) {
                ImVec2 viewport_size = ImGui::GetWindowSize();
                renderer.CenterOnNode(state.selected_element_id, viewport_size);
                state.center_on_selection = false;
            }
            if (state.center_on_marked && !state.marked_for_removal.empty()) {
                ImVec2 viewport_size = ImGui::GetWindowSize();
                renderer.CenterOnIds(state.marked_for_removal, viewport_size);
                state.center_on_marked = false;
            }
        }

        // Ctrl + mouse scroll wheel: zoom at mouse pointer position
        // Plain scroll wheel (no Ctrl): pan vertically
        if (ImGui::IsWindowHovered()) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f && ImGui::GetIO().KeyCtrl) {
                // Convert mouse screen position to content-space (unzoomed layout coords)
                ImVec2 mouse = ImGui::GetIO().MousePos;
                ImVec2 offset = renderer.GetViewOffset();
                float zoom = renderer.GetZoom();
                ImVec2 focus_content(
                    (mouse.x - child_pos.x + offset.x) / zoom,
                    (mouse.y - child_pos.y + offset.y) / zoom
                );
                float new_zoom = zoom + (wheel > 0.0f ? kZoomStep : -kZoomStep);
                renderer.ZoomAtPoint(new_zoom, focus_content);
            } else if (wheel != 0.0f) {
                // Scroll wheel without Ctrl: pan vertically (Shift+wheel: pan horizontally)
                float scroll_speed = 60.0f;
                if (ImGui::GetIO().KeyShift)
                    renderer.Pan(-wheel * scroll_speed, 0.0f);
                else
                    renderer.Pan(0.0f, -wheel * scroll_speed);
            }
        }

        // Middle mouse button panning
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            renderer.Pan(-delta.x, -delta.y);
        }

        // Keyboard +/- (numpad and main keyboard)
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
                renderer.ZoomIn();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
                renderer.ZoomOut();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_0) || ImGui::IsKeyPressed(ImGuiKey_Keypad0)) {
                renderer.ResetZoom();
            }
        }

        // --- Pre-compute overlay button rects and check if mouse is over them ---
        // This prevents node clicks from firing when clicking overlay controls.
        {
            ImVec2 child_size_pre = ImGui::GetWindowSize();
            ImVec2 mouse_pos = ImGui::GetIO().MousePos;
            g_overlay_hovered = false;

            // Zoom strip rect
            float btn_sz = 28.0f;
            float mgn = 12.0f;
            float lbl_w = 50.0f;
            float strip_w = btn_sz * 2 + lbl_w + 12.0f;
            float zx = child_pos.x + child_size_pre.x - (btn_sz * 2 + lbl_w + mgn + 8.0f);
            float zy = child_pos.y + child_size_pre.y - (btn_sz + mgn);
            ImVec2 zoom_tl(zx - 4.0f, zy - 2.0f);
            ImVec2 zoom_br(zx + strip_w, zy + btn_sz + 2.0f);
            if (mouse_pos.x >= zoom_tl.x && mouse_pos.x <= zoom_br.x &&
                mouse_pos.y >= zoom_tl.y && mouse_pos.y <= zoom_br.y) {
                g_overlay_hovered = true;
            }

            // Language button rect
            UiState& state_pre = GetUiState();
            if (state_pre.show_secondary_language || state_pre.model_has_translations) {
                float lbw = 36.0f, lbh = 24.0f, lmgn = 12.0f;
                float lx = child_pos.x + child_size_pre.x - (lbw + lmgn);
                float ly = child_pos.y + child_size_pre.y - (28.0f + lmgn) - lbh - 6.0f;
                ImVec2 lang_tl(lx - 2.0f, ly - 2.0f);
                ImVec2 lang_br(lx + lbw + 2.0f, ly + lbh + 2.0f);
                if (mouse_pos.x >= lang_tl.x && mouse_pos.x <= lang_br.x &&
                    mouse_pos.y >= lang_tl.y && mouse_pos.y <= lang_br.y) {
                    g_overlay_hovered = true;
                }
            }
        }

        // Render the canvas content
        renderer.Render();

        // --- Language toggle button above zoom strip (bottom-right) ---
        {
            UiState& state = GetUiState();
            // Only show when model has translations
            if (state.show_secondary_language || state.model_has_translations) {
                ImVec2 child_size_lang = ImGui::GetWindowSize();
                float lang_btn_w = 36.0f;
                float lang_btn_h = 24.0f;
                float lang_margin = 12.0f;
                float lang_x = child_pos.x + child_size_lang.x - (lang_btn_w + lang_margin);
                float lang_y = child_pos.y + child_size_lang.y - (28.0f + lang_margin) - lang_btn_h - 6.0f;

                ImDrawList* fg_lang = ImGui::GetWindowDrawList();
                fg_lang->AddRectFilled(ImVec2(lang_x - 4.0f, lang_y - 3.0f),
                                       ImVec2(lang_x + lang_btn_w + 4.0f, lang_y + lang_btn_h + 3.0f),
                                       WithAlpha(GetTheme().surface_2, 0.85f), 8.0f);
                fg_lang->AddRect(ImVec2(lang_x - 4.0f, lang_y - 3.0f),
                                 ImVec2(lang_x + lang_btn_w + 4.0f, lang_y + lang_btn_h + 3.0f),
                                 GetTheme().border, 8.0f, 0, 1.0f);

                ImGui::SetCursorScreenPos(ImVec2(lang_x, lang_y));
                // Show "EN" when primary, or the active secondary language code (uppercased)
                char lang_upper[4] = {};
                const std::string& sl = state.active_secondary_lang;
                for (size_t i = 0; i < sl.size() && i < 3; ++i)
                    lang_upper[i] = (char)toupper((unsigned char)sl[i]);
                const char* lang_label = state.show_secondary_language ? lang_upper : "EN";
                if (ImGui::Button(lang_label, ImVec2(lang_btn_w, lang_btn_h))) {
                    state.show_secondary_language = !state.show_secondary_language;
                }
            }
        }

        // --- Keyboard and mouse shortcut hints (top-left) ---
        {
            const char* hint_1 = "Ctrl+Wheel  Zoom";
            const char* hint_2 = "Middle Drag  Pan";

            ImDrawList* fg_hints = ImGui::GetWindowDrawList();
            ImVec2 hint_pos(child_pos.x + 12.0f, child_pos.y + 12.0f);
            ImVec2 hint_size(164.0f, 44.0f);

            const Theme& th_hint = GetTheme();
            fg_hints->AddRectFilled(
                hint_pos,
                ImVec2(hint_pos.x + hint_size.x, hint_pos.y + hint_size.y),
                WithAlpha(th_hint.surface_2, 0.85f), 8.0f);
            fg_hints->AddRect(
                hint_pos,
                ImVec2(hint_pos.x + hint_size.x, hint_pos.y + hint_size.y),
                th_hint.border, 8.0f, 0, 1.0f);

            fg_hints->AddText(ImVec2(hint_pos.x + 10.0f, hint_pos.y + 8.0f),  th_hint.text_secondary, hint_1);
            fg_hints->AddText(ImVec2(hint_pos.x + 10.0f, hint_pos.y + 26.0f), th_hint.text_secondary, hint_2);
        }

        // --- Overlay zoom buttons in bottom-right corner ---
        {
            ImVec2 child_size = ImGui::GetWindowSize();
            float button_size = 28.0f;
            float margin = 12.0f;
            float label_width = 50.0f;

            // Position: bottom-right of the child window
            float buttons_x = child_pos.x + child_size.x - (button_size * 2 + label_width + margin + 8.0f);
            float buttons_y = child_pos.y + child_size.y - (button_size + margin);

            // Semi-transparent background for the zoom control strip
            ImDrawList* fg = ImGui::GetWindowDrawList();
            float strip_width = button_size * 2 + label_width + 12.0f;
            ImVec2 strip_tl(buttons_x - 4.0f, buttons_y - 2.0f);
            ImVec2 strip_br(buttons_x + strip_width, buttons_y + button_size + 2.0f);
            const Theme& th_zoom = GetTheme();
            fg->AddRectFilled(strip_tl, strip_br, WithAlpha(th_zoom.surface_2, 0.85f), 8.0f);
            fg->AddRect(strip_tl, strip_br, th_zoom.border, 8.0f, 0, 1.0f);

            ImGui::SetCursorScreenPos(ImVec2(buttons_x, buttons_y));
            if (ImGui::Button("-##zoom_out", ImVec2(button_size, button_size))) {
                renderer.ZoomOut();
            }

            ImGui::SameLine();
            // Zoom percentage label
            char zoom_label[16];
            snprintf(zoom_label, sizeof(zoom_label), "%d%%", static_cast<int>(renderer.GetZoom() * 100.0f + 0.5f));
            ImVec2 text_size = ImGui::CalcTextSize(zoom_label);
            float label_x = ImGui::GetCursorScreenPos().x + (label_width - text_size.x) * 0.5f;
            float label_y = ImGui::GetCursorScreenPos().y + (button_size - text_size.y) * 0.5f;
            fg->AddText(ImVec2(label_x, label_y), GetTheme().text_secondary, zoom_label);
            ImGui::Dummy(ImVec2(label_width, button_size));

            ImGui::SameLine();
            if (ImGui::Button("+##zoom_in", ImVec2(button_size, button_size))) {
                renderer.ZoomIn();
            }
        }

        // --- Custom scrollbars ---
        {
            ImVec2 content_min, content_max;
            renderer.GetContentBounds(content_min, content_max);

            float zoom = renderer.GetZoom();
            ImVec2 offset = renderer.GetViewOffset();
            ImVec2 child_size = ImGui::GetWindowSize();

            // Total content size in screen pixels (zoomed)
            float content_w = (content_max.x - content_min.x) * zoom;
            float content_h = (content_max.y - content_min.y) * zoom;

            // Viewport position relative to content origin (in screen pixels)
            float viewport_x = offset.x - content_min.x * zoom;
            float viewport_y = offset.y - content_min.y * zoom;

            float scrollbar_thickness = 10.0f;
            float scrollbar_margin = 2.0f;
            const Theme& th_sb = GetTheme();
            ImU32 track_color = WithAlpha(th_sb.surface_1, 0.55f);
            ImU32 thumb_color = th_sb.surface_3;
            ImU32 thumb_hover = LerpColor(th_sb.surface_3, th_sb.accent, 0.45f);

            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            // Horizontal scrollbar (along bottom edge, above zoom controls area)
            if (content_w > child_size.x) {
                float bar_y = child_pos.y + child_size.y - scrollbar_thickness - scrollbar_margin;
                float bar_x = child_pos.x + scrollbar_margin;
                float bar_w = child_size.x - scrollbar_thickness - scrollbar_margin * 3;

                // Track
                draw_list->AddRectFilled(
                    ImVec2(bar_x, bar_y),
                    ImVec2(bar_x + bar_w, bar_y + scrollbar_thickness),
                    track_color, 4.0f);

                // Thumb
                float thumb_ratio = child_size.x / content_w;
                float thumb_w = bar_w * thumb_ratio;
                if (thumb_w < 20.0f) thumb_w = 20.0f;
                float scroll_ratio = viewport_x / (content_w - child_size.x);
                if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
                if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;
                float thumb_x = bar_x + scroll_ratio * (bar_w - thumb_w);

                ImVec2 thumb_tl(thumb_x, bar_y);
                ImVec2 thumb_br(thumb_x + thumb_w, bar_y + scrollbar_thickness);

                // Hit test for dragging
                ImGui::SetCursorScreenPos(thumb_tl);
                ImGui::InvisibleButton("##hscroll_thumb", ImVec2(thumb_w, scrollbar_thickness));
                bool h_hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                    float delta_px = ImGui::GetIO().MouseDelta.x;
                    float delta_scroll = delta_px / (bar_w - thumb_w) * (content_w - child_size.x);
                    renderer.Pan(delta_scroll, 0.0f);
                }

                draw_list->AddRectFilled(thumb_tl, thumb_br, h_hovered ? thumb_hover : thumb_color, 4.0f);
            }

            // Vertical scrollbar (along right edge)
            if (content_h > child_size.y) {
                float bar_x = child_pos.x + child_size.x - scrollbar_thickness - scrollbar_margin;
                float bar_y = child_pos.y + scrollbar_margin;
                float bar_h = child_size.y - scrollbar_thickness - scrollbar_margin * 3;

                // Track
                draw_list->AddRectFilled(
                    ImVec2(bar_x, bar_y),
                    ImVec2(bar_x + scrollbar_thickness, bar_y + bar_h),
                    track_color, 4.0f);

                // Thumb
                float thumb_ratio = child_size.y / content_h;
                float thumb_h = bar_h * thumb_ratio;
                if (thumb_h < 20.0f) thumb_h = 20.0f;
                float scroll_ratio = viewport_y / (content_h - child_size.y);
                if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
                if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;
                float thumb_y = bar_y + scroll_ratio * (bar_h - thumb_h);

                ImVec2 thumb_tl(bar_x, thumb_y);
                ImVec2 thumb_br(bar_x + scrollbar_thickness, thumb_y + thumb_h);

                // Hit test for dragging
                ImGui::SetCursorScreenPos(thumb_tl);
                ImGui::InvisibleButton("##vscroll_thumb", ImVec2(scrollbar_thickness, thumb_h));
                bool v_hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                    float delta_px = ImGui::GetIO().MouseDelta.y;
                    float delta_scroll = delta_px / (bar_h - thumb_h) * (content_h - child_size.y);
                    renderer.Pan(0.0f, delta_scroll);
                }

                draw_list->AddRectFilled(thumb_tl, thumb_br, v_hovered ? thumb_hover : thumb_color, 4.0f);
            }
        }

    ImGui::EndChild();
}

void ShowGsnCanvasWindow() {
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse
                                  | ImGuiWindowFlags_NoMove
                                  | ImGuiWindowFlags_NoResize
                                  | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("GSN Canvas", nullptr, window_flags)) {
        ShowGsnCanvasContent();
    }
    ImGui::End();
}

void SetCanvasElements(const std::vector<CanvasElement>& elements) {
    GlobalRenderer().SetElements(elements);
}

void SetCanvasTree(const core::AssuranceTree& tree) {
    GlobalRenderer().SetTree(tree);
}

} // namespace ui
