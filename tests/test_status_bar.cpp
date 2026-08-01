#include "ui/panels/status_bar_panel.h"

#include <gtest/gtest.h>

#include <string>

namespace {

ui::panels::StatusBarModel OpenProject() {
    ui::panels::StatusBarModel model;
    model.has_project = true;
    model.project_name = "MySafetyCase";
    model.document_name = "main.sacm";
    model.document_full_path = "C:/projects/MySafetyCase/arguments/main.sacm";
    return model;
}

TEST(StatusBarLabel, CombinesProjectAndDocument) {
    const std::string label = ui::panels::DocumentLabel(OpenProject());
    EXPECT_NE(label.find("MySafetyCase"), std::string::npos);
    EXPECT_NE(label.find("main.sacm"), std::string::npos);
    // The document must come after the project: the bar reads left to right as
    // container then content.
    EXPECT_LT(label.find("MySafetyCase"), label.find("main.sacm"));
}

TEST(StatusBarLabel, DegradesWhenOnlyOneHalfIsKnown) {
    ui::panels::StatusBarModel project_only = OpenProject();
    project_only.document_name.clear();
    EXPECT_EQ(ui::panels::DocumentLabel(project_only), "MySafetyCase");

    ui::panels::StatusBarModel document_only = OpenProject();
    document_only.project_name.clear();
    EXPECT_EQ(ui::panels::DocumentLabel(document_only), "main.sacm");
}

TEST(StatusBarLabel, SaysSomethingWhenNothingIsOpen) {
    // An empty status bar is indistinguishable from a broken one.
    EXPECT_FALSE(ui::panels::DocumentLabel(ui::panels::StatusBarModel{}).empty());
}

// The point of the bar for a tool whose output is a safety argument: saved and
// unsaved must never render as the same string.
TEST(StatusBarSaveState, DistinguishesSavedFromUnsaved) {
    ui::panels::StatusBarModel saved = OpenProject();
    saved.has_unsaved_changes = false;

    ui::panels::StatusBarModel unsaved = OpenProject();
    unsaved.has_unsaved_changes = true;

    const std::string saved_label = ui::panels::SaveStateLabel(saved);
    const std::string unsaved_label = ui::panels::SaveStateLabel(unsaved);

    EXPECT_FALSE(saved_label.empty());
    EXPECT_FALSE(unsaved_label.empty());
    EXPECT_NE(saved_label, unsaved_label);
}

TEST(StatusBarSaveState, ClaimsNothingWhenNothingIsOpen) {
    // With no document there is no save state to report, and reporting "Saved"
    // would be a claim about a file that does not exist.
    EXPECT_TRUE(ui::panels::SaveStateLabel(ui::panels::StatusBarModel{}).empty());
}

} // namespace
