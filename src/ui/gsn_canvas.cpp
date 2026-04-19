#include "ui/gsn_canvas.h"
#include "ui/gsn_canvas_renderer.h"

#include <iostream>

namespace ui {

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
    dl->AddRectFilled(a, b, col, 6.0f);
    dl->AddRect(a, b, IM_COL32(0,0,0,200), 6.0f, 0, 2.0f);

    // Draw label with word-wrapping inside the node box
    const float pad = 6.0f;
    float wrap_width = node.size.x - pad * 2.0f;
    ImVec2 text_size = ImGui::CalcTextSize(node.label.c_str(), nullptr, false, wrap_width);
    // Center vertically within the box
    float text_y = a.y + (node.size.y - text_size.y) * 0.5f;
    if (text_y < a.y + pad) text_y = a.y + pad;
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(a.x + pad, text_y),
                IM_COL32(10,10,10,255), node.label.c_str(), nullptr, wrap_width);

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
