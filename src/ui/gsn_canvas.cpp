#include "ui/gsn_canvas.h"
#include "ui/gsn_canvas_renderer.h"

#include <iostream>

namespace ui {

ImFont* g_BoldFont = nullptr;

// Single shared renderer instance used by the compatibility wrapper.
static GsnCanvas& GlobalRenderer() {
    static GsnCanvas instance;
    return instance;
}

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

void DrawGsnNode(const GsnNode& node, ImVec2 canvas_origin) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImVec2(canvas_origin.x + node.position.x, canvas_origin.y + node.position.y);
    ImVec2 b = ImVec2(a.x + node.size.x, a.y + node.size.y);

    ImU32 col = ColorForType(node.type);
    ImU32 outline = IM_COL32(0,0,0,200);
    float cx = (a.x + b.x) * 0.5f;
    float cy = (a.y + b.y) * 0.5f;

    // Draw shape based on GSN notation
    if (node.type == "Strategy") {
        // Parallelogram: skew inward at top/bottom
        float skew = node.size.x * 0.15f;
        ImVec2 p[4] = {
            ImVec2(a.x + skew, a.y),         // top-left
            ImVec2(b.x, a.y),                 // top-right
            ImVec2(b.x - skew, b.y),          // bottom-right
            ImVec2(a.x, b.y)                  // bottom-left
        };
        dl->AddConvexPolyFilled(p, 4, col);
        dl->AddPolyline(p, 4, outline, ImDrawFlags_Closed, 2.0f);
    } else if (node.type == "Context" || node.type == "Assumption" || node.type == "Justification") {
        // Rounded rectangle / stadium shape (large rounding for ellipse-like look)
        float rounding = node.size.y * 0.5f;
        dl->AddRectFilled(a, b, col, rounding);
        dl->AddRect(a, b, outline, rounding, 0, 2.0f);
    } else if (node.type == "Solution" || node.type == "Evidence") {
        // Circle centered in the bounding box
        float radius = (node.size.x < node.size.y ? node.size.x : node.size.y) * 0.5f;
        dl->AddCircleFilled(ImVec2(cx, cy), radius, col, 36);
        dl->AddCircle(ImVec2(cx, cy), radius, outline, 36, 2.0f);
    } else {
        // Claim / Other: rectangle with slight rounding
        dl->AddRectFilled(a, b, col, 6.0f);
        dl->AddRect(a, b, outline, 6.0f, 0, 2.0f);
    }

    // Draw label with word-wrapping inside the node box
    float pad = 6.0f;
    float text_left = a.x + pad;
    float text_wrap = node.size.x - pad * 2.0f;

    // For parallelogram/circle, inset text more to avoid clipping edges
    if (node.type == "Strategy") {
        float skew = node.size.x * 0.15f;
        text_left = a.x + skew + pad;
        text_wrap = node.size.x - skew * 2.0f - pad * 2.0f;
    } else if (node.type == "Solution" || node.type == "Evidence") {
        float radius = (node.size.x < node.size.y ? node.size.x : node.size.y) * 0.5f;
        float inset = radius * 0.29f; // ~sqrt(2)/2 inset for circle
        text_left = cx - radius + inset + pad;
        text_wrap = (radius - inset) * 2.0f - pad * 2.0f;
    } else if (node.type == "Context" || node.type == "Assumption" || node.type == "Justification") {
        float inset = node.size.y * 0.15f;
        text_left = a.x + inset + pad;
        text_wrap = node.size.x - inset * 2.0f - pad * 2.0f;
    }

    if (text_wrap < 40.0f) text_wrap = 40.0f;

    // Split label into first line (ID: Name) and rest (description)
    ImFont* bold = g_BoldFont ? g_BoldFont : ImGui::GetFont();
    ImFont* normal = ImGui::GetFont();
    float font_size = ImGui::GetFontSize();
    ImU32 text_col = IM_COL32(10,10,10,255);

    const char* label_cstr = node.label.c_str();
    const char* newline = strchr(label_cstr, '\n');

    // Measure both parts for vertical centering
    ImVec2 bold_size(0, 0);
    ImVec2 rest_size(0, 0);
    if (newline) {
        bold_size = bold->CalcTextSizeA(font_size, FLT_MAX, text_wrap, label_cstr, newline);
        rest_size = normal->CalcTextSizeA(font_size, FLT_MAX, text_wrap, newline + 1, nullptr);
    } else {
        bold_size = bold->CalcTextSizeA(font_size, FLT_MAX, text_wrap, label_cstr, nullptr);
    }
    float total_h = bold_size.y + rest_size.y;
    float text_y = a.y + (node.size.y - total_h) * 0.5f;
    if (text_y < a.y + pad) text_y = a.y + pad;

    // Draw bold first line
    dl->AddText(bold, font_size, ImVec2(text_left, text_y), text_col,
                label_cstr, newline ? newline : nullptr, text_wrap);
    // Draw rest in normal font
    if (newline) {
        dl->AddText(normal, font_size, ImVec2(text_left, text_y + bold_size.y), text_col,
                    newline + 1, nullptr, text_wrap);
    }

    // Invisible button for hit-testing
    ImGui::SetCursorScreenPos(a);
    ImGui::InvisibleButton(node.id.c_str(), node.size);
    if (ImGui::IsItemClicked()) {
        std::cout << "Clicked node: " << node.label << " (id=" << node.id << ")\n";
        // Also show a small ImGui popup text as feedback
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
