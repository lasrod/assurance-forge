#include "ui/widgets/danger_button.h"

#include "ui/theme.h"

namespace ui::widgets {

bool DangerButton(const char* label, const ImVec2& size) {
    const Theme& theme = GetTheme();

    // Transparent until hovered, so the outline and the text colour carry the
    // meaning while at rest. ImGui applies the disabled alpha over these, so a
    // disabled danger button still reads as unavailable rather than as safe.
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(WithAlpha(theme.danger, 0.10f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(WithAlpha(theme.danger, 0.24f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(WithAlpha(theme.danger, 0.38f)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.danger));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(WithAlpha(theme.danger, 0.55f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    const bool clicked = ImGui::Button(label, size);

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);
    return clicked;
}

} // namespace ui::widgets
