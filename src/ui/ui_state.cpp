#include "ui/ui_state.h"

namespace ui {

namespace {

ElementReviewVisualState* MutableReviewState(UiState& ui_state, const std::string& element_id) {
    if (element_id.empty())
        return nullptr;
    return &ui_state.review_visual_states[element_id];
}

void StoreReviewProfile(ElementReviewVisualState& state,
                        const std::string& review_profile_id,
                        const std::string& review_profile_name) {
    if (!review_profile_id.empty())
        state.review_profile_id = review_profile_id;
    if (!review_profile_name.empty())
        state.review_profile_name = review_profile_name;
}

void RemoveIfVisuallyEmpty(UiState& ui_state, const std::string& element_id) {
    auto found = ui_state.review_visual_states.find(element_id);
    if (found == ui_state.review_visual_states.end())
        return;

    const ElementReviewVisualState& state = found->second;
    if (!state.ai_running && !state.ai_ok && !state.manual_ok && !state.failed) {
        ui_state.review_visual_states.erase(found);
    }
}

} // namespace

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

ElementReviewVisualStatus ResolveElementReviewVisualStatus(const ElementReviewVisualState& state) {
    if (state.ai_running)
        return ElementReviewVisualStatus::AiRunning;
    if (state.failed)
        return ElementReviewVisualStatus::Failed;
    if (state.manual_ok)
        return ElementReviewVisualStatus::ManualOk;
    if (state.ai_ok)
        return ElementReviewVisualStatus::AiOk;
    return ElementReviewVisualStatus::None;
}

ElementReviewVisualStatus ResolveElementReviewVisualStatus(const UiState& ui_state, const std::string& element_id) {
    const ElementReviewVisualState* state = FindElementReviewVisualState(ui_state, element_id);
    return state ? ResolveElementReviewVisualStatus(*state) : ElementReviewVisualStatus::None;
}

const ElementReviewVisualState* FindElementReviewVisualState(const UiState& ui_state, const std::string& element_id) {
    auto found = ui_state.review_visual_states.find(element_id);
    if (found == ui_state.review_visual_states.end())
        return nullptr;
    return &found->second;
}

void MarkAiReviewRunning(UiState& ui_state,
                         const std::string& element_id,
                         const std::string& review_profile_id,
                         const std::string& review_profile_name,
                         std::unordered_set<std::string> review_scope_element_ids) {
    ElementReviewVisualState* state = MutableReviewState(ui_state, element_id);
    if (!state)
        return;
    state->ai_running = true;
    StoreReviewProfile(*state, review_profile_id, review_profile_name);
    state->last_review_message = "AI review in progress.";
    if (review_scope_element_ids.empty())
        review_scope_element_ids.insert(element_id);
    ui_state.ai_review_scope_element_ids = std::move(review_scope_element_ids);
    ui_state.ai_review_primary_element_id = element_id;
}

void MarkAiReviewNoFindings(UiState& ui_state,
                            const std::string& element_id,
                            const std::string& review_profile_id,
                            const std::string& review_profile_name) {
    ElementReviewVisualState* state = MutableReviewState(ui_state, element_id);
    if (!state)
        return;
    state->ai_running = false;
    state->failed = false;
    if (!state->manual_ok)
        state->ai_ok = true;
    StoreReviewProfile(*state, review_profile_id, review_profile_name);
    state->last_review_message = "AI review completed with no findings.";
    ui_state.ai_review_scope_element_ids.clear();
    ui_state.ai_review_primary_element_id.clear();
}

void MarkAiReviewFindings(UiState& ui_state, const std::string& element_id) {
    ElementReviewVisualState* state = MutableReviewState(ui_state, element_id);
    if (!state)
        return;
    state->ai_running = false;
    state->ai_ok = false;
    state->failed = false;
    state->last_review_message = "AI review completed with findings.";
    ui_state.ai_review_scope_element_ids.clear();
    ui_state.ai_review_primary_element_id.clear();
    RemoveIfVisuallyEmpty(ui_state, element_id);
}

void MarkAiReviewFailed(UiState& ui_state,
                        const std::string& element_id,
                        const std::string& message,
                        const std::string& review_profile_id,
                        const std::string& review_profile_name) {
    ElementReviewVisualState* state = MutableReviewState(ui_state, element_id);
    if (!state)
        return;
    state->ai_running = false;
    state->ai_ok = false;
    state->failed = true;
    StoreReviewProfile(*state, review_profile_id, review_profile_name);
    state->last_review_message = message.empty() ? "AI review failed." : message;
    ui_state.ai_review_scope_element_ids.clear();
    ui_state.ai_review_primary_element_id.clear();
}

void MarkReviewOkManually(UiState& ui_state, const std::string& element_id) {
    ElementReviewVisualState* state = MutableReviewState(ui_state, element_id);
    if (!state)
        return;
    state->ai_ok = false;
    state->failed = false;
    state->manual_ok = true;
    state->last_review_message = "Review status manually marked OK.";
}

} // namespace ui
