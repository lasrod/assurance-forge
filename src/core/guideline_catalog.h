#pragma once

#include "parser/guidelines_parser.h"

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace core {

struct GuidelineCatalogEntry {
    std::string id;
    std::string category;
    std::string title;
};

struct ReviewProfileCatalogEntry {
    std::string id;
    std::string display_name;
    std::string description;
    std::vector<std::string> applies_to;
    std::vector<std::string> required_data;
    std::vector<std::string> optional_data;
};

struct GuidelineCatalog {
    std::filesystem::path source_path;
    parser::GuidelinesDocument document;
    std::vector<GuidelineCatalogEntry> entries;
    std::unordered_set<std::string> ids;
    std::vector<ReviewProfileCatalogEntry> review_profile_entries;
    std::unordered_set<std::string> review_profile_ids;
};

std::filesystem::path FindSccgCatalogFile();
std::filesystem::path FindSccgDistDirectory();
std::filesystem::path FindGuidelinesFile();
GuidelineCatalog BuildGuidelineCatalog(parser::GuidelinesDocument document, std::filesystem::path source_path = {});
bool LoadGuidelineCatalog(GuidelineCatalog& catalog, std::string& error);

} // namespace core