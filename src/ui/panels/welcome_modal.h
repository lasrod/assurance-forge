#pragma once

#include "app/recent_projects.h"

#include <string>
#include <vector>
#include <functional>

namespace ui::panels {

using RecentProjectEntry = app::RecentProjectEntry;

struct WelcomeModalCallbacks {
    std::function<void()> create_empty_project;
    std::function<void()> create_project_from_template;
    std::function<void()> open_project;
    std::function<void()> import_sacm;
    std::function<void(const RecentProjectEntry&)> open_recent_project;
};

void ShowWelcomeModal(bool& is_open,
                      const std::vector<RecentProjectEntry>& recent,
                      const WelcomeModalCallbacks& callbacks = {});

}  // namespace ui::panels
