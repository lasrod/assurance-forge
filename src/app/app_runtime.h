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
namespace actions {
struct IgnoredSuggestionView;
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

    // GSN v3 dialectic challenges against the selected element or a relationship.
    bool AddCounterArgumentToSelected();
    bool AddCounterEvidenceToSelected();
    bool AddCounterArgumentToRelationship(const std::string& relationship_id);
    bool AddCounterEvidenceToRelationship(const std::string& relationship_id);
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
    void RestoreTerminologySuggestion(const std::string& element_id, const std::string& term_value);
    std::vector<actions::IgnoredSuggestionView> ListIgnoredTerminologySuggestions() const;
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
    // Reconcile the audit log when replay verification reports divergence.
    // Backs up the current `.af/` artifacts, rebuilds the store from the
    // currently-loaded SACM, reinstalls the command bus, and re-runs the
    // verifier. Returns true on success.
    bool ReconcileAuditStore();
    bool EnsureReviewItemStorage();

    // Starts or stops serving connected AI clients to match the open project.
    // A project with no root -- a bare SACM file -- is not served: there is
    // nowhere to anchor an endpoint record, and nothing to propose against.
    void UpdateAgentBridgeForProject();
    // Runs whatever a connected client asked for, on this thread, against the
    // model this frame is about to draw. Returns true when anything ran.
    bool PollAgentBridge();
    // Switches the application to another of the project's argument files at a
    // connected client's request, through the same path a user's click takes.
    bool OpenAgentRequestedCaseFile(const std::string& relative_path, std::string& error);

  public:
    // Accepts an agent's change set: the one point where staged work becomes a
    // real edit. Goes through `ApplyProposalCommand`, so it is audited, undoable
    // and attributed like any other change. Only a person reaches this.
    bool AcceptAgentChangeSet(const std::string& change_set_id, std::string& error);
    bool RejectAgentChangeSet(const std::string& change_set_id, std::string& error);

  private:

    bool EnsureConfidenceStorage();
    bool EnsureRegisterStorage();
    // Points every controller that owns a file outside the SACM document at the
    // currently-open project. Must run on both project-open and project-create:
    // a controller still holding the previous project's store would write it
    // into the new project on the next save.
    void EnsureProjectSideStorage();
    // Clears + reloads the current project's persisted terminology-ignore set, then
    // re-syncs terminology problems. Safe to call when no project is open.
    void EnsureTerminologyIgnoreStorage();
    void RefreshSacmPackageTreeCache();
    // Commit any in-progress inspector text edit as an audited transaction
    // before the SACM is written through a save/close path. Pulls the pending
    // edits from ui::TextEditSession and routes them through the element-edit
    // controller. Safe to call when nothing is pending or no project is open.
    void FlushPendingTextEdits();
    // Runs the per-source SyncXProblems() for each flag set in
    // AppRuntimeState::problems_dirty, then clears the flags. Called once per
    // frame so the ~dozens of model-mutation sites only have to mark a source
    // dirty rather than invoke the heavyweight sync inline.
    void RefreshDirtyProblems();
    void SyncReviewProblems();
    void SyncTerminologyProblems();
    void SyncConfidenceProblems();
    void SyncAcpProblems();
    void SyncStructureProblems();
    void SyncRegisterProblems();
    void SyncTranslationReviewProblems();
    void HandleProblemQuickFix(const core::ProblemItem& problem);
    // Quick fix for an orphaned register assessment: drops the stored
    // assessment the user has decided to let go of. Takes effect on disk at the
    // next project save.
    void DiscardOrphanedRegisterAssessment(const core::ProblemItem& problem);

    // Flags `element_id` for secondary-language translation review when the
    // element carries a translation. Called after a committed text edit. No-op
    // for elements without a secondary translation.
    void MarkTranslationReviewPending(const std::string& element_id);
    // Clears the translation-review flag for `element_id` (user pressed "Mark
    // reviewed") and re-syncs problems.
    void AcceptTranslationReview(const std::string& element_id);
    bool IsTranslationReviewPending(const std::string& element_id) const;
    // Clears + reloads the current project's persisted translation-review set,
    // then re-syncs. Safe to call when no project is open.
    void EnsureTranslationReviewStorage();
    void TouchCurrentProjectRecent();
    bool SaveProject();
    void ExportGsnSvg();
    void RequestExit(bool& done);

    // Undo the most recent in-force (i.e. not-itself-undone) audit
    // transaction. Bounded by the most recent snapshot or baseline at
    // or before the target sequence (Plan §5). Returns false (and emits
    // a status message) when there is nothing to undo or the undo
    // boundary has been reached.
    bool Undo();
    bool CanUndo() const;

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
