#pragma once

#include "app/app_events.h"
#include "core/element_factory.h"
#include "core/problems/problem_item.h"
#include "core/project_model.h"
#include "core/reviews/review_item.h"
#include "core/terminology_package_service.h"
#include "core/tree_editing.h"
#include "sacm/sacm_package_tree.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace app {

namespace areas {
struct WorkbenchAreaCallbacks;
}
namespace frame {
struct AppLayoutRegion;
}
struct AppRuntimeState;

class AppRuntime {
public:
    AppRuntime();
    ~AppRuntime();

    AppRuntime(const AppRuntime&) = delete;
    AppRuntime& operator=(const AppRuntime&) = delete;

    void RenderFrame(bool& done);
    void RequestClose();

    void LoadRecentProjectsPreference(const std::string& content);
    std::string RecentProjectsPreferenceJson() const;
    void LoadReviewerNamePreference(const std::string& content);
    std::string ReviewerNamePreference() const;

    // Add a new child element to the currently selected element.
    // Returns true on success; updates selection to the new element.
    bool AddChildToSelected(core::NewElementKind kind);

    // Add a new top-level Goal (root claim) to the current model.
    bool AddTopGoal();

    bool AddAcpToSelectedElement();
    bool AddAcpToRelationship(const std::string& relationship_id);
    bool RemoveAcp(const std::string& acp_id);
    bool RemoveProjectFile(const core::ProjectFileEntry& entry);
    bool RevealProjectFileInExplorer(const core::ProjectFileEntry& entry);
    void OpenArgumentPackageCanvas(const std::string& package_id,
                                   const std::string& package_gid,
                                   const std::string& display_name,
                                   const std::string& focus_element_id = {});
    void OpenFirstArgumentPackageCanvas();

    // Remove the currently selected element using the given mode. If the
    // planned removal targets more than one element, opens the confirmation
    // modal (with canvas highlight + fit-to-view) instead of removing.
    void RemoveSelected(core::RemoveMode mode);
    core::TreeDropValidationResult
    ValidateTreeDrop(const std::string& dragged_id, const std::string& target_id, core::TreeDropMode drop_mode) const;
    bool PerformTreeDrop(const std::string& dragged_id, const std::string& target_id, core::TreeDropMode drop_mode);

    // Set a transient status message (shown next frame in the SACM viewer panel).
    void SetStatus(const std::string& message);

    // Show the "not implemented" modal for the given feature name.
    void ShowNotImplementedModal(const std::string& feature);

    // Render AI review actions inside an element context menu.
    void RenderAiReviewContextMenuForSelected();

    // Build and send an AI review request for the selected element and SCCG profile.
    void RunAiReviewForSelection(const std::string& review_profile_id);

    // Returns the currently loaded assurance case, or nullptr if none.
    const parser::AssuranceCase* GetLoadedCase() const;

private:
    void RegisterAppEventListeners();

    void ScanDirectory();
    void RenderSacmViewerPanel(float left_w, float sacm_h, float top_y);
    areas::WorkbenchAreaCallbacks MakeWorkbenchAreaCallbacks();

    void BeginCreateProject();
    void BeginOpenProject();
    void BeginCreateProjectSacmFile();
    void BeginCreateProjectEvidenceRegister();
    void BeginCreateProjectJ3377CaeRegister();
    void OpenProjectFile(const core::ProjectFileEntry& entry);
    void PerformOpenProjectFile(const core::ProjectFileEntry& entry);
    void ConfirmPendingProjectFileOpen(bool save_current);
    void OpenProjectPackageNode(const core::ProjectFileEntry& entry, const sacm::SacmPackageTreeNode& node);
    void BeginAddTerminologyPackage(const core::ProjectFileEntry& entry, const sacm::SacmPackageTreeNode& parent_node);
    void ConfirmAddTerminologyPackage();
    void ApplyTerminologyPackageEdits();
    void BeginDeleteTerminologyPackage();
    void ConfirmDeleteTerminologyPackage();
    void RemoveProjectPackage(const core::ProjectFileEntry& entry, const sacm::SacmPackageTreeNode& node);
    void SelectTerminologyTerm(const core::TerminologyTermRef& term_ref);
    void BeginAddTerminologyTerm();
    void BeginEditTerminologyTerm(const core::TerminologyTermRef& term_ref);
    void ConfirmTerminologyTermEdit();
    void OpenTerminologyTermFromCanvas(const core::TerminologyPackageRef& package_ref,
                                       const core::TerminologyTermRef& term_ref);
    void EditTerminologyTermFromCanvas(const core::TerminologyPackageRef& package_ref,
                                       const core::TerminologyTermRef& term_ref);
    void AddTerminologyTermAsContextFromCanvas(const std::string& element_id,
                                               const core::TerminologyPackageRef& package_ref,
                                               const core::TerminologyTermRef& term_ref);
    void AddVisibleTerminologyTermContextFromCanvas(const std::string& element_id,
                                                    const core::TerminologyPackageRef& package_ref,
                                                    const core::TerminologyTermRef& term_ref);
    void BeginFindTerminologyUsages(const core::TerminologyPackageRef& package_ref,
                                    const core::TerminologyTermRef& term_ref);
    void FindTerminologyUsagesFromCanvas(const core::TerminologyPackageRef& package_ref,
                                         const core::TerminologyTermRef& term_ref);
    void NavigateToTerminologyUsage(std::size_t usage_index);
    void ChangeTerminologyMeaningFromCanvas(const std::string& element_id, const std::string& term_value);
    void BeginQuickDefineTerminologyTerm(const std::string& element_id, const std::string& term_value);
    void BeginLinkExistingTerminologyTerm(const std::string& element_id, const std::string& term_value);
    void IgnoreTerminologySuggestion(const std::string& element_id, const std::string& term_value);
    bool IsTerminologySuggestionIgnored(const std::string& element_id, const std::string& term_value) const;
    void ConfirmQuickDefineTerminologyTerm(bool add_as_context);
    void BeginDeleteTerminologyTerm(const core::TerminologyTermRef& term_ref);
    void ConfirmDeleteTerminologyTerm();
    void SelectTerminologyCategory(const core::TerminologyCategoryRef& category_ref);
    void SetTerminologyCategoryFilter(const std::string& category_filter);
    void BeginAddTerminologyCategory();
    void BeginEditTerminologyCategory(const core::TerminologyCategoryRef& category_ref);
    void ConfirmTerminologyCategoryEdit();
    void BeginDeleteTerminologyCategory(const core::TerminologyCategoryRef& category_ref);
    void ConfirmDeleteTerminologyCategory();
    void SeedRecommendedTerminologyCategories();
    bool OpenFirstProjectSacmFile();
    bool TryOpenProjectManifest(const std::string& selected_path);
    bool EnsureReviewItemStorage();
    bool EnsureConfidenceStorage();
    void RefreshSacmPackageTreeCache();
    void SyncReviewProblems();
    void SyncTerminologyProblems();
    void SyncConfidenceProblems();
    void SyncAcpProblems();
    void HandleProblemQuickFix(const core::ProblemItem& problem);
    void TouchCurrentProjectRecent();
    bool SaveProject();
    void ExportGsnSvg();
    void RequestExit(bool& done);

    bool BeginProposalForReviewItem(const core::reviews::ReviewItem& item);
    bool BeginEditProposalForReviewItem(const core::reviews::ReviewItem& item);
    bool BeginEditProposalById(const std::string& proposal_id);
    bool PreviewProposalById(const std::string& proposal_id);
    bool SaveActiveProposal(const core::reviews::ReviewItem& item);
    bool SetManualReviewOk(const std::string& element_id, bool manual_ok);
    void CancelActiveProposal();
    bool DeleteProposalPatchFile(const std::string& proposal_id, std::string& error);
    void CloseProposalPreviewIfOpen(const std::string& proposal_id);
    void BeginDeleteReviewItem(const core::reviews::ReviewItem& item);
    bool DeleteReviewItem(const core::reviews::ReviewItem& item);
    bool ResolveReviewItem(const core::reviews::ReviewItem& item);
    bool RefreshProposalCreatorPreview();
    void ProcessPendingProposalCreatorPreviewRefresh();
    bool AddProposalChildToSelected(core::NewElementKind kind);
    bool AddProposalTopGoal();
    void RemoveProposalSelected(core::RemoveMode mode);

    void RebuildDerivedViewsIfNeeded();
    void BeginAiReviewForSelection();
    void StartPendingAiReviewRequest();
    void PollAiReviewTask();

private:
    std::unique_ptr<AppRuntimeState> impl_;
};

} // namespace app
