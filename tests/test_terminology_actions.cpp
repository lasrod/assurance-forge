#include "app/actions/terminology_actions.h"
#include "app/app_events.h"
#include "app/app_runtime_state.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

sacm::AssuranceCasePackage MakePackageWithoutTerminology() {
    sacm::AssuranceCasePackage package;
    package.id = "ACP1";
    package.gid = "gid-ACP1";
    package.name = "Assurance Case";

    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    argument_package.gid = "gid-AP1";
    sacm::Claim claim;
    claim.id = "G1";
    claim.gid = "gid-G1";
    argument_package.claims.push_back(claim);
    package.argumentPackages.push_back(argument_package);

    return package;
}

} // namespace

TEST(TerminologyActions, QuickDefineCreatesTerminologyPackageWhenMissing) {
    app::AppRuntimeState state;
    state.app_state.sacm_package = MakePackageWithoutTerminology();
    std::vector<std::string> statuses;
    int document_dirty_events = 0;
    int tree_dirty_events = 0;
    state.events.Subscribe<app::StatusMessageEvent>(
        [&](const app::StatusMessageEvent& event) { statuses.push_back(event.message); });
    state.events.Subscribe<app::DocumentDirtyEvent>([&](const app::DocumentDirtyEvent&) { ++document_dirty_events; });
    state.events.Subscribe<app::TreeDirtyEvent>([&](const app::TreeDirtyEvent&) { ++tree_dirty_events; });

    app::actions::TerminologyActions(state).BeginQuickDefineTerm("G1", " ODD ");

    ASSERT_TRUE(state.app_state.sacm_package.has_value());
    ASSERT_EQ(state.app_state.sacm_package->terminologyPackages.size(), 1u);
    const sacm::TerminologyPackage& created = state.app_state.sacm_package->terminologyPackages.front();
    EXPECT_EQ(created.name, "Terminology Package");
    EXPECT_EQ(created.description, "Terms used by this safety case.");
    EXPECT_EQ(state.terminology.quick_define_target_package_ref.id, created.id);
    EXPECT_EQ(state.terminology.selected_package_ref.id, created.id);
    EXPECT_TRUE(state.terminology.show_quick_define_term_modal);
    EXPECT_EQ(std::string(state.terminology.term_value_buf), "ODD");
    EXPECT_TRUE(state.app_state.has_unsaved_changes);
    EXPECT_EQ(document_dirty_events, 1);
    EXPECT_EQ(tree_dirty_events, 1);
    ASSERT_FALSE(statuses.empty());
    EXPECT_EQ(statuses.back(), "Created a TerminologyPackage for new terms.");
}

TEST(TerminologyActions, QuickDefineUsesExistingTerminologyPackage) {
    app::AppRuntimeState state;
    state.app_state.sacm_package = MakePackageWithoutTerminology();
    sacm::TerminologyPackage terminology_package;
    terminology_package.id = "TP1";
    terminology_package.gid = "gid-TP1";
    terminology_package.name = "Glossary";
    state.app_state.sacm_package->terminologyPackages.push_back(terminology_package);
    int document_dirty_events = 0;
    int tree_dirty_events = 0;
    state.events.Subscribe<app::DocumentDirtyEvent>([&](const app::DocumentDirtyEvent&) { ++document_dirty_events; });
    state.events.Subscribe<app::TreeDirtyEvent>([&](const app::TreeDirtyEvent&) { ++tree_dirty_events; });

    app::actions::TerminologyActions(state).BeginQuickDefineTerm("G1", "ODD");

    ASSERT_TRUE(state.app_state.sacm_package.has_value());
    EXPECT_EQ(state.app_state.sacm_package->terminologyPackages.size(), 1u);
    EXPECT_EQ(state.terminology.quick_define_target_package_ref.id, "TP1");
    EXPECT_TRUE(state.terminology.show_quick_define_term_modal);
    EXPECT_FALSE(state.app_state.has_unsaved_changes);
    EXPECT_EQ(document_dirty_events, 0);
    EXPECT_EQ(tree_dirty_events, 0);
}