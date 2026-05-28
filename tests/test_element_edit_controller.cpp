#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "app/controllers/element_edit_controller.h"

#include "core/audit/audit_paths.h"
#include "core/audit/audit_store.h"
#include "core/audit/event_store.h"
#include "core/commands/command_bus.h"
#include "core/project_model.h"
#include "sacm/sacm_parser.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

TEST(ElementEditControllerTest, AddTopGoalUpdatesModelAndEmitsEvents) {
    app::AppRuntimeState state;
    state.app_state.loaded_case = parser::AssuranceCase{};
    state.app_state.sacm_package = sacm::AssuranceCasePackage{};

    std::string selected_id;
    bool tree_dirty = false;
    bool document_dirty = false;
    std::string status;
    state.events.Subscribe<app::TreeDirtyEvent>([&](const app::TreeDirtyEvent&) { tree_dirty = true; });
    state.events.Subscribe<app::DocumentDirtyEvent>([&](const app::DocumentDirtyEvent&) { document_dirty = true; });
    state.events.Subscribe<app::SelectionChangedEvent>(
        [&](const app::SelectionChangedEvent& event) { selected_id = event.element_id; });
    state.events.Subscribe<app::StatusMessageEvent>(
        [&](const app::StatusMessageEvent& event) { status = event.message; });

    ASSERT_TRUE(state.element_edit_controller->AddTopGoal(state));

    const parser::AssuranceCase& model = state.app_state.loaded_case.value();
    ASSERT_EQ(model.elements.size(), 1u);
    EXPECT_EQ(model.elements.front().type, "claim");
    EXPECT_EQ(selected_id, model.elements.front().id);
    EXPECT_TRUE(tree_dirty);
    EXPECT_TRUE(document_dirty);
    EXPECT_EQ(status, "Added " + model.elements.front().id);
}

TEST(ElementEditControllerTest, AddChildWithoutSelectionEmitsStatusOnly) {
    app::AppRuntimeState state;
    bool tree_dirty = false;
    std::string status;
    state.events.Subscribe<app::TreeDirtyEvent>([&](const app::TreeDirtyEvent&) { tree_dirty = true; });
    state.events.Subscribe<app::StatusMessageEvent>(
        [&](const app::StatusMessageEvent& event) { status = event.message; });

    EXPECT_FALSE(state.element_edit_controller->AddChildToSelected(state, "", core::NewElementKind::Goal));

    EXPECT_FALSE(state.app_state.loaded_case.has_value());
    EXPECT_FALSE(tree_dirty);
    EXPECT_EQ(status, "No element selected.");
}

// Regression: the panel passes `new_value` as a reference into the parser
// model's own string (ImGui's per-keystroke binding mutates the model in
// place). The controller's revert step writes through the same memory.
// Before the fix, `new_value` aliased that memory, so the audited command
// saw new == old and recorded a no-op transaction while the visible text
// reverted to the original. This test reproduces that scenario.
TEST(ElementEditControllerTest, CommitElementTextEditHandlesAliasedNewValueReference) {
    namespace fs = std::filesystem;
    constexpr const char* kSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    const fs::path root = fs::temp_directory_path() /
                          ("af_text_alias_" +
                           std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path sacm_rel = "argument.sacm";
    {
        std::ofstream out(root / sacm_rel, std::ios::binary);
        out << kSacm;
    }

    core::AssuranceProject project;
    project.id = "p";
    project.name = "Project";
    project.rootPath = root;
    core::ProjectFileEntry entry;
    entry.id = "f1";
    entry.relativePath = sacm_rel;
    entry.role = core::ProjectFileRole::SacmArgument;
    project.files.push_back(entry);

    core::audit::EnsureAuditStoreResult ensure;
    std::string error;
    ASSERT_TRUE(core::audit::EnsureAuditStore(project, sacm_rel, ensure, error)) << error;

    const fs::path sacm_abs = root / sacm_rel;
    auto pkg = sacm::parse_sacm(sacm_abs.string());
    ASSERT_TRUE(pkg.has_value());
    auto parsed = parser::parse_sacm_xml_string(kSacm);
    ASSERT_TRUE(parsed.has_value());

    auto bus = core::commands::CommandBus::Open(project, sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    app::AppRuntimeState state;
    state.app_state.loaded_case  = std::move(parsed.value());
    state.app_state.sacm_package = std::move(pkg.value());
    state.command_bus            = std::move(bus);

    // Locate the element and snapshot its original description.
    parser::AssuranceCase& model = state.app_state.loaded_case.value();
    parser::SacmElement* elem = nullptr;
    for (auto& e : model.elements) {
        if (e.id == "G1") { elem = &e; break; }
    }
    ASSERT_NE(elem, nullptr);
    const std::string original_value = elem->description;
    ASSERT_EQ(original_value, "The system is safe.");

    // Simulate ImGui's per-keystroke binding: the InputText writes the
    // new value straight into elem->description before the deactivation
    // callback fires.
    elem->description = "Edited by the user.";
    elem->description_langs["en"] = elem->description;

    // Call the controller exactly the way the runtime does: `new_value` is
    // a reference to elem->description itself. This is the aliasing
    // scenario that produced the no-op bug.
    const bool committed = state.element_edit_controller->CommitElementTextEdit(
        state, "G1", "description", "en", original_value, elem->description);
    EXPECT_TRUE(committed);

    // The edit must persist in the live model.
    EXPECT_EQ(elem->description, "Edited by the user.");
    EXPECT_EQ(elem->description_langs.at("en"), "Edited by the user.");

    // The audit log must contain one UpdateElementText event with the
    // correct old/new values — not a no-op.
    const auto& transactions = state.command_bus->Store().Transactions();
    ASSERT_FALSE(transactions.empty());
    const auto& tx = transactions.back();
    ASSERT_FALSE(tx.events.empty());
    const auto& ev = tx.events.back();
    EXPECT_EQ(ev.event_type, "UpdateElementText");
    EXPECT_EQ(ev.payload.at("element_id").get<std::string>(), "G1");
    EXPECT_EQ(ev.payload.at("field").get<std::string>(), "description");
    EXPECT_EQ(ev.payload.at("language").get<std::string>(), "en");
    EXPECT_EQ(ev.payload.at("old_value").get<std::string>(), "The system is safe.");
    EXPECT_EQ(ev.payload.at("new_value").get<std::string>(), "Edited by the user.");

    // Release the bus before deleting the audit-store directory so file
    // handles to the transaction log are closed (Windows).
    state.command_bus.reset();
    fs::remove_all(root);
}
