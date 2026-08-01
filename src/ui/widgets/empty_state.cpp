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

void EmptyState(const std::string& message) {
    if (message.empty())
        return;

    const ImVec2 available = ImGui::GetContentRegionAvail();
    // Wrap before centring so a long sentence in a narrow panel measures at the
    // width it will actually occupy rather than as one very long line.
    const float wrap_width = std::max(120.0f, available.x * 0.8f);
    const ImVec2 text_size = ImGui::CalcTextSize(message.c_str(), nullptr, false, wrap_width);

    const float offset_y = std::max(0.0f, (available.y - text_size.y) * kVerticalBias);
    const float offset_x = std::max(0.0f, (available.x - text_size.x) * 0.5f);
    ImGui::Dummy(ImVec2(0.0f, offset_y));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(GetTheme().text_muted));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
    ImGui::TextUnformatted(message.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

} // namespace ui::widgets
