#pragma once

#include "core/problems/problem_item.h"
#include "core/project_model.h"
#include "core/terminology_package_service.h"
#include "sacm/sacm_package_tree.h"

#include <cstddef>
#include <string>

namespace app {

struct AppRuntimeState;

class TerminologyActions {
public:
    explicit TerminologyActions(AppRuntimeState& state);

    void BeginAddPackage(const core::ProjectFileEntry& entry, const sacm::SacmPackageTreeNode& parent_node);
    bool ConfirmAddPackage();
    bool ApplyPackageEdits();
    void BeginDeletePackage();
    bool ConfirmDeletePackage();
    bool OpenTermFromCanvas(const core::TerminologyPackageRef& package_ref, const core::TerminologyTermRef& term_ref);
    bool EditTermFromCanvas(const core::TerminologyPackageRef& package_ref, const core::TerminologyTermRef& term_ref);
    bool AddTermAsContextFromCanvas(const std::string& element_id,
                                    const core::TerminologyPackageRef& package_ref,
                                    const core::TerminologyTermRef& term_ref);
    bool AddVisibleTermContextFromCanvas(const std::string& element_id,
                                         const core::TerminologyPackageRef& package_ref,
                                         const core::TerminologyTermRef& term_ref);
    void SelectTerm(const core::TerminologyTermRef& term_ref);
    void BeginAddTerm();
    bool BeginEditTerm(const core::TerminologyTermRef& term_ref);
    bool ConfirmTermEdit();
    void BeginDeleteTerm(const core::TerminologyTermRef& term_ref);
    bool ConfirmDeleteTerm();
    void SelectCategory(const core::TerminologyCategoryRef& category_ref);
    void SetCategoryFilter(const std::string& category_filter);
    void BeginAddCategory();
    bool BeginEditCategory(const core::TerminologyCategoryRef& category_ref);
    void ConfirmCategoryEdit();
    void BeginDeleteCategory(const core::TerminologyCategoryRef& category_ref);
    void ConfirmDeleteCategory();
    void SeedRecommendedCategories();
    void BeginFindUsages(const core::TerminologyPackageRef& package_ref, const core::TerminologyTermRef& term_ref);
    void NavigateToUsage(std::size_t usage_index);
    void ChangeMeaningFromCanvas(const std::string& element_id, const std::string& term_value);
    void BeginQuickDefineTerm(const std::string& element_id, const std::string& term_value);
    void BeginLinkExistingTerm(const std::string& element_id, const std::string& term_value);
    bool ConfirmQuickDefineTerm(bool add_as_context);
    void HandleProblemQuickFix(const core::ProblemItem& problem);
    void IgnoreSuggestion(const std::string& element_id, const std::string& term_value);
    bool IsSuggestionIgnored(const std::string& element_id, const std::string& term_value) const;

private:
    AppRuntimeState& state_;
};

} // namespace app
