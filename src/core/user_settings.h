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

#include <filesystem>

namespace core {

// Windows: %APPDATA%/AssuranceForge/settings.json
// POSIX:   $XDG_CONFIG_HOME/assurance-forge/settings.json, else
//          $HOME/.config/assurance-forge/settings.json
// Fallback: <temp>/AssuranceForge/settings.json
std::filesystem::path UserSettingsFilePath();

} // namespace core
