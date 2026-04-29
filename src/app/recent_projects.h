#pragma once

#include "ui/panels/welcome_modal.h"

#include <cstddef>
#include <string>
#include <vector>

namespace app {

constexpr std::size_t kMaxRecentProjects = 5;

std::vector<ui::panels::RecentProjectEntry> LoadRecentProjectsPreference(const std::string& content);
std::string SaveRecentProjectsPreference(const std::vector<ui::panels::RecentProjectEntry>& recent_projects);

void TouchRecentProject(std::vector<ui::panels::RecentProjectEntry>& recent_projects,
                        ui::panels::RecentProjectEntry entry);
void RemoveRecentProject(std::vector<ui::panels::RecentProjectEntry>& recent_projects,
                         const std::string& path);

std::string NormalizeRecentProjectPath(const std::string& path);

}  // namespace app
