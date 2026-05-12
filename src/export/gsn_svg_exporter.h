#pragma once

#include "parser/xml_parser.h"

#include <filesystem>
#include <string>
#include <vector>

namespace export_gsn {

enum class GsnNodeKind {
    Goal,
    Strategy,
    Solution,
    Context,
    Assumption,
    Justification,
};

enum class GsnEdgeKind {
    SupportedBy,
    InContextOf,
};

struct GsnNode {
    std::string id;
    std::string source_gid;
    GsnNodeKind kind = GsnNodeKind::Goal;
    std::string text;

    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

struct GsnEdge {
    std::string id;
    std::string from_id;
    std::string to_id;
    GsnEdgeKind kind = GsnEdgeKind::SupportedBy;
};

struct GsnDiagram {
    std::vector<GsnNode> nodes;
    std::vector<GsnEdge> edges;
};

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
