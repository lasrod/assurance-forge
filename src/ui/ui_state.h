#pragma once

#include <string>
#include "parser/xml_parser.h"

namespace ui {

enum class CenterView {
    GsnCanvas,
    CseRegister,
    EvidenceRegister,
    SccgGuidelines,
};

struct UiState {
    std::string selected_element_id;

    // Language toggle for GSN canvas
    bool show_secondary_language = false;
    std::string active_secondary_lang = "ja";
    bool model_has_translations = false;  // set when tree is built/rebuilt

    // Set to true when the canvas should center on the selected element
    bool center_on_selection = false;

    // Active center panel view
    CenterView center_view = CenterView::GsnCanvas;
};

// Global shared UI state accessible from all panels.
UiState& GetUiState();

// Returns true if any element in the assurance case has a non-empty secondary language entry.
bool ModelHasTranslations(const parser::AssuranceCase& ac, const std::string& secondary_lang = "ja");

}  // namespace ui
