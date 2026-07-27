#pragma once

// The configuration block a user pastes into their MCP client so it can launch
// Assurance Forge's MCP server against the open project.
//
// This exists so enabling the feature does not require hand-writing JSON that
// embeds two absolute paths. Getting either wrong produces a client that fails
// to start with an error the user cannot act on, which is the main thing
// standing between the MCP server being usable by its author and usable by
// anyone else.

#include <filesystem>
#include <string>

namespace app {

// Where the MCP server binary is expected to live: beside the running
// executable, which is how it is installed and how the build stages it.
std::filesystem::path McpServerExecutablePath();

// The client configuration for `project_root`, pretty-printed.
//
// Empty when the server binary is not beside the application, because a config
// naming a path that does not exist is worse than none: the client would accept
// it and fail later, somewhere the user is not looking.
std::string BuildMcpClientConfig(const std::filesystem::path& project_root);

} // namespace app
