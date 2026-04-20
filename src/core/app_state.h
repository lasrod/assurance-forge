#pragma once

#include <string>
#include <optional>
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

namespace core {

// Simple application state container
struct AppState {
    // Currently loaded assurance case (if any)
    std::optional<parser::AssuranceCase> loaded_case;

    // SACM domain model (populated on load, used for save)
    std::optional<sacm::AssuranceCasePackage> sacm_package;

    // Status message for UI display
    std::string status_message;

    // Load an assurance case from file
    bool load_file(const std::string& file_path);

    // Save the SACM package to file
    bool save_file(const std::string& file_path);
};

}  // namespace core
