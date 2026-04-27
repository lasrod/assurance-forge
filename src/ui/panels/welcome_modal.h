#pragma once

#include <string>
#include <vector>
#include <functional>

namespace ui::panels {

struct RecentProjectEntry {
    std::string name;
    std::string path;
    int claims = 0;
    int strategies = 0;
    int evidence = 0;
    int undeveloped = 0;
};
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
