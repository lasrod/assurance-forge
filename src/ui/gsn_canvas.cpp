#include "ui/gsn_canvas.h"
#include "ui/gsn_canvas_renderer.h"

#include <iostream>

namespace ui {

// g_BoldFont is defined in gsn_layout.cpp (shared between layout and drawing)

// ===== Node drawing constants =====
static constexpr float kTextPadding        = 6.0f;   // padding between shape edge and text
static constexpr float kMinTextWrap        = 40.0f;  // minimum text wrap width
static constexpr float kParallelogramSkew  = 0.15f;  // fraction of width used for skew inset
static constexpr float kCircleTextInset    = 0.29f;  // fraction of radius for circle text area (~1 - sqrt(2)/2)
static constexpr float kStadiumTextInset   = 0.15f;  // fraction of height for stadium shape text inset
static constexpr float kClaimRounding      = 6.0f;   // corner rounding for rectangular Claim nodes
static constexpr float kOutlineThickness   = 2.0f;   // shape outline stroke width
static constexpr int   kCircleSegments     = 36;     // number of segments for circle rendering

static const ImU32 kOutlineColor = IM_COL32(0, 0, 0, 200);
static const ImU32 kTextColor    = IM_COL32(10, 10, 10, 255);

// Zoom step used by keyboard and button controls (matches renderer constant)
static constexpr float kZoomStep = 0.1f;

// Single shared renderer instance used by the compatibility wrapper.
static GsnCanvas& GlobalRenderer() {
    static GsnCanvas instance;
    return instance;
}

// ===== Shape color mapping =====

static ImU32 ColorForType(const std::string& type) {
    if (type == "Claim")         return IM_COL32(100, 220, 100, 255);
    if (type == "Strategy")      return IM_COL32(100, 160, 230, 255);
    if (type == "Solution")      return IM_COL32(230, 180,  80, 255);
    if (type == "Context")       return IM_COL32(200, 200, 200, 255);
    if (type == "Assumption")    return IM_COL32(240, 160, 160, 255);
    if (type == "Justification") return IM_COL32(180, 200, 240, 255);
    if (type == "Evidence")      return IM_COL32(230, 180,  80, 255);
    return IM_COL32(200, 200, 200, 255);
}

// ===== Shape drawing helpers =====

// Draw a parallelogram (Strategy shape) with inward-skewed top/bottom edges.
static void DrawParallelogram(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color) {
    float skew = (bottom_right.x - top_left.x) * kParallelogramSkew;
    ImVec2 corners[4] = {
        ImVec2(top_left.x + skew, top_left.y),      // top-left (inset right)
        ImVec2(bottom_right.x, top_left.y),          // top-right
        ImVec2(bottom_right.x - skew, bottom_right.y), // bottom-right (inset left)
        ImVec2(top_left.x, bottom_right.y)           // bottom-left
    };
    draw_list->AddConvexPolyFilled(corners, 4, fill_color);
    draw_list->AddPolyline(corners, 4, kOutlineColor, ImDrawFlags_Closed, kOutlineThickness);
}

// Draw a stadium / rounded rectangle (Context, Assumption, Justification shapes).
static void DrawStadium(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color) {
    float rounding = (bottom_right.y - top_left.y) * 0.5f; // fully round the short edges
    draw_list->AddRectFilled(top_left, bottom_right, fill_color, rounding);
    draw_list->AddRect(top_left, bottom_right, kOutlineColor, rounding, 0, kOutlineThickness);
}

// Draw a circle (Solution, Evidence shapes) centered in the bounding box.
static void DrawCircle(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color) {
    float width  = bottom_right.x - top_left.x;
    float height = bottom_right.y - top_left.y;
    ImVec2 center((top_left.x + bottom_right.x) * 0.5f,
                  (top_left.y + bottom_right.y) * 0.5f);
    float radius = (width < height ? width : height) * 0.5f;
    draw_list->AddCircleFilled(center, radius, fill_color, kCircleSegments);
    draw_list->AddCircle(center, radius, kOutlineColor, kCircleSegments, kOutlineThickness);
}

// Draw a rounded rectangle (Claim / default shape).
static void DrawRoundedRect(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color) {
    draw_list->AddRectFilled(top_left, bottom_right, fill_color, kClaimRounding);
    draw_list->AddRect(top_left, bottom_right, kOutlineColor, kClaimRounding, 0, kOutlineThickness);
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
                          float text_left, float text_wrap, float zoom) {
    ImFont* bold_font   = g_BoldFont ? g_BoldFont : ImGui::GetFont();
    ImFont* normal_font = ImGui::GetFont();
    float font_size = ImGui::GetFontSize() * zoom;
    float scaled_padding = kTextPadding * zoom;

    const char* label_start = node.label.c_str();
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
    draw_list->AddText(bold_font, font_size, ImVec2(text_left, text_y), kTextColor,
                       label_start, first_newline ? first_newline : nullptr, text_wrap);
    // Normal rest
    if (first_newline) {
        draw_list->AddText(normal_font, font_size, ImVec2(text_left, text_y + bold_text_size.y), kTextColor,
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
    DrawNodeLabel(draw_list, node, top_left, bottom_right, text_left, text_wrap, zoom);

    // Invisible button for hit-testing
    ImGui::SetCursorScreenPos(top_left);
    ImGui::InvisibleButton(node.id.c_str(), scaled_size);
    if (ImGui::IsItemClicked()) {
        std::cout << "Clicked node: " << node.label << " (id=" << node.id << ")\n";
        ImGui::OpenPopup(("clicked_" + node.id).c_str());
    }

    // Popup feedback (transient)
    if (ImGui::BeginPopup(("clicked_" + node.id).c_str())) {
        ImGui::Text("Clicked: %s", node.label.c_str());
        ImGui::EndPopup();
    }
}

void ShowGsnCanvasWindow() {
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_None;
    // Ensure the canvas appears on-screen by providing a sensible
    // default position/size on first use (avoids stale imgui.ini values).
    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("GSN Canvas", nullptr, window_flags)) {
        // Reserve a canvas area
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        ImGui::Text("GSN Canvas");
        ImGui::Separator();

        // Child region with clipping; we manage our own pan/zoom offset
        // so no ImGui scrollbars are needed.
        ImGui::BeginChild("gsn_canvas_child", ImVec2(0, 0), false,
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // --- Zoom & pan input handling ---
        GsnCanvas& renderer = GlobalRenderer();
        ImVec2 child_pos = ImGui::GetWindowPos();

        // Ctrl + mouse scroll wheel: zoom at mouse pointer position
        if (ImGui::IsWindowHovered()) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f && ImGui::GetIO().KeyCtrl) {
                // Convert mouse screen position to content-space (unzoomed layout coords)
                ImVec2 mouse = ImGui::GetIO().MousePos;
                ImVec2 offset = renderer.GetViewOffset();
                float zoom = renderer.GetZoom();
                // screen = child_pos + content * zoom - view_offset
                // => content = (mouse - child_pos + view_offset) / zoom
                ImVec2 focus_content(
                    (mouse.x - child_pos.x + offset.x) / zoom,
                    (mouse.y - child_pos.y + offset.y) / zoom
                );
                float new_zoom = zoom + (wheel > 0.0f ? kZoomStep : -kZoomStep);
                renderer.ZoomAtPoint(new_zoom, focus_content);
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

        // Render the canvas content
        renderer.Render();

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
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            float strip_width = button_size * 2 + label_width + 12.0f;
            ImVec2 strip_tl(buttons_x - 4.0f, buttons_y - 2.0f);
            ImVec2 strip_br(buttons_x + strip_width, buttons_y + button_size + 2.0f);
            fg->AddRectFilled(strip_tl, strip_br, IM_COL32(40, 40, 40, 180), 6.0f);
            fg->AddRect(strip_tl, strip_br, IM_COL32(80, 80, 80, 200), 6.0f);

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
            fg->AddText(ImVec2(label_x, label_y), IM_COL32(220, 220, 220, 255), zoom_label);
            ImGui::Dummy(ImVec2(label_width, button_size));

            ImGui::SameLine();
            if (ImGui::Button("+##zoom_in", ImVec2(button_size, button_size))) {
                renderer.ZoomIn();
            }
        }

        ImGui::EndChild();
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
