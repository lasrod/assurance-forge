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
            if (elem.name_langs.count(secondary_lang))
                return true;
            if (elem.description_langs.count(secondary_lang))
                return true;
            if (elem.content_langs.count(secondary_lang))
                return true;
        }
        return false;
    }
    // Check for any non-"en" language
    for (const auto& elem : ac.elements) {
        for (const auto& kv : elem.name_langs) {
            if (kv.first != "en")
                return true;
        }
        for (const auto& kv : elem.description_langs) {
            if (kv.first != "en")
                return true;
        }
        for (const auto& kv : elem.content_langs) {
            if (kv.first != "en")
                return true;
        }
    }
    return false;
}

const std::string& DraftAcceptError(const UiState& ui_state, std::uint64_t workspace_revision) {
    static const std::string kNone;
    if (ui_state.draft_accept_error.empty() || ui_state.draft_accept_error_revision != workspace_revision)
        return kNone;
    return ui_state.draft_accept_error;
}

void BeginAiReviewSpinner(UiState& ui_state,
                          const std::string& element_id,
                          std::unordered_set<std::string> review_scope_element_ids) {
    if (element_id.empty())
        return;
    ui_state.ai_review_running_element_ids.insert(element_id);
    // Only the first concurrent review owns the scope/primary anchor. A
    // secondary spinner joins the running set without
    // displacing the existing primary element or its scope highlight.
    if (ui_state.ai_review_primary_element_id.empty() && !review_scope_element_ids.empty()) {
        ui_state.ai_review_scope_element_ids = std::move(review_scope_element_ids);
        ui_state.ai_review_primary_element_id = element_id;
    } else if (ui_state.ai_review_primary_element_id.empty()) {
        ui_state.ai_review_primary_element_id = element_id;
        ui_state.ai_review_scope_element_ids.insert(element_id);
    }
}

void EndAiReviewSpinner(UiState& ui_state, const std::string& element_id) {
    if (element_id.empty())
        return;
    ui_state.ai_review_running_element_ids.erase(element_id);
    if (ui_state.ai_review_primary_element_id == element_id) {
        ui_state.ai_review_primary_element_id.clear();
        ui_state.ai_review_scope_element_ids.clear();
    }
}

void SyncAiReviewSuccessMarkers(UiState& ui_state, const core::reviews::ElementReviewStateMap& review_states) {
    ui_state.ai_review_success_markers.clear();
    for (const auto& [element_id, outcome] : review_states) {
        if (element_id.empty() || !outcome.ai_ok || outcome.failed)
            continue;
        ui_state.ai_review_success_markers.emplace(
            element_id, AiReviewSuccessMarker{outcome.review_profile_name, outcome.last_review_message});
    }
}

void FocusProblemInPanel(UiState& ui_state, const std::string& problem_id, const std::string& element_id) {
    if (problem_id.empty()) {
        ui_state.selected_problem_id.clear();
        ui_state.selected_problem_element_id.clear();
        ui_state.problems_panel_focus_pending = false;
        ui_state.problems_panel_open_pending = false;
        return;
    }
    ui_state.selected_problem_id = problem_id;
    ui_state.selected_problem_element_id = element_id;
    ui_state.problems_panel_focus_pending = true;
    ui_state.problems_panel_open_pending = true;
    // Reset the panel filter so the focused row is guaranteed to be visible.
    ui_state.active_problem_filter = ProblemFilter::All;
}

} // namespace ui
