#include "app/guideline_catalog.h"

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
#include <string>
#include <utility>
#include <vector>

namespace app {
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

}  // namespace

std::filesystem::path FindGuidelinesFile() {
    const std::filesystem::path executable_dir = ExecutableDirectory();
    const std::filesystem::path current_dir = std::filesystem::current_path();
    const std::vector<std::filesystem::path> candidates = {
        executable_dir / "data" / "guidelines.yaml",
        current_dir / "data" / "guidelines.yaml",
        current_dir / "external" / "safety-case-core-guidelines" / "data" / "guidelines.yaml",
        current_dir.parent_path() / "external" / "safety-case-core-guidelines" / "data" / "guidelines.yaml",
    };

    for (const std::filesystem::path& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::exists(candidate, error)) return candidate;
    }
    return {};
}

GuidelineCatalog BuildGuidelineCatalog(parser::GuidelinesDocument document,
                                       std::filesystem::path source_path) {
    GuidelineCatalog catalog;
    catalog.source_path = std::move(source_path);
    catalog.document = std::move(document);
    for (const parser::Guideline& guideline : catalog.document.guidelines) {
        if (guideline.id.empty()) continue;
        catalog.entries.push_back(GuidelineCatalogEntry{
            guideline.id,
            guideline.category,
            guideline.title,
        });
        catalog.ids.insert(guideline.id);
    }
    return catalog;
}

bool LoadGuidelineCatalog(GuidelineCatalog& catalog, std::string& error) {
    catalog = {};
    error.clear();

    const std::filesystem::path guidelines_path = FindGuidelinesFile();
    if (guidelines_path.empty()) {
        error = "SCCG guidelines.yaml could not be found.";
        return false;
    }

    parser::GuidelinesParseResult result = parser::GuidelinesParser::ParseFile(guidelines_path.string());
    if (!result.success) {
        error = "SCCG guidelines could not be parsed: " + result.error_message;
        return false;
    }

    catalog = BuildGuidelineCatalog(std::move(result.document), guidelines_path);
    if (catalog.entries.empty()) {
        error = "No SCCG guidelines were found.";
        return false;
    }

    return true;
}

}  // namespace app