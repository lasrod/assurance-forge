#pragma once

#include "imgui.h"
#include "ui/element_context_menu.h"

namespace app {

class AppRuntime;

inline constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                                ImGuiWindowFlags_NoSavedSettings;

ui::ElementContextActions MakeElementContextActions(AppRuntime& runtime);

std::string ReviewItemIdFromProblemId(const std::string& problem_id);

} // namespace app
