#pragma once

#include <string>

#include "core/element_factory.h"
#include "core/project_model.h"

namespace app {

class AppRuntime {
public:
    AppRuntime();
    ~AppRuntime();

    AppRuntime(const AppRuntime&) = delete;
    AppRuntime& operator=(const AppRuntime&) = delete;

    void RenderFrame(bool& done);

    // Add a new child element to the currently selected element.
    // Returns true on success; updates selection to the new element.
    bool AddChildToSelected(core::NewElementKind kind);

    // Add a new top-level Goal (root claim) to the current model.
    bool AddTopGoal();

    // Remove the currently selected element using the given mode. If the
    // planned removal targets more than one element, opens the confirmation
    // modal (with canvas highlight + fit-to-view) instead of removing.
    void RemoveSelected(core::RemoveMode mode);

    // Set a transient status message (shown next frame in the SACM viewer panel).
    void SetStatus(const std::string& message);

    // Show the "not implemented" modal for the given feature name.
    void ShowNotImplementedModal(const std::string& feature);

    // Returns the currently loaded assurance case, or nullptr if none.
    const parser::AssuranceCase* GetLoadedCase() const;

private:
    float RenderMainMenuBar(bool& done);
    void ScanDirectory();
    void RenderSplitters(float display_w, float content_h, float left_w, float center_w, float top_y);
    void RenderTreePanel(float left_w, float safety_tree_h, float top_y);
    void RenderSacmViewerPanel(float left_w, float sacm_h, float top_y);
    void RenderCenterPanel(float center_x, float center_w, float content_h, float top_y);
    void RenderProblemsPanel(float center_x, float center_w, float problems_h, float top_y);
    void RenderElementPropertiesPanel(float center_x, float center_w, float right_w, float content_h, float top_y);
    void RenderStartupProjectWindow();
    void RenderNotImplementedModal();
    void RenderRemoveConfirmModal();
    void RenderCreateProjectModal();
    void RenderOpenProjectModal();
    void RenderProjectFileNameModal();
    void RenderProjectLoadReportModal();
    void RenderSaveBeforeExitModal(bool& done);
    void RenderPreferencesWindow();
    void RenderAiReviewDebugModal();

    void BeginCreateProject();
    void BeginOpenProject();
    void BeginCreateProjectSacmFile();
    void BeginCreateProjectEvidenceRegister();
    void BeginCreateProjectJ3377CaeRegister();
    void OpenProjectFile(const core::ProjectFileEntry& entry);
    bool OpenFirstProjectSacmFile();
    bool SaveProject();
    void RequestExit(bool& done);

    void RebuildDerivedViewsIfNeeded();
    void BeginAiReviewForSelection();
    void StartPendingAiReviewRequest();
    void PollAiReviewTask();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace app
