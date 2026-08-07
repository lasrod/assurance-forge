#include "app/recent_projects.h"

#include "core/string_utils.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <system_error>
#include <utility>

namespace app {
namespace {

std::string RecentProjectKey(const std::string& path) {
    std::string key = NormalizeRecentProjectPath(path);
#ifdef _WIN32
    key = core::ToLower(std::move(key));
#endif
    return key;
}

std::string DefaultProjectNameForPath(const std::string& path) {
    std::filesystem::path manifest_path = core::PathFromUtf8(path);
    if (manifest_path.has_parent_path()) {
        std::filesystem::path project_folder = manifest_path.parent_path().filename();
        if (!project_folder.empty())
            return core::PathToUtf8(project_folder);
    }
    return path;
}

int JsonInt(const nlohmann::json& object, const char* key) {
    auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_number_integer())
        return 0;
    return std::max(0, iterator->get<int>());
}

RecentProjectEntry EntryFromJson(const nlohmann::json& object) {
    RecentProjectEntry entry;
    if (!object.is_object())
        return entry;

    auto path_iterator = object.find("path");
    if (path_iterator == object.end() || !path_iterator->is_string())
        return entry;

    entry.path = NormalizeRecentProjectPath(path_iterator->get<std::string>());
    if (entry.path.empty())
        return entry;

    auto name_iterator = object.find("name");
    if (name_iterator != object.end() && name_iterator->is_string()) {
        entry.name = name_iterator->get<std::string>();
    }
    if (entry.name.empty())
        entry.name = DefaultProjectNameForPath(entry.path);

    entry.claims = JsonInt(object, "claims");
    entry.strategies = JsonInt(object, "strategies");
    entry.evidence = JsonInt(object, "evidence");
    entry.undeveloped = JsonInt(object, "undeveloped");
    return entry;
}

nlohmann::json EntryToJson(const RecentProjectEntry& entry) {
    return nlohmann::json{
        {"name", entry.name},
        {"path", entry.path},
        {"claims", entry.claims},
        {"strategies", entry.strategies},
        {"evidence", entry.evidence},
        {"undeveloped", entry.undeveloped},
    };
}

} // namespace

std::string NormalizeRecentProjectPath(const std::string& path) {
    if (path.empty())
        return {};

    std::filesystem::path raw_path = core::PathFromUtf8(path);
    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(raw_path, ec);
    if (ec || normalized.empty()) {
        ec.clear();
        normalized = std::filesystem::absolute(raw_path, ec);
    }
    if (ec || normalized.empty()) {
        normalized = raw_path;
    }
    return core::PathToUtf8(normalized.lexically_normal());
}

std::vector<RecentProjectEntry> LoadRecentProjectsPreference(const std::string& content) {
    std::vector<RecentProjectEntry> recent_projects;
    if (content.empty())
        return recent_projects;

    try {
        nlohmann::json root = nlohmann::json::parse(content, nullptr, true, true);
        nlohmann::json entries = root.is_object() ? root.value("recentProjects", nlohmann::json::array()) : root;
        if (!entries.is_array())
            return recent_projects;

        for (auto iterator = entries.rbegin(); iterator != entries.rend(); ++iterator) {
            TouchRecentProject(recent_projects, EntryFromJson(*iterator));
        }
    } catch (const std::exception&) {
        recent_projects.clear();
    }

    return recent_projects;
}

std::string SaveRecentProjectsPreference(const std::vector<RecentProjectEntry>& recent_projects) {
    std::vector<RecentProjectEntry> normalized;
    for (auto iterator = recent_projects.rbegin(); iterator != recent_projects.rend(); ++iterator) {
        TouchRecentProject(normalized, *iterator);
    }

    nlohmann::json entries = nlohmann::json::array();
    for (const RecentProjectEntry& entry : normalized) {
        entries.push_back(EntryToJson(entry));
    }

    nlohmann::json root;
    root["version"] = 1;
    root["recentProjects"] = std::move(entries);
    return root.dump(2);
}

void TouchRecentProject(std::vector<RecentProjectEntry>& recent_projects, RecentProjectEntry entry) {
    entry.path = NormalizeRecentProjectPath(entry.path);
    if (entry.path.empty())
        return;
    if (entry.name.empty())
        entry.name = DefaultProjectNameForPath(entry.path);

    const std::string key = RecentProjectKey(entry.path);
    std::erase_if(recent_projects,
                  [&](const RecentProjectEntry& existing) { return RecentProjectKey(existing.path) == key; });
    recent_projects.insert(recent_projects.begin(), std::move(entry));

    if (recent_projects.size() > kMaxRecentProjects) {
        recent_projects.resize(kMaxRecentProjects);
    }
}

void RemoveRecentProject(std::vector<RecentProjectEntry>& recent_projects, const std::string& path) {
    const std::string key = RecentProjectKey(path);
    if (key.empty())
        return;
    std::erase_if(recent_projects,
                  [&](const RecentProjectEntry& existing) { return RecentProjectKey(existing.path) == key; });
}

} // namespace app
