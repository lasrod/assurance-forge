#pragma once

#include "core/problems/problem_item.h"
#include "core/project_model.h"
#include "core/terminology_package_service.h"
#include "legacy_sacm/sacm_package_tree.h"

#include <cstddef>
#include <string>
#include <vector>

namespace app {
struct AppRuntimeState;
}
namespace sacm_adapter {
class LibraryDocument;
}

namespace app::actions {

// A persisted terminology-ignore decision surfaced to the UI for review/restore.
struct IgnoredSuggestionView {
    std::string element_id;
    std::string term;
};

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
    void RestoreSuggestion(const std::string& element_id, const std::string& term_value);
    std::vector<IgnoredSuggestionView> ListIgnoredSuggestions() const;
    // Clears the in-memory ignore set and reloads it from the current project's
    // sidecar file (no-op clear when no project is open).
    void LoadIgnoredSuggestions();

private:
    // Fills `pending_delete_term_references` from the SACM library's preview of
    // deleting `term_id` from `document`, so the confirmation can list what a
    // cascade would remove.
    void PreviewTermDeleteReferences(const sacm_adapter::LibraryDocument& document, const std::string& term_id);

    // True while the working draft document takes the user's glossary edits
    // (ADR 0016) -- the same rule the argument's own edits follow.
    bool DraftTakesGlossaryEdits() const;

    // True, with the reason on the status line, when `gesture` (translated) has
    // to be refused because it writes to the accepted document while a working
    // draft is open (see `detail::AcceptedGlossaryEditBlockedByDraft`). Checked
    // at the gesture that would open an editor as well as at the confirm, so a
    // modal never opens onto an edit that cannot be made.
    bool AcceptedEditRefused(const std::string& gesture);

    // True, with the reason on the status line, when a term or category created
    // in the draft would not land in `package_ref`: the draft files a new term
    // or category in the case's first glossary, the rule an MCP client's
    // `CreateTerm` follows, so a create aimed at another glossary would land
    // somewhere the user did not choose.
    bool DraftCreateTargetRefused(const core::TerminologyPackageRef& package_ref);

    // Lookups in the WORKING package: the draft's glossary while a draft differs
    // from the accepted argument, the accepted glossary otherwise. Null when
    // nothing is loaded or the reference matches nothing.
    const sacm::TerminologyPackage* WorkingTerminologyPackage(const core::TerminologyPackageRef& package_ref);
    const sacm::Term* WorkingTerm(const core::TerminologyPackageRef& package_ref,
                                  const core::TerminologyTermRef& term_ref);
    const sacm::Category* WorkingCategory(const core::TerminologyPackageRef& package_ref,
                                          const core::TerminologyCategoryRef& category_ref);

    // The draft halves of the confirms: the editor's result expressed as the
    // operations an MCP client would send, applied to the draft document.
    bool ConfirmTermEditInDraft(const core::TerminologyTermDraft& draft);
    bool ConfirmDeleteTermInDraft();
    bool ConfirmCategoryEditInDraft(const core::TerminologyCategoryDraft& draft);
    void SeedRecommendedCategoriesInDraft(const sacm::TerminologyPackage& terminology_package,
                                          const std::vector<std::string>& missing_names);
    bool ConfirmQuickDefineTermInDraft(bool add_as_context);

    AppRuntimeState& state_;
};

} // namespace app::actions
