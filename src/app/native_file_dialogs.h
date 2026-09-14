#pragma once

#include <filesystem>
#include <string>

namespace app::dialogs {

enum class DialogResult { Selected, Cancelled, Failed };

DialogResult
BrowseForProjectParentFolder(const std::string& default_path, std::string& selected_path, std::string& error_message);

DialogResult
BrowseForProjectManifest(const std::string& default_path, std::string& selected_path, std::string& error_message);

// Picks an existing SACM XML file to copy into a project, either as the first
// argument of a new project or as another argument of the open one.
DialogResult BrowseForSacmFile(const std::string& default_path, std::string& selected_path, std::string& error_message);

bool RevealPathInFileExplorer(const std::filesystem::path& path, std::string& error_message);

// Opens a file with its associated application, or a URL in the browser --
// what a piece of evidence's recorded location points at. `target` is a URL
// (has a scheme) or an absolute filesystem path; relative paths are the
// caller's to resolve, because only the caller knows the project root.
bool OpenPathOrUrl(const std::string& target, std::string& error_message);

// Picks any file, for recording where a piece of evidence is. `default_path`
// is where the picker opens (a folder, or a file whose folder is used).
DialogResult
BrowseForEvidenceFile(const std::string& default_path, std::string& selected_path, std::string& error_message);

} // namespace app::dialogs