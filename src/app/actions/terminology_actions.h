#pragma once

#include "core/terminology_package_service.h"

#include <cstddef>
#include <string>

namespace app {

struct AppRuntimeState;

class TerminologyActions {
public:
    explicit TerminologyActions(AppRuntimeState& state);

    bool OpenTermFromCanvas(const core::TerminologyPackageRef& package_ref, const core::TerminologyTermRef& term_ref);
    bool EditTermFromCanvas(const core::TerminologyPackageRef& package_ref, const core::TerminologyTermRef& term_ref);
    bool AddTermAsContextFromCanvas(const std::string& element_id,
                                    const core::TerminologyPackageRef& package_ref,
                                    const core::TerminologyTermRef& term_ref);
    bool AddVisibleTermContextFromCanvas(const std::string& element_id,
                                         const core::TerminologyPackageRef& package_ref,
                                         const core::TerminologyTermRef& term_ref);
    void BeginFindUsages(const core::TerminologyPackageRef& package_ref, const core::TerminologyTermRef& term_ref);
    void NavigateToUsage(std::size_t usage_index);
    void ChangeMeaningFromCanvas(const std::string& element_id, const std::string& term_value);
    void BeginQuickDefineTerm(const std::string& element_id, const std::string& term_value);
    void BeginLinkExistingTerm(const std::string& element_id, const std::string& term_value);
    bool ConfirmQuickDefineTerm(bool add_as_context);
    void IgnoreSuggestion(const std::string& element_id, const std::string& term_value);
    bool IsSuggestionIgnored(const std::string& element_id, const std::string& term_value) const;

private:
    AppRuntimeState& state_;
};

} // namespace app
