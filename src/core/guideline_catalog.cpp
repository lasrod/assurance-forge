#include "core/guideline_catalog.h"

#include <cstdlib>

#include "parser/sccg_dist_parser.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <filesystem>
#include <format>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace core {
namespace {

std::filesystem::path ExecutableDirectory() {
#ifdef _WIN32
    char path[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(path).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

// An explicit SCCG distribution, when one is named.
//
// SCCG is a versioned dependency, and an evaluation that compares two versions
// -- or one that asks what a proposed catalogue change would do -- has to be
// able to point the tool at a distribution other than the one beside the
// executable. Discovery alone cannot express that.
//
// Opt-in and silent when unset, so a normal run behaves exactly as before. It is
// not a hidden switch: every review record states the `sccg_catalog_path` it
// actually loaded, so a run against a substituted catalogue says so in its own
// output rather than looking like a run against the shipped one.
//
// Authoritative when set: returned whether or not it names a directory, and
// LoadGuidelineCatalog refuses one that does not. Falling back to discovery
// ran an evaluation meant for another catalogue against the shipped one.
std::filesystem::path SccgDistDirectoryOverride() {
    const char* configured = std::getenv("AF_SCCG_DIST_DIR");
    if (configured == nullptr || *configured == '\0')
        return {};
    return std::filesystem::path(configured);
}

} // namespace

// The directory holding `sccg.full.json`, the one file SCCG declares
// sufficient for a tool (contract 3.1.0). An explicit AF_SCCG_DIST_DIR wins;
// otherwise the copy beside the executable, then a source checkout.
std::filesystem::path FindSccgDistDirectory() {
    if (std::filesystem::path configured = SccgDistDirectoryOverride(); !configured.empty())
        return configured;
    const std::filesystem::path executable_dir = ExecutableDirectory();
    const std::filesystem::path current_dir = std::filesystem::current_path();
    const std::vector<std::filesystem::path> candidates = {
        executable_dir / "data" / "sccg" / "dist",
        current_dir / "data" / "sccg" / "dist",
        current_dir / "external" / "safety-case-core-guidelines" / "dist",
        current_dir.parent_path() / "external" / "safety-case-core-guidelines" / "dist",
    };

    for (const std::filesystem::path& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::exists(candidate / parser::SccgDistParser::kCatalogFileName, error))
            return candidate;
    }
    return {};
}

GuidelineCatalog BuildGuidelineCatalog(parser::GuidelinesDocument document, std::filesystem::path source_path) {
    GuidelineCatalog catalog;
    catalog.source_path = std::move(source_path);
    catalog.document = std::move(document);
    for (const parser::Guideline& guideline : catalog.document.guidelines) {
        if (guideline.id.empty())
            continue;
        catalog.entries.push_back(GuidelineCatalogEntry{
            guideline.id,
            guideline.category,
            guideline.title,
        });
        catalog.ids.insert(guideline.id);
    }
    for (const parser::ReviewProfile& profile : catalog.document.review_profiles) {
        if (profile.id.empty())
            continue;
        catalog.review_profile_entries.push_back(ReviewProfileCatalogEntry{
            profile.id,
            profile.display_name,
            profile.description,
            profile.applies_to,
            profile.required_data,
            profile.optional_data,
        });
        catalog.review_profile_ids.insert(profile.id);
    }
    return catalog;
}

bool LoadGuidelineCatalog(GuidelineCatalog& catalog, std::string& error) {
    catalog = {};
    error.clear();

    const std::filesystem::path dist_dir = FindSccgDistDirectory();
    if (!SccgDistDirectoryOverride().empty()) {
        std::error_code ignored;
        if (!std::filesystem::is_directory(dist_dir, ignored)) {
            error = std::format(
                "AF_SCCG_DIST_DIR names {}, which is not a directory; the shipped SCCG catalogue is not loaded "
                "in its place.",
                dist_dir.string());
            return false;
        }
    }
    if (dist_dir.empty()) {
        error = std::string("SCCG catalogue could not be found: no ") + parser::SccgDistParser::kCatalogFileName +
                " beside the executable or in a source checkout.";
        return false;
    }
    auto result = parser::SccgDistParser::ParseDirectory(dist_dir);
    if (!result) {
        error = "SCCG catalogue could not be parsed: " + std::move(result.error());
        return false;
    }

    catalog = BuildGuidelineCatalog(std::move(*result), dist_dir);
    if (catalog.entries.empty()) {
        error = "No SCCG guidelines were found in the catalogue.";
        return false;
    }
    if (catalog.review_profile_entries.empty()) {
        error = "No SCCG review profiles were found in the catalogue.";
        return false;
    }
    return true;
}

} // namespace core