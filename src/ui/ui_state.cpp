#include "ui/ui_state.h"

namespace ui {

UiState& GetUiState() {
    static UiState instance;
    return instance;
}

bool ModelHasTranslations(const parser::AssuranceCase& ac, const std::string& secondary_lang) {
    if (!secondary_lang.empty()) {
        // Check for a specific language
        for (const auto& elem : ac.elements) {
            if (elem.name_langs.count(secondary_lang)) return true;
            if (elem.description_langs.count(secondary_lang)) return true;
            if (elem.content_langs.count(secondary_lang)) return true;
        }
        return false;
    }
    // Check for any non-"en" language
    for (const auto& elem : ac.elements) {
        for (const auto& kv : elem.name_langs) {
            if (kv.first != "en") return true;
        }
        for (const auto& kv : elem.description_langs) {
            if (kv.first != "en") return true;
        }
        for (const auto& kv : elem.content_langs) {
            if (kv.first != "en") return true;
        }
    }
    return false;
}

}  // namespace ui
