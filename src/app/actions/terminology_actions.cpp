#include "app/actions/terminology_actions.h"

#include "app/app_events.h"
#include "app/app_runtime_state.h"

#include <cctype>

namespace app {
namespace {

void SetStatus(AppRuntimeState& state, const std::string& message) {
    state.events.Emit(StatusMessageEvent{message});
}

std::string TrimWhitespace(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin)))
        ++begin;
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))))
        --end;
    return std::string(begin, end);
}

std::string TerminologySuggestionKey(const std::string& element_id, const std::string& term_value) {
    return element_id + "\n" + term_value;
}

} // namespace

TerminologyActions::TerminologyActions(AppRuntimeState& state) : state_(state) {}

void TerminologyActions::BeginFindUsages(const core::TerminologyPackageRef& package_ref,
                                         const core::TerminologyTermRef& term_ref) {
    state_.terminology_usages_active = true;
    state_.focus_terminology_usages_tab = true;
    state_.usage_search_package_ref = package_ref;
    state_.usage_search_term_ref = term_ref;
    state_.usage_search_term_value.clear();
    state_.usage_search_term_name.clear();
    state_.usage_search_message.clear();
    state_.usage_search_error.clear();
    state_.terminology_usage_results.clear();
    state_.selected_terminology_usage_index = -1;

    if (!state_.app_state.sacm_package.has_value()) {
        state_.usage_search_error = "Open a SACM model before finding terminology usages.";
        SetStatus(state_, state_.usage_search_error);
        return;
    }

    core::TerminologyTermUsageSearchResult result =
        core::FindTerminologyTermUsages(state_.app_state.sacm_package.value(), package_ref, term_ref);
    state_.usage_search_term_value = result.term_value;
    state_.usage_search_term_name = result.term_name;
    if (!result.success) {
        state_.usage_search_error = result.error;
        SetStatus(state_, "Find usages failed: " + result.error);
        return;
    }

    state_.terminology_usage_results = std::move(result.usages);
    if (!state_.terminology_usage_results.empty())
        state_.selected_terminology_usage_index = 0;
    const int usage_count = static_cast<int>(state_.terminology_usage_results.size());
    const std::string label = state_.usage_search_term_value.empty() ? "term" : state_.usage_search_term_value;
    SetStatus(state_,
              "Found " + std::to_string(usage_count) + " usage" + (usage_count == 1 ? "" : "s") + " of " + label + ".");
}

void TerminologyActions::NavigateToUsage(std::size_t usage_index) {
    if (usage_index >= state_.terminology_usage_results.size())
        return;
    state_.selected_terminology_usage_index = static_cast<int>(usage_index);
    const core::TerminologyTermUsage& usage = state_.terminology_usage_results[usage_index];
    if (usage.element_id.empty()) {
        SetStatus(state_, "The selected usage has no navigable element id.");
        return;
    }
    state_.show_gsn_tab = true;
    state_.events.Emit(SelectionChangedEvent{usage.element_id, true});
    state_.events.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
}

void TerminologyActions::ChangeMeaningFromCanvas(const std::string& element_id, const std::string& term_value) {
    (void)element_id;
    (void)term_value;
    state_.events.Emit(ModalRequestEvent{ModalKind::NotImplemented, true, "Change linked terminology meaning"});
}

void TerminologyActions::IgnoreSuggestion(const std::string& element_id, const std::string& term_value) {
    const std::string trimmed_term = TrimWhitespace(term_value);
    state_.ignored_terminology_suggestion_keys.insert(TerminologySuggestionKey(element_id, trimmed_term));
    SetStatus(state_, "Ignored terminology suggestion " + trimmed_term + " for this session.");
}

bool TerminologyActions::IsSuggestionIgnored(const std::string& element_id, const std::string& term_value) const {
    const std::string key = TerminologySuggestionKey(element_id, TrimWhitespace(term_value));
    return state_.ignored_terminology_suggestion_keys.count(key) > 0;
}

} // namespace app
