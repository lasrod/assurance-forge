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
static void ComputeTextRegion(const GsnNode& node, ImVec2 top_left, ImVec2 bottom_right,
                              float& out_text_left, float& out_text_wrap) {
    out_text_left = top_left.x + kTextPadding;
    out_text_wrap = node.size.x - kTextPadding * 2.0f;

    if (node.type == "Strategy") {
        float skew = node.size.x * kParallelogramSkew;
        out_text_left = top_left.x + skew + kTextPadding;
        out_text_wrap = node.size.x - skew * 2.0f - kTextPadding * 2.0f;
    } else if (node.type == "Solution" || node.type == "Evidence") {
        float center_x = (top_left.x + bottom_right.x) * 0.5f;
        float radius = (node.size.x < node.size.y ? node.size.x : node.size.y) * 0.5f;
        float inset = radius * kCircleTextInset;
        out_text_left = center_x - radius + inset + kTextPadding;
        out_text_wrap = (radius - inset) * 2.0f - kTextPadding * 2.0f;
    } else if (node.type == "Context" || node.type == "Assumption" || node.type == "Justification") {
        float inset = node.size.y * kStadiumTextInset;
        out_text_left = top_left.x + inset + kTextPadding;
        out_text_wrap = node.size.x - inset * 2.0f - kTextPadding * 2.0f;
    }

    if (out_text_wrap < kMinTextWrap) out_text_wrap = kMinTextWrap;
}

// Draw the node label: bold first line (ID: Name), normal text for rest (description).
// Text is vertically centered within the node bounding box.
static void DrawNodeLabel(ImDrawList* draw_list, const GsnNode& node,
                          ImVec2 top_left, ImVec2 bottom_right,
                          float text_left, float text_wrap) {
    ImFont* bold_font   = g_BoldFont ? g_BoldFont : ImGui::GetFont();
    ImFont* normal_font = ImGui::GetFont();
    float font_size = ImGui::GetFontSize();

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

    float total_text_height = bold_text_size.y + rest_text_size.y;
    float text_y = top_left.y + (node.size.y - total_text_height) * 0.5f;
    if (text_y < top_left.y + kTextPadding) text_y = top_left.y + kTextPadding;

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

void DrawGsnNode(const GsnNode& node, ImVec2 canvas_origin) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 top_left  = ImVec2(canvas_origin.x + node.position.x, canvas_origin.y + node.position.y);
    ImVec2 bottom_right = ImVec2(top_left.x + node.size.x, top_left.y + node.size.y);

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
    ComputeTextRegion(node, top_left, bottom_right, text_left, text_wrap);
    DrawNodeLabel(draw_list, node, top_left, bottom_right, text_left, text_wrap);

    // Invisible button for hit-testing
    ImGui::SetCursorScreenPos(top_left);
    ImGui::InvisibleButton(node.id.c_str(), node.size);
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

        // Start a child region so cursor screen pos is stable
        ImGui::BeginChild("gsn_canvas_child", ImVec2(0, 0), false,
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_HorizontalScrollbar);

        // Use a single shared renderer instance
        GlobalRenderer().Render();

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
