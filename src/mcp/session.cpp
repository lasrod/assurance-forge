#include "mcp/session.h"

#include "core/active_argument.h"
#include "core/user_settings.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <fstream>

namespace mcp {
namespace {

// Opening a project reads its manifest but does not load any of its documents,
// so a session pointed at a project directory -- the normal case -- would
// otherwise come up with no assurance case at all. Opens the project's argument
// file to match what the application does on open.
//
// Prefers the argument the application currently has open, recorded in the
// project's session sidecar, and falls back to the first one.
//
// Guessing "the first" meant an agent reasoning about `main.sacm` while the user
// edited `main2.sacm` and proposing changes against an argument they were not
// looking at -- which is indistinguishable, from the user's side, from the
// proposal never arriving.
bool OpenProjectArgumentFile(core::AppState& state) {
    if (!state.current_project.has_value()) {
        return false;
    }

    const std::string active = core::ReadActiveArgument(state.current_project->rootPath);
    const core::ProjectFileEntry* first = nullptr;
    for (const core::ProjectFileEntry& entry : state.current_project->files) {
        if (entry.role != core::ProjectFileRole::SacmArgument) {
            continue;
        }
        if (!active.empty() && entry.relativePath.generic_string() == active) {
            return state.open_project_file(entry);
        }
        if (first == nullptr) {
            first = &entry;
        }
    }
    return first != nullptr && state.open_project_file(*first);
}

// A path we should hand to AppState::open_project rather than load_file: a
// directory, or a manifest. Bare SACM files load standalone, which is useful for
// inspecting a file that is not part of a project, but yields no project root.
bool LooksLikeAssuranceCaseFile(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    for (char& character : extension) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return extension == ".sacm" || extension == ".xml";
}

} // namespace

bool ReadMcpConsent(const std::filesystem::path& settings_path) {
    // Shares the reader with the Preferences toggle that writes it, so the gate
    // and the switch can never disagree about what "enabled" means. Every failure
    // path -- missing file, unreadable, malformed, absent or non-boolean flag --
    // resolves to false.
    return core::LoadMcpUserSettings(settings_path).enabled;
}

bool Session::consent_granted() const {
    return ReadMcpConsent(settings_path_);
}

std::vector<core::ProjectFileEntry> Session::case_files() const {
    std::vector<core::ProjectFileEntry> files;
    if (!state_.current_project.has_value()) {
        return files;
    }
    for (const core::ProjectFileEntry& entry : state_.current_project->files) {
        if (entry.role == core::ProjectFileRole::SacmArgument) {
            files.push_back(entry);
        }
    }
    return files;
}

bool Session::OpenCaseFile(const std::string& relative_path, std::string& error) {
    error.clear();
    for (const core::ProjectFileEntry& entry : case_files()) {
        if (entry.relativePath.generic_string() != relative_path) {
            continue;
        }
        if (!state_.open_project_file(entry)) {
            error = state_.status_message.empty() ? ("Could not open " + relative_path)
                                                  : state_.status_message;
            return false;
        }
        // Proposal validity is evaluated against the loaded case, so the cache
        // must not survive a switch to a different argument.
        proposals_.InvalidateProposalCache();
        return true;
    }
    error = "No argument file in this project has the path \"" + relative_path + "\".";
    return false;
}

std::unique_ptr<Session> Session::Open(Config config, std::string& error) {
    error.clear();

    if (config.project_path.empty()) {
        error = "No project path was given. Pass --project <path>.";
        return nullptr;
    }

    std::error_code ec;
    if (!std::filesystem::exists(config.project_path, ec)) {
        error = "Project path does not exist: " + config.project_path.string();
        return nullptr;
    }

    std::unique_ptr<Session> session(new Session());
    session->project_path_ = config.project_path;
    session->settings_path_ =
        config.settings_path.empty() ? core::UserSettingsFilePath() : config.settings_path;

    const bool is_directory = std::filesystem::is_directory(config.project_path, ec);
    const bool opened = (is_directory || !LooksLikeAssuranceCaseFile(config.project_path))
                            ? session->state_.open_project(config.project_path.string())
                            : session->state_.load_file(config.project_path.string());

    if (!opened) {
        error = session->state_.status_message.empty()
                    ? ("Could not open " + config.project_path.string())
                    : session->state_.status_message;
        return nullptr;
    }

    if (session->state_.current_project.has_value()) {
        session->proposals_.SetProjectRoot(session->state_.current_project->rootPath);
        if (!session->state_.loaded_case.has_value() && !OpenProjectArgumentFile(session->state_)) {
            // Not fatal: the project may legitimately hold no argument yet, and a
            // session that can still list proposals is more useful than none. The
            // read tools report the absence rather than pretending to an empty case.
            session->state_.load_warnings.push_back(
                "The project's assurance case could not be opened: " +
                (session->state_.status_message.empty() ? std::string("no SACM argument file")
                                                        : session->state_.status_message));
        }
    }

    return session;
}

} // namespace mcp
