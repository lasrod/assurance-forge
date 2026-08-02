#pragma once

#include <string_view>

namespace ui::widgets {

// Draws the compact, full-width heading used by fixed shell panels. Call once,
// immediately after ImGui::Begin(), on a window created with NoTitleBar.
void PanelHeader(const char* icon, std::string_view title);

} // namespace ui::widgets
