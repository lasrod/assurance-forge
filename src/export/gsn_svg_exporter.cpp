#include "export/gsn_svg_exporter.h"

#include "core/string_utils.h"
#include "export/gsn_layout.h"
#include "export/gsn_projection.h"
#include "export/svg_writer.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace export_gsn {
namespace {

std::string CleanFileStem(const std::string& stem) {
    std::string clean = core::TrimWhitespace(stem);
    if (clean.empty())
        clean = "safety_case";
    for (char& ch : clean) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (!std::isalnum(uch) && ch != '-' && ch != '_')
            ch = '_';
    }
    return clean;
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& content, std::string& error_message) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        error_message = "Could not write " + path.string();
        return false;
    }
    file << content;
    if (!file.good()) {
        error_message = "Could not finish writing " + path.string();
        return false;
    }
    return true;
}

void AppendWarnings(std::vector<std::string>& target, const std::vector<std::string>& source) {
    target.insert(target.end(), source.begin(), source.end());
}

} // namespace

std::filesystem::path EnsureExportsFolder(const std::filesystem::path& project_root, std::string& error_message) {
    error_message.clear();
    if (project_root.empty()) {
        error_message = "Project root cannot be determined.";
        return {};
    }

    std::filesystem::path exports_dir = project_root / "exports";
    std::error_code ec;
    if (!std::filesystem::create_directories(exports_dir, ec) && ec) {
        error_message = "Could not create exports folder: " + exports_dir.string();
        return {};
    }
    if (!std::filesystem::is_directory(exports_dir, ec)) {
        error_message = "Exports path is not a folder: " + exports_dir.string();
        return {};
    }
    return exports_dir;
}

std::filesystem::path MakeGsnSvgExportPath(const std::filesystem::path& exports_dir,
                                           const std::string& source_file_stem) {
    const std::string base_name = CleanFileStem(source_file_stem) + "_gsn";
    std::filesystem::path candidate = exports_dir / (base_name + ".svg");
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec))
        return candidate;

    for (int i = 1; i <= 999; ++i) {
        std::ostringstream suffix;
        suffix << "_" << std::setw(3) << std::setfill('0') << i;
        candidate = exports_dir / (base_name + suffix.str() + ".svg");
        if (!std::filesystem::exists(candidate, ec))
            return candidate;
    }

    candidate = exports_dir / (base_name + "_overflow.svg");
    return candidate;
}

GsnSvgExportResult ExportCurrentSafetyCaseToGsnSvg(const parser::AssuranceCase& model,
                                                   const std::filesystem::path& project_root,
                                                   const std::string& source_file_stem) {
    GsnSvgExportResult result;

    std::string error;
    const std::filesystem::path exports_dir = EnsureExportsFolder(project_root, error);
    if (exports_dir.empty()) {
        result.error_message = error.empty() ? "Could not create exports folder." : error;
        return result;
    }

    GsnProjectionResult projection = BuildGsnProjection(model);
    AppendWarnings(result.warnings, projection.warnings);
    if (projection.diagram.nodes.empty()) {
        result.error_message = "No exportable GSN elements were found.";
        return result;
    }

    GsnLayoutResult layout = LayoutGsnDiagram(projection.diagram);
    AppendWarnings(result.warnings, layout.warnings);

    const std::string svg = GenerateGsnSvg(projection.diagram);
    if (svg.empty()) {
        result.error_message = "SVG generation produced no output.";
        return result;
    }

    const std::filesystem::path output_path = MakeGsnSvgExportPath(exports_dir, source_file_stem);
    if (!WriteTextFile(output_path, svg, error)) {
        result.error_message = error;
        return result;
    }

    result.success = true;
    result.output_path = output_path;
    return result;
}

} // namespace export_gsn
