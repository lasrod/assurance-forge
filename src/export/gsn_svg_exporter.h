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

GsnSvgExportResult ExportCurrentSafetyCaseToGsnSvg(const parser::AssuranceCase& model,
                                                   const std::filesystem::path& project_root,
                                                   const std::string& source_file_stem);

} // namespace export_gsn
