#pragma once

#include "core/terminology_package_service.h"

#include <cstddef>
#include <string>

namespace app {

struct AppRuntimeState;

class TerminologyActions {
public:
    explicit TerminologyActions(AppRuntimeState& state);

    void BeginFindUsages(const core::TerminologyPackageRef& package_ref, const core::TerminologyTermRef& term_ref);
    void NavigateToUsage(std::size_t usage_index);
    void ChangeMeaningFromCanvas(const std::string& element_id, const std::string& term_value);
    void IgnoreSuggestion(const std::string& element_id, const std::string& term_value);
    bool IsSuggestionIgnored(const std::string& element_id, const std::string& term_value) const;

private:
    AppRuntimeState& state_;
};

} // namespace app
