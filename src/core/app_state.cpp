#include "core/app_state.h"
#include "sacm/sacm_parser.h"
#include "sacm/sacm_serializer.h"

namespace core {

bool AppState::load_file(const std::string& file_path) {
    parser::ParseResult result = parser::parse_sacm_xml(file_path);

    if (result.success) {
        loaded_case = std::move(result.assurance_case);
        status_message = "Loaded: " + loaded_case->name +
                         " (" + std::to_string(loaded_case->elements.size()) + " elements)";

        // Also populate the SACM domain model for save support
        auto sacm_result = sacm::parse_sacm(file_path);
        if (sacm_result.success) {
            sacm_package = std::move(sacm_result.package);
        }

        return true;
    } else {
        loaded_case.reset();
        sacm_package.reset();
        status_message = "Error: " + result.error_message;
        return false;
    }
}

bool AppState::save_file(const std::string& file_path) {
    if (!sacm_package.has_value()) {
        status_message = "Error: No SACM data to save";
        return false;
    }

    if (sacm::serialize_sacm_to_file(sacm_package.value(), file_path)) {
        status_message = "Saved to: " + file_path;
        return true;
    } else {
        status_message = "Error: Failed to write " + file_path;
        return false;
    }
}

}  // namespace core
