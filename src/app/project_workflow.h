#pragma once

#include "app/recent_projects.h"
#include "core/app_state.h"
#include "core/project_model.h"

#include <filesystem>
#include <string>

namespace app {

enum class ProjectFileCreateKind { Sacm, EvidenceRegister, J3377CaeRegister };

const char* ProjectFileCreateTitle(ProjectFileCreateKind kind);
std::filesystem::path ReviewItemsPath(const core::AssuranceProject& project);
std::filesystem::path ReviewProposalRelativePath(const std::string& proposal_id);
bool ProjectTracksFile(const core::AssuranceProject& project, const std::filesystem::path& relative_path);
bool IsProjectManifestPath(const std::filesystem::path& path);
RecentProjectEntry MakeRecentProjectEntry(const core::AppState& app_state);

} // namespace app
