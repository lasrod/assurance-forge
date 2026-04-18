#include "ui/gsn_canvas.h"

#include <iostream>

namespace ui {

static ImU32 ColorForType(const std::string& type) {
    if (type == "Claim") return IM_COL32(100, 220, 100, 255);
    if (type == "Evidence") return IM_COL32(230, 180, 80, 255);
    return IM_COL32(200, 200, 200, 255);
}

void DrawGsnNode(const GsnNode& node) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetCursorScreenPos();
    ImVec2 a = ImVec2(win_pos.x + node.position.x, win_pos.y + node.position.y);
    ImVec2 b = ImVec2(a.x + node.size.x, a.y + node.size.y);

    ImU32 col = ColorForType(node.type);
    dl->AddRectFilled(a, b, col, 6.0f);
    dl->AddRect(a, b, IM_COL32(0,0,0,200), 6.0f, 0, 2.0f);

    // Draw label centered
    ImVec2 text_size = ImGui::CalcTextSize(node.label.c_str());
    ImVec2 text_pos = ImVec2(a.x + (node.size.x - text_size.x) * 0.5f, a.y + (node.size.y - text_size.y) * 0.5f);
    dl->AddText(text_pos, IM_COL32(10,10,10,255), node.label.c_str());

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
        ImGui::Text("GSN Canvas (hardcoded demo)");
        ImGui::Separator();

        // Start a child region so cursor screen pos is stable
        ImGui::BeginChild("gsn_canvas_child", ImVec2(0, 0), false, ImGuiWindowFlags_NoMove);

        // Hardcoded nodes
        GsnNode claim{ "G1", "Claim", ImVec2(50, 20), ImVec2(180, 80), "Claim: Top Goal" };
        GsnNode evidence{ "E1", "Evidence", ImVec2(300, 160), ImVec2(160, 60), "Evidence: Test Report" };

        DrawGsnNode(claim);
        DrawGsnNode(evidence);

        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace ui
