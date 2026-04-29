#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace app {

struct RecentProjectEntry {
    std::string name;
    std::string path;
    int claims = 0;
    int strategies = 0;
    int evidence = 0;
    int undeveloped = 0;
};

constexpr std::size_t kMaxRecentProjects = 5;

std::vector<RecentProjectEntry> LoadRecentProjectsPreference(const std::string& content);
std::string SaveRecentProjectsPreference(const std::vector<RecentProjectEntry>& recent_projects);

void TouchRecentProject(std::vector<RecentProjectEntry>& recent_projects,
                        RecentProjectEntry entry);
void RemoveRecentProject(std::vector<RecentProjectEntry>& recent_projects,
                         const std::string& path);

std::string NormalizeRecentProjectPath(const std::string& path);

}  // namespace app
