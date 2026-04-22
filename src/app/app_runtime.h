#pragma once

#include <string>

#include "core/element_factory.h"

namespace app {

// Request that the active AppRuntime add a child of the given kind to the
// currently selected element. Safe to call from any UI code; no-op if no
// runtime is active or no element is selected.
void RequestAddChild(core::NewElementKind kind);

// Request that the active AppRuntime show a "not implemented yet" status for
// the given feature name.
void RequestNotImplemented(const char* feature);

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

    // Set a transient status message (shown next frame in the SACM viewer panel).
    void SetStatus(const std::string& message);

    // Show the "not implemented" modal for the given feature name.
    void ShowNotImplementedModal(const std::string& feature);

private:
    void ScanDirectory();
    void RenderSplitters(float display_w, float display_h, float left_w, float center_w);
    void RenderTreePanel(float left_w, float top_left_h);
    void RenderSacmViewerPanel(float left_w, float top_left_h, float bottom_left_h, bool& done);
    void RenderCenterPanel(float center_x, float center_w, float display_h);
    void RenderElementPropertiesPanel(float center_x, float center_w, float right_w);
    void RenderNotImplementedModal();

    void RebuildDerivedViewsIfNeeded();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace app
