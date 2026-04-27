#pragma once

#include <string>

#include "core/element_factory.h"

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
    void RenderElementPropertiesPanel(float center_x, float center_w, float right_w, float content_h, float top_y);
    void RenderStartupProjectWindow();
    void RenderNotImplementedModal();
    void RenderRemoveConfirmModal();

    void RebuildDerivedViewsIfNeeded();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace app
