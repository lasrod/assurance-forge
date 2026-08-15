#pragma once

#include "app/app_events.h"
#include "core/drafts/draft_change_index.h"
#include "core/drafts/draft_workspace.h"
#include "core/element_factory.h"
#include "core/problems/problem_item.h"
#include "core/project_model.h"
#include "core/reviews/review_item.h"
#include "core/terminology_package_service.h"
#include "core/tree_editing.h"
#include "legacy_sacm/sacm_package_tree.h"

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

    // Repairs for the defects the GSN v3 well-formedness checker reports. Each
    // is an ordinary audited edit, reachable from the Problems panel quick fix
    // and (for removal) from the canvas edge menu and relationship inspector.
    bool RemoveRelationship(const std::string& relationship_id);
    bool DropRelationshipReference(const std::string& relationship_id, const std::string& reference);
    bool MoveStrategyToReasoning(const std::string& relationship_id, const std::string& strategy_id);
    bool SetElementUndeveloped(const std::string& element_id, bool undeveloped);
    bool RenumberGsnIdentifier(const std::string& element_id);
    // Applies the repair a GSN well-formedness problem offers. False when the
    // problem belongs to another subsystem, so the quick-fix router falls
    // through to the next handler.
    bool ApplyGsnWellFormednessQuickFix(const core::ProblemItem& problem);
    core::TreeDropValidationResult
    ValidateTreeDrop(const std::string& dragged_id, const std::string& target_id, core::TreeDropMode drop_mode) const;
    bool PerformTreeDrop(const std::string& dragged_id, const std::string& target_id, core::TreeDropMode drop_mode);

    // Set a transient status message (shown next frame in the SACM viewer panel).
    void SetStatus(const std::string& message);

    // Show the "not implemented" modal for the given feature name.
    void ShowNotImplementedModal(const std::string& feature);

    // Render AI review actions inside an element context menu.
    void RenderAiReviewContextMenuForSelected();

    // Build and send an AI review request using the selected element's unique SCCG profile.
    void RunAiReviewForSelection();

    // Explicit-profile entry point retained for internal/debug callers.
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
    void EnsureAgentBridgeStarted();
    void UpdateAgentBridgeForProject();
    // Runs whatever a connected client asked for, on this thread, against the
    // model this frame is about to draw. Returns true when anything ran.
    bool PollAgentBridge();
    // Switches the application to another of the project's argument files at a
    // connected client's request, through the same path a user's click takes.
    bool OpenAgentRequestedCaseFile(const std::string& relative_path, std::string& error);

    // Recomputes what a connected client's open change set would do and returns
    // the model the canvas should draw: the preview when one is open, otherwise
    // the committed case unchanged. Also refreshes the per-element change status
    // the canvas decorates nodes with.
    //
    // Returning `committed` unchanged means the caller's argument has to outlive
    // the returned reference. The rvalue overload is deleted so that a caller
    // passing a temporary is a compile error rather than a dangling reference
    // the canvas would then read from for a frame.
    const parser::AssuranceCase& RefreshAgentChangePreview(const parser::AssuranceCase& committed);
    const parser::AssuranceCase& RefreshAgentChangePreview(parser::AssuranceCase&&) = delete;

    // Points the draft workspace store at whatever argument is open, opening its
    // recovery data when there is any and closing the previous one first.
    //
    // A draft belongs to one argument file, and the application has one loaded,
    // so exactly one workspace is live at a time. Run every frame before the
    // derived views rebuild: a connected client can switch which argument is
    // open, and a workspace left pointing at the previous one would decorate
    // this argument's identically-named elements.
    void SyncDraftWorkspace();

    // Rebuilds the per-element and per-relationship markers the canvas draws for
    // the working draft. Run with the derived views, from the same change index.
    void RefreshDraftDecorations();

    // Rebuilds what the inspector shows for the selected element: the accepted
    // and working text, the ordered contributions, and the dependency closure
    // accepting it would actually take.
    void RefreshSelectedDraftDetail();

    // Whether the user's edits belong in the draft rather than in the accepted
    // argument.
    //
    // While a draft is active the canvas is drawing the *working* model, so an
    // edit applied to the accepted model underneath it lands somewhere the user
    // was not looking -- and against a parent that may not exist there at all.
    // ADR 0009: no independently changing accepted editing surface underneath an
    // active draft.
    bool DraftEditingActive() const;

    // Appends operations to the user's own draft group, creating it on first
    // use, so a session of hand edits reads as one change rather than as one
    // group per click.
    bool StageHumanDraftOperations(const std::string& title,
                                   const std::vector<core::reviews::PatchOperation>& operations,
                                   std::string& error);

    // Routes "add a child under the selection" into the draft. Returns false
    // when no draft is active, so the caller falls through to the ordinary
    // accepted-model command.
    bool AddChildToSelectedAsDraft(core::NewElementKind kind);
    bool AddTopGoalAsDraft();
    bool RemoveSelectedAsDraft(core::RemoveMode mode);

    // Routes a finished text edit into the draft. Returns false when no draft is
    // active, so the caller falls through to the ordinary audited command.
    bool CommitTextEditAsDraft(const std::string& element_id,
                               const std::string& field_token,
                               const std::string& language,
                               const std::string& new_value);

    // The model the inspector edits: the working argument while a draft is
    // active, the accepted one otherwise.
    //
    // Held as its own copy rather than handed the materializer's cache, because
    // the panel edits the element in place for immediate feedback and the cache
    // is regenerated from the draft on the next materialization. The in-place
    // edit is provisional; the staged operation is what makes it real.
    parser::AssuranceCase* InspectorModel();

public:
    // The draft workspace for the argument that is open, or null when there is
    // no draft. Read-only: only `AppRuntime` mutates it, which is what keeps
    // ADR 0008's single-owner rule true of draft state as well as of SACM.
    const core::drafts::DraftWorkspace* CurrentDraftWorkspace() const;

    // **The one authoritative view of the argument.**
    //
    // The working model when a draft workspace is active and materializes, the
    // accepted model otherwise. The canvas, navigator, inspectors, search,
    // placement suggestions, validation and -- from phase 3 -- connected MCP
    // reads all read through this, so a proposal is never evaluated against a
    // different version of the argument than the one beside it (ADR 0009).
    //
    // Export, the audit baseline and the canonical model hash deliberately do
    // **not**: those are properties of what a human accepted.
    //
    // Non-const because it materializes on demand and caches the result. The
    // reference is valid until the next mutation of the workspace or the
    // accepted model, which for every caller means "this frame".
    const parser::AssuranceCase& CurrentArgumentView();

    // What the draft does to each element, and which groups did it. Empty when
    // no draft is active. Valid for the same window as `CurrentArgumentView`.
    const core::drafts::DraftChangeIndex& CurrentDraftChangeIndex();

    // What the *canvas* draws, which is the user's view-mode choice applied to
    // `CurrentArgumentView`.
    //
    // Deliberately separate. The view mode is a display preference; validation,
    // the checks and connected reads must not follow it, or switching to
    // "accepted baseline" to compare would quietly stop reporting the problems
    // the draft introduces. A rule about the argument is a property of the
    // argument, not of the part of it someone happens to be looking at.
    const parser::AssuranceCase& CurrentCanvasView();

private:
    // Accepts an agent's change set: the one point where staged work becomes a
    // real edit. Goes through `ApplyProposalCommand`, so it is audited, undoable
    // and attributed like any other change. Only a person reaches this.
    bool AcceptAgentChangeSet(const std::string& change_set_id, std::string& error);
    bool RejectAgentChangeSet(const std::string& change_set_id, std::string& error);

    // Accepts every active group in the working draft: the one point where
    // proposed work becomes accepted argument.
    //
    // Compiles the groups into a single `ReviewProposal` and dispatches one
    // audited `ApplyProposalCommand`, so the whole promotion is one transaction
    // with one undo boundary and one audit record naming every contributing
    // source. Only a person reaches this -- there is no tool that does.
    bool PromoteWorkingDraft(std::string& error);

    // Accepts a dependency-closed selection of draft groups.
    //
    // The closure is computed and both halves are materialized -- the selection
    // against the accepted baseline, the remainder against the result -- before
    // anything is written. If either fails, nothing is.
    bool PromoteDraftGroups(const std::vector<std::string>& group_ids, std::string& error);

    // How far a rejection reaches past what the user picked.
    enum class DraftRejectionScope {
        // Reject the dependents too. The work goes, and goes visibly.
        Cascade,
        // Keep them, marked `NeedsAttention`. They leave materialization -- they
        // could not apply -- but stay in the workspace for their author to
        // retarget.
        StrandDependents,
    };

    // Rejects a selection, applying `scope` to whatever it would strand.
    bool RejectDraftGroups(const std::vector<std::string>& group_ids, DraftRejectionScope scope, std::string& error);

    // Starts a rejection, raising the choice above only when there is one to make.
    //
    // A rejection that strands nothing is applied immediately: a modal on every
    // rejection is a modal the user stops reading, and then the one that matters
    // is dismissed with the rest.
    void BeginRejectDraftGroups(const std::vector<std::string>& group_ids);
    void ResolvePendingDraftRejection(DraftRejectionScope scope);
    void CancelPendingDraftRejection();

    // Selects the first element a draft group changes and centres the canvas on
    // it. Does nothing when the group's changes are not on screen -- a stranded
    // group is not applied to the working model, so there is nothing to focus.
    void FocusDraftGroupOnCanvas(const std::string& group_id);

    // Deletes promotion snapshots the undo stack can no longer reach.
    //
    // The rule is the audit undo boundary and nothing else. `CanUndo` requires a
    // sequence strictly past the boundary, and a boundary only ever moves
    // forward as snapshots and baselines are taken -- so a promotion at or below
    // it is unreachable permanently, and its snapshot can never be consulted
    // again. Every cheaper rule (keep the last N, drop by age) can delete a
    // snapshot that is still reachable, which would destroy the only copy of
    // unaccepted work at the exact moment the user asked for it back.
    void PrunePromotionSnapshots();

    // Throws the whole draft away. The accepted `.sacm` is left byte-identical,
    // because nothing in the draft was ever applied to it.
    bool DiscardWorkingDraft(std::string& error);

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
