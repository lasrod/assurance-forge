#include "ui/widgets/panel_header.h"

#include "ui/fonts.h"
#include "ui/theme.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace ui::widgets {

void PanelHeader(const char* icon, std::string_view title) {
    const Theme& theme = GetTheme();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Anchored to the cursor, and taking part in layout, rather than painted at
    // the window's fixed origin.
    //
    // It used to draw from `GetWindowPos()` and then finish with
    // `SetCursorScreenPos` to a screen Y computed from that same fixed origin.
    // `GetWindowPos()` does not move with scroll, so the panel body was re-pinned
    // to the same screen position on every frame: the scrollbar appeared,
    // tracked, and moved nothing. That is exactly what "the scrollbar does not
    // work" looked like in Element Properties -- the one panel long enough to
    // overflow and not wrapping its body in a scrolling child, which is what hid
    // the same bug everywhere else.
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float window_x = ImGui::GetWindowPos().x;
    const float window_width = ImGui::GetWindowSize().x;

    // Snap the small header geometry to whole framebuffer pixels. At fractional
    // DPI coordinates the icon tile and the 1px rule otherwise look subtly
    // offset even though their mathematical centres match.
    const float header_height = std::round(ImGui::GetFrameHeight() + 10.0f);
    const float icon_size = std::round(ImGui::GetFontSize() + 8.0f);
    const float icon_x = std::round(origin.x);
    const float icon_y = std::round(origin.y + (header_height - icon_size) * 0.5f);
    const float rule_y = std::round(origin.y + header_height) - 1.0f;

    // Spans the full window width, so the rule still reaches both edges even
    // though the cursor sits inside the window padding.
    draw_list->AddRectFilled(
        ImVec2(window_x, origin.y), ImVec2(window_x + window_width, origin.y + header_height), theme.surface_1);
    draw_list->AddLine(ImVec2(window_x, rule_y), ImVec2(window_x + window_width, rule_y), theme.border, 1.0f);
    draw_list->AddRectFilled(ImVec2(icon_x, icon_y),
                             ImVec2(icon_x + icon_size, icon_y + icon_size),
                             WithAlpha(theme.accent, 0.16f),
                             theme.rounding_ui);

    const char* safe_icon = icon != nullptr ? icon : "";
    const ImVec2 icon_text_size = ImGui::CalcTextSize(safe_icon);
    draw_list->AddText(ImVec2(std::round(icon_x + (icon_size - icon_text_size.x) * 0.5f),
                              std::round(icon_y + (icon_size - icon_text_size.y) * 0.5f) - 1.0f),
                       theme.accent_hover,
                       safe_icon);

    {
        fonts::Scoped strong(fonts::Role::BodyStrong);
        const float text_y = std::round(origin.y + (header_height - ImGui::GetTextLineHeight()) * 0.5f);
        draw_list->AddText(
            ImVec2(icon_x + icon_size + 9.0f, text_y), theme.text_primary, title.data(), title.data() + title.size());
    }

    // Reserves the header's height as a real layout item. `Dummy` adds
    // `ItemSpacing.y` after it, which is the gap the old absolute calculation
    // added by hand.
    ImGui::Dummy(ImVec2(0.0f, header_height));
}

} // namespace ui::widgets
