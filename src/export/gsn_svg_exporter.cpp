#include "export/gsn_svg_exporter.h"

#include "core/string_utils.h"
#include "export/gsn_projection.h"
#include "export/gsn_svg_layout.h"
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

namespace {

// The scheme of `value` if it names one ("https", "file", ...), lower-cased;
// empty when it carries none. A Windows drive letter (`C:\...`) is a path, not
// a one-letter scheme, so a single character never counts.
std::string SchemeOf(const std::string& value) {
    const std::size_t colon = value.find(':');
    if (colon == std::string::npos || colon < 2)
        return {};
    std::string scheme = value.substr(0, colon);
    for (char& c : scheme) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'))
            return {};
    }
    return scheme;
}

bool IsSafeScheme(const std::string& scheme) {
    return scheme == "http" || scheme == "https" || scheme == "file" || scheme == "mailto";
}

// Percent-encodes what a browser would otherwise read as URL syntax. Only the
// characters that change the meaning of a path: a space ends an attribute-free
// URL, and #/?/% start a fragment, a query and an escape.
std::string PercentEncodePath(const std::string& path) {
    std::string encoded;
    encoded.reserve(path.size());
    for (const unsigned char c : path) {
        switch (c) {
        case ' ':
            encoded += "%20";
            break;
        case '#':
            encoded += "%23";
            break;
        case '?':
            encoded += "%3F";
            break;
        case '%':
            encoded += "%25";
            break;
        default:
            encoded += static_cast<char>(c);
            break;
        }
    }
    return encoded;
}

} // namespace

std::string LinkTargetForExport(const std::string& location,
                                const std::filesystem::path& project_root,
                                const std::filesystem::path& exports_dir) {
    if (location.empty())
        return {};

    const std::string scheme = SchemeOf(location);
    if (!scheme.empty())
        return IsSafeScheme(scheme) ? location : std::string{};

    const std::filesystem::path recorded = core::PathFromUtf8(location);
    std::error_code ec;
    if (recorded.is_absolute()) {
        const std::string generic = recorded.generic_string();
        // file:///C:/... on Windows, file:///srv/... elsewhere: the leading
        // slash of a POSIX path is the URL's own, not a second one.
        return "file:///" + PercentEncodePath(generic.front() == '/' ? generic.substr(1) : generic);
    }
    if (project_root.empty() || exports_dir.empty())
        return PercentEncodePath(recorded.generic_string());

    const std::filesystem::path absolute = std::filesystem::weakly_canonical(project_root / recorded, ec);
    const std::filesystem::path base = std::filesystem::weakly_canonical(exports_dir, ec);
    if (ec)
        return PercentEncodePath(recorded.generic_string());
    const std::filesystem::path relative = absolute.lexically_relative(base);
    if (relative.empty())
        return PercentEncodePath(recorded.generic_string());
    return PercentEncodePath(relative.generic_string());
}

GsnSvgExportResult ExportCurrentSafetyCaseToGsnSvg(const parser::AssuranceCase& model,
                                                   const std::filesystem::path& project_root,
                                                   const std::string& source_file_stem,
                                                   const std::string& secondary_language) {
    GsnSvgExportResult result;

    std::string error;
    const std::filesystem::path exports_dir = EnsureExportsFolder(project_root, error);
    if (exports_dir.empty()) {
        result.error_message = error.empty() ? "Could not create exports folder." : error;
        return result;
    }

    GsnProjectionResult projection = BuildGsnProjection(model, secondary_language);
    AppendWarnings(result.warnings, projection.warnings);
    if (projection.diagram.nodes.empty()) {
        result.error_message = "No exportable GSN elements were found.";
        return result;
    }

    GsnSvgLayoutResult layout = LayoutGsnSvgDiagram(projection.diagram);
    AppendWarnings(result.warnings, layout.warnings);

    // The recorded location is what the register holds; what the SVG can follow
    // depends on where the SVG is written, which only this function knows.
    for (GsnNode& node : projection.diagram.nodes) {
        if (node.location.empty())
            continue;
        const std::string target = LinkTargetForExport(node.location, project_root, exports_dir);
        if (target.empty()) {
            result.warnings.push_back("Node '" + node.id +
                                      "' records a location the export will not link to: " + node.location);
        }
        node.location = target;
    }

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
