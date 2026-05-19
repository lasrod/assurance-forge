#pragma once

#include "core/project_model.h"

#include <filesystem>
#include <string>

namespace core {

// Serialize the AssuranceProject manifest to canonical af.proj JSON text.
std::string SerializeManifest(const AssuranceProject& project);

// Parse af.proj JSON text into an AssuranceProject. `root_path` becomes the
// project's `rootPath`. Returns false and populates `error` on failure.
bool ParseManifest(const std::string& text,
                   const std::filesystem::path& root_path,
                   AssuranceProject& project,
                   std::string& error);

} // namespace core
