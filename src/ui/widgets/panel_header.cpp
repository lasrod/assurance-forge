#include "ui/widgets/panel_header.h"

#include "ui/fonts.h"
#include "ui/theme.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace ui::widgets {

void PanelHeader(const char* icon, std::string_view title) {
    const Theme& theme = GetTheme();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 window_position = ImGui::GetWindowPos();
    const ImVec2 window_size = ImGui::GetWindowSize();
    // Snap the small header geometry to whole framebuffer pixels. At fractional
    // DPI coordinates the icon tile and the 1px rule otherwise look subtly
    // offset even though their mathematical centres match.
    const float header_height = std::round(ImGui::GetFrameHeight() + 10.0f);
    const float icon_size = std::round(ImGui::GetFontSize() + 8.0f);
    const float icon_x = std::round(window_position.x + style.WindowPadding.x);
    const float icon_y = std::round(window_position.y + (header_height - icon_size) * 0.5f);
    const float rule_y = std::round(window_position.y + header_height) - 1.0f;

    draw_list->AddRectFilled(
        window_position, ImVec2(window_position.x + window_size.x, window_position.y + header_height), theme.surface_1);
    draw_list->AddLine(
        ImVec2(window_position.x, rule_y), ImVec2(window_position.x + window_size.x, rule_y), theme.border, 1.0f);
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
        const float text_y = std::round(window_position.y + (header_height - ImGui::GetTextLineHeight()) * 0.5f);
        draw_list->AddText(
            ImVec2(icon_x + icon_size + 9.0f, text_y), theme.text_primary, title.data(), title.data() + title.size());
    }

    const float content_y = window_position.y + header_height + style.ItemSpacing.y;
    ImGui::SetCursorScreenPos(
        ImVec2(window_position.x + style.WindowPadding.x, std::max(ImGui::GetCursorScreenPos().y, content_y)));
}

} // namespace ui::widgets
