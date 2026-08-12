#pragma once

#include "parser/xml_parser.h"

#include <filesystem>
#include <string>
#include <vector>

namespace export_gsn {

struct GsnSvgExportResult {
    bool success = false;
    std::filesystem::path output_path;
    std::vector<std::string> warnings;
    std::string error_message;
};

std::filesystem::path EnsureExportsFolder(const std::filesystem::path& project_root, std::string& error_message);
std::filesystem::path MakeGsnSvgExportPath(const std::filesystem::path& exports_dir,
                                           const std::string& source_file_stem);

// `secondary_language` selects the language of the exported text: empty means
// the primary, a language code (e.g. "ja") means that language with a
// per-field fallback to the primary. The application passes the language the
// canvas is currently showing, so the exported diagram says what the screen
// says.
GsnSvgExportResult ExportCurrentSafetyCaseToGsnSvg(const parser::AssuranceCase& model,
                                                   const std::filesystem::path& project_root,
                                                   const std::string& source_file_stem,
                                                   const std::string& secondary_language = std::string());

} // namespace export_gsn
