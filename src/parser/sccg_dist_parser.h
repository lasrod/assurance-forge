#pragma once

#include "parser/guidelines_parser.h"

#include <filesystem>

namespace parser {

class SccgDistParser {
public:
    static GuidelinesParseResult ParseDirectory(const std::filesystem::path& dist_dir);
};

} // namespace parser