#pragma once

#include <filesystem>
#include <string>

namespace app::dialogs {

enum class DialogResult { Selected, Cancelled, Failed };

DialogResult
BrowseForProjectParentFolder(const std::string& default_path, std::string& selected_path, std::string& error_message);

DialogResult
BrowseForProjectManifest(const std::string& default_path, std::string& selected_path, std::string& error_message);

bool RevealPathInFileExplorer(const std::filesystem::path& path, std::string& error_message);

} // namespace app::dialogs