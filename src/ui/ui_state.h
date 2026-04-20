#pragma once

#include <string>

namespace ui {

struct UiState {
    std::string selected_element_id;
};

// Global shared UI state accessible from all panels.
UiState& GetUiState();

}  // namespace ui
