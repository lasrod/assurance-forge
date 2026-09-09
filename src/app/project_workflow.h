#pragma once

#include "app/recent_projects.h"
#include "core/app_state.h"
#include "core/project_model.h"

#include <filesystem>
#include <string>

namespace app {

// `ImportedSacm` copies an existing SACM file the user has already picked;
// the dialog only asks what to call the copy.
enum class ProjectFileCreateKind { Sacm, EvidenceRegister, J3377CaeRegister, ImportedSacm };

// The stable, untranslated ImGui id for the file-create dialog of `kind`.
const char* ProjectFileCreateTitle(ProjectFileCreateKind kind);
// The same title translated for display. Spelled out per kind with literal
// msgids so the catalog extractor sees every one; `AF_TR(ProjectFileCreateTitle(kind))`
// would translate at runtime but be invisible to the tooling that keeps the
// catalog complete.
std::string TranslatedProjectFileCreateTitle(ProjectFileCreateKind kind);
std::filesystem::path ReviewItemsPath(const core::AssuranceProject& project);
std::filesystem::path ReviewProposalRelativePath(const std::string& proposal_id);
bool ProjectTracksFile(const core::AssuranceProject& project, const std::filesystem::path& relative_path);
bool IsProjectManifestPath(const std::filesystem::path& path);
RecentProjectEntry MakeRecentProjectEntry(const core::AppState& app_state);

} // namespace app
