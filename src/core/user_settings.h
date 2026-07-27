#pragma once

// Location of the per-user settings file shared by every Assurance Forge
// component that needs it.
//
// This exists because two layers now need the path and cannot see each other:
// `ai::AiSettingsStore` owns the `"ai"` section, and the MCP server reads the
// `"mcp"` section for its consent gate, but the layer gate forbids `mcp/` from
// including `ai/` (the two AI features must not share machinery -- see
// docs/features/mcp-server.md). Rather than let each layer carry its own copy of
// the platform path rules and drift, both resolve it here.
//
// `tests/test_user_settings_path.cpp` pins this against
// `ai::AiSettingsStore::DefaultSettingsPath()`, so the two cannot diverge
// silently. A divergence points the consent gate at a file nobody writes, and it
// then reads `false` forever: the gate fails closed, so nothing leaks, but MCP
// quietly stops working with no diagnostic. The worse direction is a gate left
// pointing at a stale file that still says `true` after the user revoked
// permission. Neither is visible without this pin.

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace core {

// Windows: %APPDATA%/AssuranceForge/settings.json
// POSIX:   $XDG_CONFIG_HOME/assurance-forge/settings.json, else
//          $HOME/.config/assurance-forge/settings.json
// Fallback: <temp>/AssuranceForge/settings.json
std::filesystem::path UserSettingsFilePath();

// One settings file now holds several independent sections -- `"ai"` and
// `"mcp"`, with more likely -- written by components that cannot see each other.
// Anything that writes it must therefore read-modify-write.
//
// This is not hypothetical tidiness. `AiSettingsStore::Save` used to build a
// fresh document containing only `"ai"` and truncate the file, so saving any AI
// preference silently deleted the `"mcp"` section and turned the MCP server off.
// The user got no diagnostic: their AI client simply started refusing with
// "not been given permission". Every writer goes through the helper below so
// that cannot happen again.

// Returns the named top-level section, or a null json when the file is missing,
// unreadable, malformed, or has no such section. Comments are tolerated, since
// the file is hand-edited.
nlohmann::json ReadUserSettingsSection(const std::filesystem::path& path, const std::string& section);

// Sets one top-level section, preserving every sibling section already in the
// file. Creates the file and its directory when absent. A malformed existing
// document is replaced rather than propagated -- there is nothing to preserve in
// something that cannot be parsed, and refusing to save would leave the user
// unable to fix it from the UI.
bool UpdateUserSettingsSection(const std::filesystem::path& path, const std::string& section,
                               const nlohmann::json& value, std::string& error);

// The `"mcp"` section: whether the user has allowed an external AI client to
// read this machine's assurance cases over the MCP server.
//
// Typed here rather than left as raw JSON at each call site because two layers
// that cannot see each other both need it -- `mcp/` reads it as a consent gate,
// `app/` writes it from Preferences -- and a disagreement about the shape would
// be a disagreement about whether the user granted permission.
struct McpUserSettings {
    bool enabled = false;
};

// Every failure reads as `enabled = false`. A consent gate must fail closed: the
// failure in the other direction is publishing a safety argument to a client the
// user never approved.
McpUserSettings LoadMcpUserSettings(const std::filesystem::path& path);

bool SaveMcpUserSettings(const std::filesystem::path& path, const McpUserSettings& settings,
                         std::string& error);

} // namespace core
