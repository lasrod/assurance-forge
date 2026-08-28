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
// The href an exported SVG should carry for a recorded evidence `location`.
//
// Three things have to happen between what the register stores and what a
// reader can follow:
//   * A project-relative path is relative to the PROJECT ROOT, but the SVG is
//     written into `exports/`, and a browser resolves a relative href against
//     the document. It is rebased onto `exports_dir` (`evidence/r.pdf` becomes
//     `../evidence/r.pdf`), so the exported diagram keeps working when the
//     whole project is copied elsewhere.
//   * An absolute filesystem path becomes a `file:` URL, which is what a
//     browser follows; a bare `C:\...` in an href is not.
//   * Only http, https, file and mailto are emitted. An exported diagram is a
//     document people open and pass on, and a `javascript:` or `data:` href in
//     one would run when they did. Anything else yields an empty target, and
//     the caller reports it rather than linking.
//
// Returns the href, or empty when there is nothing safe to link to.
std::string LinkTargetForExport(const std::string& location,
                                const std::filesystem::path& project_root,
                                const std::filesystem::path& exports_dir);

GsnSvgExportResult ExportCurrentSafetyCaseToGsnSvg(const parser::AssuranceCase& model,
                                                   const std::filesystem::path& project_root,
                                                   const std::string& source_file_stem,
                                                   const std::string& secondary_language = std::string());

} // namespace export_gsn
