#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ui::panels {

// What the welcome modal renders for one recent project.
//
// Deliberately not `app::RecentProjectEntry`. A panel that names an `app` type
// in its own interface makes `ui` depend on the layer above it, which is the
// dependency the layer gate exists to prevent -- this was one of its two
// recorded exceptions. The panel owns the shape it draws, and `app` maps its
// own type onto it, the same way the MCP fields on the preferences panel are
// passed as plain data rather than by reaching into `mcp`.
struct RecentProjectEntry {
    std::string name;
    std::string path;
    int claims = 0;
    int strategies = 0;
    int evidence = 0;
    int undeveloped = 0;
};

struct WelcomeModalCallbacks {
    // Left empty when no example is bundled with this build; the action is then
    // not offered rather than offered and failing.
    std::function<void()> open_example_project;
    std::function<void()> create_empty_project;
    std::function<void()> open_project;
    std::function<void()> create_project_from_sacm;
    // Each opens a user-guide page in the browser.
    std::function<void()> open_guide_get_started;
    std::function<void()> open_guide_ai_review;
    std::function<void()> open_guide_ai_client;
    std::function<void(const RecentProjectEntry&)> open_recent_project;
};

void ShowWelcomeModal(bool& is_open,
                      const std::vector<RecentProjectEntry>& recent,
                      const WelcomeModalCallbacks& callbacks = {});

} // namespace ui::panels
