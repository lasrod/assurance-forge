#include "app/project_workflow.h"

#include "core/project_service.h"
#include "core/string_utils.h"

#include <algorithm>
#include <system_error>

namespace app {

const char* ProjectFileCreateTitle(ProjectFileCreateKind kind) {
    switch (kind) {
    case ProjectFileCreateKind::Sacm:
        return "New GSN / SACM File";
    case ProjectFileCreateKind::EvidenceRegister:
        return "New Evidence Register";
    case ProjectFileCreateKind::J3377CaeRegister:
        return "New J3377 CAE Register";
    case ProjectFileCreateKind::ImportedSacm:
        return "Import SACM File";
    }
    return "New Project File";
}

std::filesystem::path ReviewItemsPath(const core::AssuranceProject& project) {
    for (const core::ProjectFileEntry& entry : project.files) {
        if (entry.role == core::ProjectFileRole::ReviewItems)
            return project.rootPath / entry.relativePath;
    }
    return {};
}

std::filesystem::path ReviewProposalRelativePath(const std::string& proposal_id) {
    return std::filesystem::path("reviews") / "proposals" / (proposal_id + ".afpatch.json");
}

bool ProjectTracksFile(const core::AssuranceProject& project, const std::filesystem::path& relative_path) {
    const std::string normalized = relative_path.generic_string();
    return std::any_of(project.files.begin(), project.files.end(), [&](const core::ProjectFileEntry& entry) {
        return entry.relativePath.generic_string() == normalized;
    });
}

bool IsProjectManifestPath(const std::filesystem::path& path) {
    if (core::ToLower(path.filename().string()) == "af.proj")
        return true;

    std::error_code error;
    return std::filesystem::is_directory(path, error) && std::filesystem::exists(path / "af.proj", error);
}

RecentProjectEntry MakeRecentProjectEntry(const core::AppState& app_state) {
    RecentProjectEntry entry;
    if (!app_state.current_project.has_value())
        return entry;

    const core::AssuranceProject& project = app_state.current_project.value();
    entry.name = project.name;
    entry.path = core::PathToUtf8(core::ProjectService::ManifestPath(project));

    if (!app_state.loaded_case.has_value())
        return entry;
    for (const parser::SacmElement& element : app_state.loaded_case->elements) {
        const std::string type = core::ToLower(element.type);
        if (type == "claim") {
            ++entry.claims;
        } else if (type == "argumentreasoning") {
            ++entry.strategies;
        } else if (type == "artifact" || type == "artifactreference") {
            ++entry.evidence;
        }
        if (element.undeveloped)
            ++entry.undeveloped;
    }

    return entry;
}

} // namespace app
