#pragma once

#include <string>
#include <vector>

// Metadata for a recently opened assurance project displayed on the welcome screen.
struct RecentProjectEntry {
    std::string name;         // Human-readable project name
    std::string path;         // File-system path shown under the name
    int         claims       = 0;
    int         strategies   = 0;
    int         evidence     = 0;
    int         undeveloped  = 0;
};

// Shows the welcome/startup modal dialog.
// Call this once per frame after ImGui::NewFrame() if the modal should be displayed.
// `recent` may be empty if no projects have been opened yet.
void ShowWelcomeModal(bool& is_open, const std::vector<RecentProjectEntry>& recent);
