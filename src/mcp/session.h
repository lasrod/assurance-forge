#pragma once

// One MCP connection's view of an Assurance Forge project.
//
// The session opens the project READ-ONLY. It never constructs a
// core::commands::CommandBus, never writes SACM XML, and never touches the audit
// store. That is a design constraint rather than an unfinished edge: the MCP
// write surface is ReviewProposal drafts a human accepts in the GUI, so the SACM
// file keeps exactly one writer and the dual-writer problem does not arise.
// See docs/features/mcp-server.md.

#include "core/app_state.h"

#include <filesystem>
#include <memory>
#include <string>

namespace mcp {

class Session {
  public:
    struct Config {
        // A project directory, a project manifest, or a bare SACM file.
        std::filesystem::path project_path;
        // Empty resolves to core::UserSettingsFilePath(). Tests point this at a
        // temporary file so a developer's real consent setting cannot make a
        // test pass that would fail on a clean machine.
        std::filesystem::path settings_path;
    };

    static std::unique_ptr<Session> Open(Config config, std::string& error);

    bool                         has_case() const { return state_.loaded_case.has_value(); }
    const parser::AssuranceCase& assurance_case() const { return state_.loaded_case.value(); }
    const core::AppState&        state() const { return state_; }

    // True when the user has enabled the MCP server in settings.json. Every tool
    // that returns assurance-case content is refused until this is true.
    bool consent_granted() const { return consent_granted_; }

    // MCP requires `initialize` before any other request. Tracking it here keeps
    // the ordering rule enforceable in one place.
    bool initialized() const { return initialized_; }
    void mark_initialized() { initialized_ = true; }

    // Client name and version from the initialize handshake, used to attribute
    // anything this session produces.
    void               set_client_label(std::string label) { client_label_ = std::move(label); }
    const std::string& client_label() const { return client_label_; }

    const std::filesystem::path& project_path() const { return project_path_; }

  private:
    Session() = default;

    core::AppState        state_;
    std::filesystem::path project_path_;
    bool                  consent_granted_ = false;
    bool                  initialized_     = false;
    std::string           client_label_;
};

// Reads the `mcp.enabled` flag from a settings document. A missing file, a
// missing section, a non-boolean value, and a malformed document all read as
// false: a consent gate must fail closed, because the failure mode in the other
// direction is publishing a safety argument to a client the user never approved.
bool ReadMcpConsent(const std::filesystem::path& settings_path);

} // namespace mcp
