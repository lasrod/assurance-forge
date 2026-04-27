#pragma once

#include <string>
#include <vector>

namespace ui::panels {

struct RecentProjectEntry {
    std::string name;
    std::string path;
    int claims = 0;
    int strategies = 0;
    int evidence = 0;
    int undeveloped = 0;
};

void ShowWelcomeModal(bool& is_open, const std::vector<RecentProjectEntry>& recent);

}  // namespace ui::panels
