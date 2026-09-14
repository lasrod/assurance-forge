#include "parser/guidelines_parser.h"

// The catalogue model's lookups. The catalogue itself is read by
// `SccgDistParser` from `sccg.full.json`; the YAML loader that used to live here
// read the same catalogue a second way and went when SCCG declared the whole
// JSON file sufficient on its own (contract 3.1.0).

#include <algorithm>

namespace parser {

const RetiredGuideline* GuidelinesDocument::FindRetiredGuidelineById(const std::string& id) const {
    auto found = std::find_if(retired_guidelines.begin(),
                              retired_guidelines.end(),
                              [&](const RetiredGuideline& retired) { return retired.id == id; });
    return found == retired_guidelines.end() ? nullptr : &(*found);
}

const Guideline* GuidelinesDocument::FindGuidelineById(const std::string& id) const {
    auto found = std::find_if(
        guidelines.begin(), guidelines.end(), [&](const Guideline& guideline) { return guideline.id == id; });
    return found == guidelines.end() ? nullptr : &(*found);
}

std::vector<const Guideline*> GuidelinesDocument::FindGuidelinesByCategory(const std::string& category_id) const {
    std::vector<const Guideline*> matches;
    for (const auto& guideline : guidelines) {
        if (guideline.category == category_id)
            matches.push_back(&guideline);
    }
    return matches;
}

std::vector<const Guideline*>
GuidelinesDocument::FindGuidelinesByApplicableElement(const std::string& element_name) const {
    std::vector<const Guideline*> matches;
    for (const auto& guideline : guidelines) {
        const auto& elements = guideline.tool.applicable_elements;
        if (std::find(elements.begin(), elements.end(), element_name) != elements.end()) {
            matches.push_back(&guideline);
        }
    }
    return matches;
}

std::vector<const Guideline*>
GuidelinesDocument::FindGuidelinesByReviewProfile(const std::string& review_profile_id) const {
    std::vector<const Guideline*> matches;
    const ReviewProfile* profile = FindReviewProfileById(review_profile_id);
    if (!profile)
        return matches;

    for (const std::string& guideline_id : profile->guideline_ids) {
        const Guideline* guideline = FindGuidelineById(guideline_id);
        if (guideline)
            matches.push_back(guideline);
    }
    return matches;
}

std::vector<const Guideline*> GuidelinesDocument::FindGuidelinesBySuggestedCheckId(const std::string& check_id) const {
    std::vector<const Guideline*> matches;
    for (const auto& guideline : guidelines) {
        for (const auto& check : guideline.tool.suggested_checks) {
            if (check.id == check_id) {
                matches.push_back(&guideline);
                break;
            }
        }
    }
    return matches;
}

const SuggestedCheck* GuidelinesDocument::FindSuggestedCheckById(const std::string& check_id) const {
    for (const auto& guideline : guidelines) {
        for (const auto& check : guideline.tool.suggested_checks) {
            if (check.id == check_id)
                return &check;
        }
    }
    return nullptr;
}

const ReviewProfile* GuidelinesDocument::FindReviewProfileById(const std::string& id) const {
    auto found = std::find_if(
        review_profiles.begin(), review_profiles.end(), [&](const ReviewProfile& profile) { return profile.id == id; });
    return found == review_profiles.end() ? nullptr : &(*found);
}

const DataPackage* GuidelinesDocument::FindDataPackageById(const std::string& id) const {
    auto found = std::find_if(
        data_packages.begin(), data_packages.end(), [&](const DataPackage& package) { return package.id == id; });
    return found == data_packages.end() ? nullptr : &(*found);
}

const Precheck* GuidelinesDocument::FindPrecheckById(const std::string& id) const {
    auto found =
        std::find_if(prechecks.begin(), prechecks.end(), [&](const Precheck& precheck) { return precheck.id == id; });
    return found == prechecks.end() ? nullptr : &(*found);
}

const AvailabilityState* GuidelinesDocument::FindAvailabilityStateById(const std::string& id) const {
    auto found = std::find_if(
        availability_states.begin(), availability_states.end(), [&](const AvailabilityState& s) { return s.id == id; });
    return found == availability_states.end() ? nullptr : &(*found);
}

const DataPackage* GuidelinesDocument::FindSelectedElementPackage(const ReviewProfile& profile) const {
    for (const std::string& package_id : profile.required_data) {
        const DataPackage* package = FindDataPackageById(package_id);
        if (package != nullptr && package->role == "selected_element")
            return package;
    }
    return nullptr;
}

std::string GuidelinesDocument::ElementRoleForSelectableElement(const std::string& element_name) const {
    auto found = std::find_if(selectable_elements.begin(), selectable_elements.end(), [&](const SelectableElement& e) {
        return e.element == element_name;
    });
    return found == selectable_elements.end() ? std::string() : found->element_role;
}

const AuthoringElementRule* GuidelinesDocument::FindAuthoringElementRule(const std::string& element_role) const {
    const std::vector<AuthoringElementRule>& rules = authoring_guidance.element_rules;
    auto found = std::find_if(rules.begin(), rules.end(), [&](const AuthoringElementRule& rule) {
        return rule.element_role == element_role;
    });
    return found == rules.end() ? nullptr : &(*found);
}

const ReviewProfile* GuidelinesDocument::FindReviewProfileForElementRole(const std::string& element_role) const {
    if (element_role.empty()) {
        return nullptr;
    }
    for (const ReviewProfile& profile : review_profiles) {
        const DataPackage* selected = FindSelectedElementPackage(profile);
        if (selected != nullptr && selected->element_role == element_role) {
            return &profile;
        }
    }
    return nullptr;
}

} // namespace parser
