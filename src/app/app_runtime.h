#pragma once

#include "app/app_events.h"
#include "core/element_factory.h"
#include "core/project_model.h"
#include "core/reviews/review_item.h"
#include "core/terminology_package_service.h"
#include "core/tree_editing.h"
#include "sacm/sacm_package_tree.h"

#include <string>
#include <vector>

namespace app {

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

    float RenderMainMenuBar(bool& done);
    void ScanDirectory();
    void RenderTreePanel(float left_w, float safety_tree_h, float top_y);
    void RenderSacmViewerPanel(float left_w, float sacm_h, float top_y);
    void RenderCenterPanel(float center_x, float center_w, float content_h, float top_y);
    void RenderProblemsPanel(float center_x, float center_w, float problems_h, float top_y);
    void RenderElementPropertiesPanel(float center_x, float center_w, float right_w, float content_h, float top_y);
    void RenderReviewPanelContent();
    void RenderAiDebugPanelContent();
    void RenderProposalElementEditor();
    void RenderStartupProjectWindow();
    void RenderNotImplementedModal();
    void RenderRemoveConfirmModal();
    void RenderDeleteReviewItemConfirmModal();
    void RenderReviewerNamePromptModal();
    void RenderCreateProjectModal();
    void RenderProjectFileNameModal();
    void RenderProjectLoadReportModal();
    void RenderSaveBeforeExitModal(bool& done);
    void RenderCreateTerminologyPackageModal();
    void RenderDeleteTerminologyPackageModal();
    void RenderTerminologyTermEditorModal();
    void RenderDeleteTerminologyTermModal();
    void RenderPreferencesWindow();
    void RenderThemeTweaksWindow();

    void BeginCreateProject();
    void BeginOpenProject();
    void BeginCreateProjectSacmFile();
    void BeginCreateProjectEvidenceRegister();
    void BeginCreateProjectJ3377CaeRegister();
    void OpenProjectFile(const core::ProjectFileEntry& entry);
    void OpenProjectPackageNode(const core::ProjectFileEntry& entry, const sacm::SacmPackageTreeNode& node);
    void BeginAddTerminologyPackage(const core::ProjectFileEntry& entry, const sacm::SacmPackageTreeNode& parent_node);
    void ConfirmAddTerminologyPackage();
    void ApplyTerminologyPackageEdits();
    void BeginDeleteTerminologyPackage();
    void ConfirmDeleteTerminologyPackage();
    void SelectTerminologyTerm(const core::TerminologyTermRef& term_ref);
    void BeginAddTerminologyTerm();
    void BeginEditTerminologyTerm(const core::TerminologyTermRef& term_ref);
    void ConfirmTerminologyTermEdit();
    void BeginDeleteTerminologyTerm(const core::TerminologyTermRef& term_ref);
    void ConfirmDeleteTerminologyTerm();
    bool OpenFirstProjectSacmFile();
    bool TryOpenProjectManifest(const std::string& selected_path);
    bool EnsureReviewItemStorage();
    void RefreshSacmPackageTreeCache();
    void SyncReviewProblems();
    void SyncReviewVisualStatesFromReviews();
    void TouchCurrentProjectRecent();
    bool SaveProject();
    void RequestExit(bool& done);

    bool BeginProposalForReviewItem(const core::reviews::ReviewItem& item);
    bool BeginEditProposalForReviewItem(const core::reviews::ReviewItem& item);
    bool BeginEditProposalById(const std::string& proposal_id);
    bool PreviewProposalById(const std::string& proposal_id);
    bool SaveActiveProposal(const core::reviews::ReviewItem& item);
    void CreateAiGeneratedProposals(const std::vector<AiReviewProposalSuggestion>& suggestions);
    bool SetManualReviewOk(const std::string& element_id, bool manual_ok);
    void CancelActiveProposal();
    void MarkReviewItemsDirty();
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
    AppRuntimeState* impl_ = nullptr;
};

} // namespace app
