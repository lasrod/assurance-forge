#include "ui/widgets/empty_state.h"

#include "ui/theme.h"

#include "imgui.h"

#include <algorithm>

namespace ui::widgets {
namespace {

// Sits above the true centre. Optically centred text in a tall region reads as
// slightly low when placed at exactly half, and this keeps the message clear of
// anything docked along the bottom edge.
constexpr float kVerticalBias = 0.38f;

} // namespace

namespace {

// Draws one centred, wrapped line. ImGui resets the cursor X to the window
// indent after every item, so the shift applies to this line only and cannot
// leak into whatever the caller draws next.
void CentredLine(const std::string& text, float available_width, float wrap_width, ImU32 color) {
    const ImVec2 size = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap_width);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (available_width - size.x) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

} // namespace

void EmptyState(const std::string& message, const std::string& detail) {
    if (message.empty())
        return;

    const Theme& theme = GetTheme();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    // Wrap before centring so a long sentence in a narrow panel measures at the
    // width it will actually occupy rather than as one very long line.
    const float wrap_width = std::max(120.0f, available.x * 0.8f);

    const ImVec2 message_size = ImGui::CalcTextSize(message.c_str(), nullptr, false, wrap_width);
    float block_height = message_size.y;
    if (!detail.empty()) {
        block_height +=
            ImGui::GetStyle().ItemSpacing.y + ImGui::CalcTextSize(detail.c_str(), nullptr, false, wrap_width).y;
    }

    ImGui::Dummy(ImVec2(0.0f, std::max(0.0f, (available.y - block_height) * kVerticalBias)));
    CentredLine(message, available.x, wrap_width, theme.text_muted);
    if (!detail.empty())
        CentredLine(detail, available.x, wrap_width, theme.text_muted);
}

} // namespace ui::widgets
