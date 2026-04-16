#include "core/app_state.h"

namespace core {

bool AppState::load_file(const std::string& file_path) {
    parser::ParseResult result = parser::parse_sacm_xml(file_path);

    if (result.success) {
        loaded_case = std::move(result.assurance_case);
        status_message = "Loaded: " + loaded_case->name +
                         " (" + std::to_string(loaded_case->elements.size()) + " elements)";
        return true;
    } else {
        loaded_case.reset();
        status_message = "Error: " + result.error_message;
        return false;
    }
}

}  // namespace core
