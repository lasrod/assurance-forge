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

// Returns true while the mouse button is held AND the drag was initiated inside the extended
// hit zone (i.e. outside the core InvisibleButton rect). Uses ImGui state storage to latch
// the "started here" flag per splitter window so the check survives the window moving during drag.
bool TrackExtendedDrag(bool item_active,
                       const ImVec2& ext_min,
                       const ImVec2& ext_max,
                       ImGuiMouseButton button) {
    ImGuiID latch_id = ImGui::GetID("##ext_drag_latch");
    ImGuiStorage* storage = ImGui::GetStateStorage();

    if (!ImGui::IsMouseDown(button)) {
        storage->SetBool(latch_id, false);
        return false;
    }

    bool latched = storage->GetBool(latch_id, false);
    if (!latched && !item_active) {
        ImVec2 click_pos = ImGui::GetIO().MouseClickedPos[button];
        if (click_pos.x >= ext_min.x && click_pos.x <= ext_max.x &&
            click_pos.y >= ext_min.y && click_pos.y <= ext_max.y) {
            storage->SetBool(latch_id, true);
            latched = true;
        }
    }

    return latched && ImGui::IsMouseDragging(button, 0.0f);
}

} // namespace

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
                          float hit_padding,
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
    bool item_active = ImGui::IsItemActive();

    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    ImVec2 ext_min(wp.x - hit_padding, wp.y);
    ImVec2 ext_max(wp.x + ws.x + hit_padding, wp.y + ws.y);

    bool extended_drag = TrackExtendedDrag(item_active, ext_min, ext_max, ImGuiMouseButton_Left);
    bool hovered = item_active || extended_drag || ImGui::IsMouseHoveringRect(ext_min, ext_max, false);

    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        float cx = wp.x + ws.x * 0.5f;
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(cx, wp.y), ImVec2(cx, wp.y + ws.y), ImGui::ColorConvertFloat4ToU32(SplitterHoverColor()), 2.0f);
    }

    bool core_drag = item_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f);
    if (core_drag || extended_drag) {
        float delta = ImGui::GetIO().MouseDelta.x / display_w;
        ratio += subtract_delta ? -delta : delta;
        if (ratio < min_ratio)
            ratio = min_ratio;
        if (ratio > max_ratio)
            ratio = max_ratio;
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

float DrawHorizontalSplitter(
    const char* id, float x, float y, float width, float height, float hit_padding, ImGuiWindowFlags panel_flags) {
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
    bool item_active = ImGui::IsItemActive();

    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    ImVec2 ext_min(wp.x, wp.y - hit_padding);
    ImVec2 ext_max(wp.x + ws.x, wp.y + ws.y + hit_padding);

    bool extended_drag = TrackExtendedDrag(item_active, ext_min, ext_max, ImGuiMouseButton_Left);
    bool hovered = item_active || extended_drag || ImGui::IsMouseHoveringRect(ext_min, ext_max, false);

    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        float cy = wp.y + ws.y * 0.5f;
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(wp.x, cy), ImVec2(wp.x + ws.x, cy), ImGui::ColorConvertFloat4ToU32(SplitterHoverColor()), 2.0f);
    }

    bool core_drag = item_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f);
    if (core_drag || extended_drag) {
        delta_y = ImGui::GetIO().MouseDelta.y;
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    return delta_y;
}

} // namespace ui::widgets
