#pragma once

#include <string>
#include <optional>
#include "parser/xml_parser.h"

namespace core {

// Simple application state container
struct AppState {
    // Currently loaded assurance case (if any)
    std::optional<parser::AssuranceCase> loaded_case;

    // Status message for UI display
    std::string status_message;

    // Load an assurance case from file
    bool load_file(const std::string& file_path);
};

}  // namespace core
