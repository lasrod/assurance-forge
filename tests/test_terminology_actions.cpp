#include "app/actions/terminology_actions.h"
#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "core/commands/command_bus.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_store.h"
#include "core/project_model.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <system_error>
#include <string>
#include <string_view>
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

// A case whose glossary term is already used as a visible context: the
// ArtifactReference points at the term, the AssertedContext sources the
// reference and targets the goal, and the context carries the visible-term
// marker. This is the shape the "add as visible context" action produces, and
// the one whose delete has consequences worth confirming.
constexpr const char* kTermWithVisibleContextSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <terminologyPackage id="TP1" name="Terms">
    <term id="T1" value="ODD" name="Operational Design Domain"/>
  </terminologyPackage>
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
    <artifactReference id="TC1" name="ODD: Operational Design Domain" referencedArtifact="T1"/>
    <assertedContext id="AC2" name="Context: ODD" source="TC1" target="G1"
                     description="assurance-forge:visible-term-context"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

// A throwaway project directory holding one SACM file with an initialized audit
// store, so a real CommandBus can be opened over it. The bus matters here: the
// delete preview is deliberately withheld when there is none.
struct ActionFixture {
    std::filesystem::path root;
    std::filesystem::path sacm_absolute;
    core::AssuranceProject project;

    ~ActionFixture() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::unique_ptr<ActionFixture> MakeActionFixture(const std::string& tag, std::string_view content) {
    auto fixture = std::make_unique<ActionFixture>();
    fixture->root =
        std::filesystem::temp_directory_path() /
        ("af_term_actions_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(fixture->root);
    std::filesystem::create_directories(fixture->root);

    const std::filesystem::path relative = "argument.sacm";
    fixture->sacm_absolute = fixture->root / relative;
    std::ofstream out(fixture->sacm_absolute, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();

    fixture->project.id = "p";
    fixture->project.name = "Project";
    fixture->project.rootPath = fixture->root;
    core::ProjectFileEntry entry;
    entry.id = "f1";
    entry.relativePath = relative;
    entry.role = core::ProjectFileRole::SacmArgument;
    fixture->project.files.push_back(entry);

    core::audit::EnsureAuditStoreResult ensure;
    std::string error;
    EXPECT_TRUE(core::audit::EnsureAuditStore(fixture->project, relative, ensure, error)) << error;
    return fixture;
}

// Loads the fixture into an app state with a live command bus, the way an open
// project reaches these actions.
void OpenWithBus(app::AppRuntimeState& state, ActionFixture& fixture) {
    ASSERT_TRUE(state.app_state.load_file(fixture.sacm_absolute.string())) << state.app_state.status_message;
    ASSERT_NE(state.app_state.library_document, nullptr);
    std::string error;
    state.command_bus = core::commands::CommandBus::Open(fixture.project, fixture.sacm_absolute, error);
    ASSERT_NE(state.command_bus, nullptr) << error;
}

std::string ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

// The confirmation dialog's data, end to end through the real action. Without
// this the cascade is only tested at the command layer, and the wiring that
// decides WHETHER the user is asked is exactly where a "are you sure?" feature
// goes wrong: ask about the wrong thing, or never ask at all.
TEST(TerminologyActions, BeginDeleteTermListsTheReferencesTheDeleteWouldRemove) {
    std::unique_ptr<ActionFixture> fixture = MakeActionFixture("visible_ctx", kTermWithVisibleContextSacm);
    app::AppRuntimeState state;
    OpenWithBus(state, *fixture);

    state.terminology.selected_package_ref = core::TerminologyPackageRef{"TP1", ""};
    app::actions::TerminologyActions(state).BeginDeleteTerm(core::TerminologyTermRef{"T1", ""});

    EXPECT_TRUE(state.terminology.show_delete_term_modal);
    EXPECT_TRUE(state.terminology.pending_delete_term_preview_available);
    std::vector<std::string> listed;
    for (const auto& effect : state.terminology.pending_delete_term_references)
        listed.push_back(effect.element_id);
    std::sort(listed.begin(), listed.end());
    EXPECT_EQ(listed, (std::vector<std::string>{"AC2", "TC1"}))
        << "the dialog would not name the visible-context pair it is about to remove";
}

// ...and confirming it removes exactly those, reporting the count in the status
// line. This is the whole feature, driven the way the UI drives it.
TEST(TerminologyActions, ConfirmDeleteTermRemovesTheReferencesItListed) {
    std::unique_ptr<ActionFixture> fixture = MakeActionFixture("confirm_cascade", kTermWithVisibleContextSacm);
    app::AppRuntimeState state;
    OpenWithBus(state, *fixture);
    std::vector<std::string> statuses;
    state.events.Subscribe<app::StatusMessageEvent>(
        [&](const app::StatusMessageEvent& event) { statuses.push_back(event.message); });

    state.terminology.selected_package_ref = core::TerminologyPackageRef{"TP1", ""};
    app::actions::TerminologyActions actions(state);
    actions.BeginDeleteTerm(core::TerminologyTermRef{"T1", ""});
    ASSERT_EQ(state.terminology.pending_delete_term_references.size(), 2u);
    ASSERT_TRUE(actions.ConfirmDeleteTerm());

    EXPECT_FALSE(state.terminology.show_delete_term_modal);
    EXPECT_TRUE(state.terminology.pending_delete_term_references.empty()) << "the consent outlived the delete";
    ASSERT_FALSE(statuses.empty());
    EXPECT_EQ(statuses.back(), "Deleted term and 2 elements that referenced it.")
        << "the status line does not say what was removed: " << statuses.back();

    const std::string saved = ReadWholeFile(fixture->sacm_absolute);
    EXPECT_EQ(saved.find("Operational Design Domain"), std::string::npos) << saved;
    EXPECT_EQ(saved.find("TC1"), std::string::npos) << "the reference survived in the saved file";
    EXPECT_EQ(saved.find("AC2"), std::string::npos) << "the context survived in the saved file";
    EXPECT_NE(saved.find("Top goal"), std::string::npos) << "the cascade overshot";
}

// ...and a term nothing references must not produce a dialog that asks for
// consent to remove nothing.
TEST(TerminologyActions, BeginDeleteTermAsksForNoConsentWhenNothingReferencesTheTerm) {
    constexpr const char* kUnreferencedTermSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <terminologyPackage id="TP1" name="Terms">
    <term id="T1" value="MRC" name="Minimal Risk Condition"/>
  </terminologyPackage>
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    std::unique_ptr<ActionFixture> fixture = MakeActionFixture("unreferenced", kUnreferencedTermSacm);
    app::AppRuntimeState state;
    OpenWithBus(state, *fixture);

    state.terminology.selected_package_ref = core::TerminologyPackageRef{"TP1", ""};
    app::actions::TerminologyActions(state).BeginDeleteTerm(core::TerminologyTermRef{"T1", ""});

    EXPECT_TRUE(state.terminology.show_delete_term_modal);
    EXPECT_TRUE(state.terminology.pending_delete_term_preview_available);
    EXPECT_TRUE(state.terminology.pending_delete_term_references.empty());
}

// A file opened outside a project reaches the commands with no library document
// (the tracked #347 exception), so the dispatch cannot cascade. The dialog must
// not offer what the delete would then refuse.
TEST(TerminologyActions, BeginDeleteTermOffersNoCascadeWithoutACommandBus) {
    std::unique_ptr<ActionFixture> fixture = MakeActionFixture("no_bus", kTermWithVisibleContextSacm);
    app::AppRuntimeState state;
    ASSERT_TRUE(state.app_state.load_file(fixture->sacm_absolute.string())) << state.app_state.status_message;
    ASSERT_NE(state.app_state.library_document, nullptr) << "no document, so this would pass for the wrong reason";
    ASSERT_EQ(state.command_bus, nullptr);

    state.terminology.selected_package_ref = core::TerminologyPackageRef{"TP1", ""};
    app::actions::TerminologyActions(state).BeginDeleteTerm(core::TerminologyTermRef{"T1", ""});

    EXPECT_TRUE(state.terminology.show_delete_term_modal);
    EXPECT_FALSE(state.terminology.pending_delete_term_preview_available);
    EXPECT_TRUE(state.terminology.pending_delete_term_references.empty());
}

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