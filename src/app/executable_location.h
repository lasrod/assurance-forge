#pragma once

#include <filesystem>

namespace app {

// The directory the running executable lives in: where the build and every
// package put the files that ship beside it (the MCP server, the bundled
// example project). Falls back to the working directory only when the platform
// cannot say; empty if even that fails.
std::filesystem::path ExecutableDirectory();

} // namespace app
