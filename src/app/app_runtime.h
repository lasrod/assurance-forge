#pragma once

#include <string>

#include "core/element_factory.h"

namespace app {

// Request that the active AppRuntime add a child of the given kind to the
// currently selected element. Safe to call from any UI code; no-op if no
// runtime is active or no element is selected.
void RequestAddChild(core::NewElementKind kind);

// Request that the active AppRuntime remove the currently selected element
// using the given mode. If the planned removal targets a single element it is
// removed immediately; otherwise the targeted nodes are highlighted on the
// canvas, the view fits to them, and a confirmation modal is shown.
void RequestRemove(core::RemoveMode mode);

// Request that the active AppRuntime show a "not implemented yet" status for
// the given feature name.
void RequestNotImplemented(const char* feature);

// Returns a pointer to the currently loaded assurance case, or nullptr if no
// case is loaded. Used by UI menus that need to compute model-derived labels
// (e.g. the Remove submenu's element counts) without owning model access.
const parser::AssuranceCase* GetActiveAssuranceCase();

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
    void ScanDirectory();
    void RenderSplitters(float display_w, float display_h, float left_w, float center_w);
    void RenderTreePanel(float left_w, float top_left_h);
    void RenderSacmViewerPanel(float left_w, float top_left_h, float bottom_left_h, bool& done);
    void RenderCenterPanel(float center_x, float center_w, float display_h);
    void RenderElementPropertiesPanel(float center_x, float center_w, float right_w);
    void RenderNotImplementedModal();
    void RenderRemoveConfirmModal();

    void RebuildDerivedViewsIfNeeded();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace app
