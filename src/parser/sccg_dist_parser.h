#pragma once

#include "parser/guidelines_parser.h"

#include <filesystem>

namespace parser {

// The SCCG catalogue, read from the one file SCCG declares sufficient on its
// own for review, authoring and retirement: `dist/sccg.full.json` (contract
// 3.1.0 onwards; safety-case-core-guidelines#17).
//
// Before that declaration this tool kept two loaders -- the per-concern JSON
// files and the whole-catalogue YAML -- and implemented, tested and reviewed
// every contract addition twice. Worse, the per-concern files SCCG's own
// integration guide pointed a review tool at did not carry the retirement list,
// and the runtime copy silently retired nothing. One file, one parse.
class SccgDistParser {
public:
    static constexpr const char* kCatalogFileName = "sccg.full.json";

    // `dist_dir / kCatalogFileName`.
    static GuidelinesParseResult ParseDirectory(const std::filesystem::path& dist_dir);
    static GuidelinesParseResult ParseFile(const std::filesystem::path& catalog_path);
};

} // namespace parser
