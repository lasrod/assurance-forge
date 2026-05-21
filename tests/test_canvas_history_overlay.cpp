// Tests for the per-canvas-tab history overlay state.
//
// The overlay's rendering pipeline depends on ImGui and a live audit store
// and therefore cannot be exercised end-to-end without a windowed test
// harness. These tests cover the data-layer guarantees the overlay relies
// on: per-tab state isolation (each open ArgumentPackage canvas keeps its
// own history toggle and pinned-transaction selection) and safe cleanup
// when a tab is closed.

#include "app/app_runtime_state.h"
#include "app/areas/canvas_history_overlay.h"

#include <gtest/gtest.h>

namespace {

TEST(CanvasHistoryOverlayState, TabDefaults) {
    app::WorkbenchState::ArgumentPackageCanvasTab tab;
    EXPECT_FALSE(tab.show_history);
    EXPECT_FALSE(tab.selected_transaction_sequence.has_value());
}

TEST(CanvasHistoryOverlayState, TabsAreIndependent) {
    app::WorkbenchState workbench;
    workbench.argument_package_canvas_tabs.push_back({/*key=*/"tab-a",
                                                      /*package_id=*/"AP1",
                                                      /*package_gid=*/"AP1-gid",
                                                      /*title=*/"Main",
                                                      /*source_file_path=*/{}});
    workbench.argument_package_canvas_tabs.push_back({/*key=*/"tab-b",
                                                      /*package_id=*/"AP2",
                                                      /*package_gid=*/"AP2-gid",
                                                      /*title=*/"ACP",
                                                      /*source_file_path=*/{}});

    auto& a = workbench.argument_package_canvas_tabs[0];
    auto& b = workbench.argument_package_canvas_tabs[1];

    a.show_history = true;
    a.selected_transaction_sequence = 5;

    EXPECT_TRUE(a.show_history);
    EXPECT_EQ(a.selected_transaction_sequence.value(), 5u);
    EXPECT_FALSE(b.show_history);
    EXPECT_FALSE(b.selected_transaction_sequence.has_value());

    b.show_history = true;
    b.selected_transaction_sequence = 2;
    EXPECT_EQ(a.selected_transaction_sequence.value(), 5u);
    EXPECT_EQ(b.selected_transaction_sequence.value(), 2u);
}

TEST(CanvasHistoryOverlayState, ForgetCanvasHistoryTabIsSafeForUnknownKey) {
    // The cleanup hook is invoked from the tab-close path; calling it for
    // a tab that never opened the overlay must not crash or throw.
    EXPECT_NO_THROW(app::areas::ForgetCanvasHistoryTab("never-opened"));
    EXPECT_NO_THROW(app::areas::ForgetCanvasHistoryTab(""));
}

} // namespace
