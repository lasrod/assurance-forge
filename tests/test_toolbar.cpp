#include "ui/panels/toolbar_panel.h"

#include <gtest/gtest.h>

namespace {

using ui::panels::IsActionEnabled;
using ui::panels::ToolbarAction;
using ui::panels::ToolbarModel;

ToolbarModel LoadedProject() {
    ToolbarModel model;
    model.has_project = true;
    model.has_loaded_case = true;
    model.gsn_canvas_active = true;
    return model;
}

// A toolbar button that looks available but does nothing is worse than no
// button, so the enablement rules are asserted rather than eyeballed.
TEST(Toolbar, OffersOnlyEscapeHatchesWithNoProject) {
    const ToolbarModel empty;
    EXPECT_TRUE(IsActionEnabled(empty, ToolbarAction::OpenProject));
    EXPECT_TRUE(IsActionEnabled(empty, ToolbarAction::Preferences));

    EXPECT_FALSE(IsActionEnabled(empty, ToolbarAction::SaveProject));
    EXPECT_FALSE(IsActionEnabled(empty, ToolbarAction::NewSacmFile));
    EXPECT_FALSE(IsActionEnabled(empty, ToolbarAction::ExportGsnSvg));
    EXPECT_FALSE(IsActionEnabled(empty, ToolbarAction::Undo));
    EXPECT_FALSE(IsActionEnabled(empty, ToolbarAction::FitToView));
}

TEST(Toolbar, EnablesDocumentActionsOnceAProjectIsOpen) {
    const ToolbarModel model = LoadedProject();
    EXPECT_TRUE(IsActionEnabled(model, ToolbarAction::SaveProject));
    EXPECT_TRUE(IsActionEnabled(model, ToolbarAction::NewSacmFile));
    EXPECT_TRUE(IsActionEnabled(model, ToolbarAction::ExportGsnSvg));
}

// Export writes the GSN of a loaded case; a project with no case open has
// nothing to export, and offering it would produce an empty or failed file.
TEST(Toolbar, RequiresALoadedCaseToExport) {
    ToolbarModel model = LoadedProject();
    model.has_loaded_case = false;
    EXPECT_FALSE(IsActionEnabled(model, ToolbarAction::ExportGsnSvg));
    EXPECT_TRUE(IsActionEnabled(model, ToolbarAction::SaveProject));
}

TEST(Toolbar, TracksUndoAvailabilityRatherThanProjectState) {
    ToolbarModel model = LoadedProject();
    EXPECT_FALSE(IsActionEnabled(model, ToolbarAction::Undo));
    model.can_undo = true;
    EXPECT_TRUE(IsActionEnabled(model, ToolbarAction::Undo));
}

// Fit acts on the GSN canvas, so it must follow the active center view rather
// than merely whether a case is loaded.
TEST(Toolbar, OffersFitOnlyWhileTheCanvasIsShowing) {
    ToolbarModel model = LoadedProject();
    EXPECT_TRUE(IsActionEnabled(model, ToolbarAction::FitToView));
    model.gsn_canvas_active = false;
    EXPECT_FALSE(IsActionEnabled(model, ToolbarAction::FitToView));
}

} // namespace
