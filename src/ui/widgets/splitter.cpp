#include "ui/widgets/splitter.h"

#include "ui/theme.h"

namespace ui::widgets {
namespace {

ImVec4 SplitterColor() {
    return ImGui::ColorConvertU32ToFloat4(ui::GetTheme().bg_app);
}

ImVec4 SplitterHoverColor() {
    return ImGui::ColorConvertU32ToFloat4(ui::WithAlpha(ui::GetTheme().accent, 0.55f));
}

}  // namespace

void DrawVerticalSplitter(const char* id,
                          float x,
                          float width,
                          float height,
                          float top_y,
                          float display_w,
                          float& ratio,
                          bool subtract_delta,
                          float min_ratio,
                          float max_ratio,
                          ImGuiWindowFlags panel_flags) {
    ImGui::SetNextWindowPos(ImVec2(x, top_y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, SplitterColor());

    ImGui::Begin(id, nullptr, panel_flags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    ImGui::InvisibleButton("##splitter_btn", ImVec2(width, height));
    bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();
        float cx = wp.x + ws.x * 0.5f;
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(cx, wp.y), ImVec2(cx, wp.y + ws.y),
            ImGui::ColorConvertFloat4ToU32(SplitterHoverColor()), 2.0f);
    }

    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        float delta = ImGui::GetIO().MouseDelta.x / display_w;
        ratio += subtract_delta ? -delta : delta;
        if (ratio < min_ratio) ratio = min_ratio;
        if (ratio > max_ratio) ratio = max_ratio;
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

float DrawHorizontalSplitter(const char* id,
                             float x,
                             float y,
                             float width,
                             float height,
                             ImGuiWindowFlags panel_flags) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, SplitterColor());

    float delta_y = 0.0f;
    ImGui::Begin(id, nullptr, panel_flags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    ImGui::InvisibleButton("##splitter_btn", ImVec2(width, height));
    bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();
        float cy = wp.y + ws.y * 0.5f;
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(wp.x, cy), ImVec2(wp.x + ws.x, cy),
            ImGui::ColorConvertFloat4ToU32(SplitterHoverColor()), 2.0f);
    }

    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        delta_y = ImGui::GetIO().MouseDelta.y;
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    return delta_y;
}

}  // namespace ui::widgets
