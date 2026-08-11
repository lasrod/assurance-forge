#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "app/controllers/element_edit_controller.h"

#include "core/audit/audit_paths.h"
#include "core/audit/audit_store.h"
#include "core/audit/event_store.h"
#include "core/audit/replay_verifier.h"
#include "core/commands/command_bus.h"
#include "core/element_factory.h"
#include "core/project_model.h"
#include "parser/model_utils.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"
#include "legacy_sacm/sacm_parser.h"
#include "ui/text_edit_session.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <vector>

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

TEST(ElementEditControllerTest, GSN3_CORE_010_CommitsNotationIdentifierWithoutRenamingStorageId) {
    app::AppRuntimeState state;
    parser::SacmElement goal;
    goal.id = "generated_3";
    goal.gsn_identifier = "G1";
    goal.type = "claim";
    state.app_state.loaded_case = parser::AssuranceCase{};
    state.app_state.loaded_case->elements.push_back(goal);

    sacm::Claim stored_goal;
    stored_goal.id = "generated_3";
    sacm::ArgumentPackage argument_package;
    argument_package.claims.push_back(stored_goal);
    state.app_state.sacm_package = sacm::AssuranceCasePackage{};
    state.app_state.sacm_package->argumentPackages.push_back(argument_package);

    parser::SacmElement& edited = state.app_state.loaded_case->elements.front();
    edited.gsn_identifier = "SYS-GOAL";
    ASSERT_TRUE(state.element_edit_controller->CommitElementTextEdit(
        state, edited.id, "gsn_identifier", "none", "G1", edited.gsn_identifier));

    EXPECT_EQ(edited.id, "generated_3");
    EXPECT_EQ(edited.gsn_identifier, "SYS-GOAL");
    const sacm::Claim& stored = state.app_state.sacm_package->argumentPackages.front().claims.front();
    EXPECT_EQ(stored.id, "generated_3");
    ASSERT_EQ(stored.taggedValues.size(), 1u);
    EXPECT_EQ(stored.taggedValues.front().value, "SYS-GOAL");
}

// Regression: the controller reverts the panel's in-place edit before
// dispatching the audited command. Reverting the SACM package too would upsert
// the vendor TaggedValue onto an element that carries none, so a rejected edit
// left a tag behind in the document — a silent modification of the argument for
// an edit the user never landed.
TEST(ElementEditControllerTest, GSN3_CORE_010_RejectedIdentifierEditLeavesPackageUntouched) {
    app::AppRuntimeState state;
    parser::SacmElement goal;
    goal.id = "generated_3";
    goal.gsn_identifier = "G1";
    goal.type = "claim";
    parser::SacmElement sibling;
    sibling.id = "generated_4";
    sibling.gsn_identifier = "G2";
    sibling.type = "claim";
    state.app_state.loaded_case = parser::AssuranceCase{};
    state.app_state.loaded_case->elements.push_back(goal);
    state.app_state.loaded_case->elements.push_back(sibling);

    // Both claims are imported without the vendor tag, as a third-party SACM
    // document would be.
    sacm::ArgumentPackage argument_package;
    sacm::Claim stored_goal;
    stored_goal.id = "generated_3";
    argument_package.claims.push_back(stored_goal);
    sacm::Claim stored_sibling;
    stored_sibling.id = "generated_4";
    argument_package.claims.push_back(stored_sibling);
    state.app_state.sacm_package = sacm::AssuranceCasePackage{};
    state.app_state.sacm_package->argumentPackages.push_back(argument_package);

    // The user retypes G1 as G2, which the sibling already owns.
    parser::SacmElement& edited = state.app_state.loaded_case->elements.front();
    edited.gsn_identifier = "G2";
    EXPECT_FALSE(state.element_edit_controller->CommitElementTextEdit(
        state, edited.id, "gsn_identifier", "none", "G1", edited.gsn_identifier));

    EXPECT_EQ(edited.gsn_identifier, "G1");
    const sacm::ArgumentPackage& package = state.app_state.sacm_package->argumentPackages.front();
    EXPECT_TRUE(package.claims.front().taggedValues.empty());
    EXPECT_TRUE(package.claims.back().taggedValues.empty());
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
    <claim id="G1" name="Top goal" content="The system is safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    const fs::path root = fs::temp_directory_path() /
                          ("af_text_alias_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
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
    state.app_state.loaded_case = std::move(parsed.value());
    state.app_state.sacm_package = std::move(pkg.value());
    state.command_bus = std::move(bus);

    // Locate the element and snapshot its original statement. G1 is a claim, so
    // its text lives in `content` -- a claim has no note field (ADR 0012).
    parser::AssuranceCase& model = state.app_state.loaded_case.value();
    parser::SacmElement* elem = nullptr;
    for (auto& e : model.elements) {
        if (e.id == "G1") {
            elem = &e;
            break;
        }
    }
    ASSERT_NE(elem, nullptr);
    const std::string original_value = elem->content;
    ASSERT_EQ(original_value, "The system is safe.");

    // Simulate ImGui's per-keystroke binding: the InputText writes the
    // new value straight into elem->description before the deactivation
    // callback fires.
    elem->content = "Edited by the user.";
    elem->content_langs["en"] = elem->content;

    // Call the controller exactly the way the runtime does: `new_value` is
    // a reference to elem->description itself. This is the aliasing
    // scenario that produced the no-op bug.
    const bool committed = state.element_edit_controller->CommitElementTextEdit(
        state, "G1", "content", "en", original_value, elem->content);
    EXPECT_TRUE(committed);

    // The edit must persist in the live model.
    EXPECT_EQ(elem->content, "Edited by the user.");
    EXPECT_EQ(elem->content_langs.at("en"), "Edited by the user.");

    // The audit log must contain one UpdateElementText event with the
    // correct old/new values — not a no-op.
    const auto& transactions = state.command_bus->Store().Transactions();
    ASSERT_FALSE(transactions.empty());
    const auto& tx = transactions.back();
    ASSERT_FALSE(tx.events.empty());
    const auto& ev = tx.events.back();
    EXPECT_EQ(ev.event_type, "UpdateElementText");
    EXPECT_EQ(ev.payload.at("element_id").get<std::string>(), "G1");
    EXPECT_EQ(ev.payload.at("field").get<std::string>(), "content");
    EXPECT_EQ(ev.payload.at("language").get<std::string>(), "en");
    EXPECT_EQ(ev.payload.at("old_value").get<std::string>(), "The system is safe.");
    EXPECT_EQ(ev.payload.at("new_value").get<std::string>(), "Edited by the user.");

    // Release the bus before deleting the audit-store directory so file
    // handles to the transaction log are closed (Windows).
    state.command_bus.reset();
    fs::remove_all(root);
}

// Regression: a focused inspector field that never saw ImGui's deactivation
// transition (window closed mid-edit, or programmatic navigation away) leaves
// the typed value in the model but with no audit transaction. Previously the
// un-audited SaveProject path wrote that value to the SACM, and the next open
// reported "Audit log divergence detected". FlushPendingTextEdits must turn
// the pending edit into an audited transaction so replay still matches disk.
TEST(ElementEditControllerTest, FlushPendingTextEditsCommitsUncommittedEditWithoutDivergence) {
    namespace fs = std::filesystem;
    constexpr const char* kSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    const fs::path root = fs::temp_directory_path() /
                          ("af_flush_pending_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
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
    auto bus = core::commands::CommandBus::Open(project, sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    app::AppRuntimeState state;
    // Load through the production path (library-owned document -> projected views)
    // so the in-memory package matches what the audit verifier reconstructs from
    // the (now library-projected) snapshot. A manual sacm::parse_sacm base would
    // diverge from the library projection on a description edit's language handling.
    // Phase 1b.
    ASSERT_TRUE(state.app_state.load_file(sacm_abs.string())) << state.app_state.status_message;
    state.command_bus = std::move(bus);

    parser::AssuranceCase& model = state.app_state.loaded_case.value();
    parser::SacmElement* elem = nullptr;
    for (auto& e : model.elements) {
        if (e.id == "G1") {
            elem = &e;
            break;
        }
    }
    ASSERT_NE(elem, nullptr);
    const std::string original_value = elem->content;

    // Simulate the live, uncommitted keystroke edit: ImGui's per-keystroke
    // binding wrote the new value into the model, but the deactivation commit
    // never fired (no audit transaction yet).
    elem->content = "Edited but never deactivated.";
    elem->content_langs["en"] = elem->content;

    // The forced-flush path hands the controller the still-open edit.
    std::vector<ui::PendingTextEdit> pending;
    pending.push_back(ui::PendingTextEdit{"G1", "content", "en", original_value, elem->content});
    const int committed = state.element_edit_controller->FlushPendingTextEdits(state, pending);
    EXPECT_EQ(committed, 1);

    // The audit log now carries the edit as a real transaction.
    const auto& transactions = state.command_bus->Store().Transactions();
    ASSERT_FALSE(transactions.empty());
    const auto& ev = transactions.back().events.back();
    EXPECT_EQ(ev.event_type, "UpdateElementText");
    EXPECT_EQ(ev.payload.at("element_id").get<std::string>(), "G1");
    EXPECT_EQ(ev.payload.at("old_value").get<std::string>(), original_value);
    EXPECT_EQ(ev.payload.at("new_value").get<std::string>(), "Edited but never deactivated.");

    // Replay (snapshot + log) must now match the SACM the command bus wrote to
    // disk — i.e., no divergence.
    state.command_bus.reset();
    const core::audit::ReplayVerificationResult result = core::audit::VerifyProject(project);
    EXPECT_TRUE(result.ran);
    EXPECT_TRUE(result.success) << core::audit::ToString(result.cause);
    EXPECT_EQ(result.cause, core::audit::DivergenceCause::None);

    fs::remove_all(root);
}

// --- SACM23-INT-002: the delete confirmation is library-backed --------------

// Deleting a leaf whose removal drags a relationship with it must STOP and
// disclose that, not just delete. Before this, a single-element plan skipped
// the confirmation entirely: removing a sub-goal silently took its inference
// with it, and the count-only dialog (when it did appear) never named what
// else was going.
TEST(ElementEditControllerTest, SACM23_INT_002_RemoveConfirmDisclosesLibraryConsequences) {
    const std::filesystem::path fixture =
        std::filesystem::path(AF_REPO_ROOT) / "tests" / "data" / "fixture_acp_parity.sacm.xml";
    ASSERT_TRUE(std::filesystem::exists(fixture)) << fixture.string();

    app::AppRuntimeState state;
    ASSERT_TRUE(state.app_state.load_file(fixture.string())) << state.app_state.status_message;
    ASSERT_NE(state.app_state.library_document, nullptr);

    // G2 is a leaf: the legacy plan is one element, so this used to delete
    // straight away with no dialog at all.
    ASSERT_TRUE(state.element_edit_controller->RemoveSelected(state, "G2", core::RemoveMode::NodeAndDescendants));
    ASSERT_TRUE(state.element_edit_controller->ShouldShowRemoveConfirm())
        << "a delete with consequences went through without asking";
    EXPECT_TRUE(state.element_edit_controller->PendingRemovePreviewAvailable());

    const auto& targets = state.element_edit_controller->PendingRemoveTargets();
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets.front().element_id, "G2");

    // The disclosures that matter: R1 goes too, and so does the Assurance
    // Claim Point riding on R1. The ACP is the harder of the two -- it lives in
    // vendor TaggedValues, which the seam filters out as attachments, and its
    // owner is a consequential deletion the user never selected. Without an
    // explicit line for it, ACP2 and its confidence argument package would
    // disappear with nothing on screen having mentioned them.
    const auto& consequences = state.element_edit_controller->PendingRemoveConsequences();
    const auto consequence_for = [&consequences](const std::string& id) {
        return std::find_if(
            consequences.begin(), consequences.end(), [&id](const auto& effect) { return effect.element_id == id; });
    };

    const auto inference = consequence_for("R1");
    ASSERT_NE(inference, consequences.end()) << "the inference dragged along was not disclosed";
    EXPECT_EQ(inference->kind, "AssertedInference");
    EXPECT_TRUE(inference->is_relationship);
    EXPECT_TRUE(inference->deleted);

    const auto acp = consequence_for("ACP2");
    ASSERT_NE(acp, consequences.end()) << "the Assurance Claim Point on the cascaded inference was not disclosed";
    EXPECT_EQ(acp->kind, "AssuranceClaimPoint");
    EXPECT_EQ(acp->name, "Confidence in the inference");
    EXPECT_TRUE(acp->deleted);

    // ACP1 belongs to G1, which survives; it must not be listed.
    EXPECT_EQ(consequence_for("ACP1"), consequences.end())
        << "an ACP on a surviving element was reported as going away";

    // Nothing has been removed yet -- this is a confirmation, not a report.
    EXPECT_NE(parser::FindElementById(state.app_state.loaded_case.value(), "G2"), nullptr);

    // Cancelling leaves the model and the preview state clean.
    state.element_edit_controller->CancelPendingRemoval();
    EXPECT_FALSE(state.element_edit_controller->ShouldShowRemoveConfirm());
    EXPECT_TRUE(state.element_edit_controller->PendingRemoveConsequences().empty());
    EXPECT_NE(parser::FindElementById(state.app_state.loaded_case.value(), "G2"), nullptr);
}

// An edit made with NO command bus -- a SACM file opened outside a project --
// must not lose content the legacy projection cannot represent.
//
// It used to. Without a bus the command took the pure legacy path, mutated the
// package in place, and `sync_library_document()` then re-derived the
// library-owned document FROM that projection: the wrong direction, unguarded,
// and with the reload result ignored. Anything the POD cannot hold was gone from
// the source of truth, with no refusal in the way and nothing reported. It was
// the last such path (#347); the audited path has refused this since
// SACM23-LIB-002.
//
// The document is now passed into the no-bus context, so the command applies to
// it natively and the views are re-derived from it instead.
TEST(ElementEditControllerTest, SACM23_LIB_002_NoBusEditKeepsUnrepresentableContent) {
    const std::filesystem::path fixture = std::filesystem::path(AF_REPO_ROOT) / "libs" / "sacm" / "tests" / "data" /
                                          "sacm23" / "argumentation-full-valid.sacm.xmi";
    app::AppRuntimeState state;
    ASSERT_TRUE(state.app_state.load_file(fixture.string())) << state.app_state.status_message;
    ASSERT_EQ(state.command_bus, nullptr) << "this test is about the NO-BUS path; a bus makes it measure nothing";
    ASSERT_NE(state.app_state.library_document, nullptr);

    // Non-vacuity: the case carries a kind the legacy projection cannot hold.
    const sacm_adapter::SaveOutcome before = sacm_adapter::save_document(*state.app_state.library_document);
    ASSERT_TRUE(before.ok);
    ASSERT_NE(before.xml.find("ArgumentGroup"), std::string::npos)
        << "fixture carries no unrepresentable element; this test measures nothing";

    ASSERT_TRUE(state.element_edit_controller->CommitElementTextEdit(
        state, "claim_top", "name", "en", "Top claim", "Renamed outside a project"));

    const sacm_adapter::SaveOutcome after = sacm_adapter::save_document(*state.app_state.library_document);
    ASSERT_TRUE(after.ok);
    EXPECT_NE(after.xml.find("ArgumentGroup"), std::string::npos)
        << "a no-bus edit dropped the unrepresentable element from the source of truth";
    EXPECT_NE(after.xml.find("Renamed outside a project"), std::string::npos)
        << "the no-bus edit never reached the document";
}

// The counterpart: a delete that really does reach nothing else still goes
// through without a dialog. A confirmation on every delete is a confirmation
// on none -- users learn to click past it, and then it protects nothing when
// it matters.
TEST(ElementEditControllerTest, SACM23_INT_002_RemoveWithoutConsequencesStillDeletesImmediately) {
    const std::filesystem::path fixture =
        std::filesystem::path(AF_REPO_ROOT) / "tests" / "data" / "fixture_acp_parity.sacm.xml";
    app::AppRuntimeState state;
    ASSERT_TRUE(state.app_state.load_file(fixture.string())) << state.app_state.status_message;

    // Clear the way: R1 carries an ACP, so removing it legitimately prompts.
    // Confirm through, leaving G2 an unreferenced, ACP-free leaf. The dispatch
    // path re-derives the library document, so the second preview sees the
    // post-removal state rather than a stale one.
    ASSERT_TRUE(state.element_edit_controller->RemoveSelected(state, "R1", core::RemoveMode::NodeAndDescendants));
    ASSERT_TRUE(state.element_edit_controller->ConfirmPendingRemoval(state));
    ASSERT_EQ(parser::FindElementById(state.app_state.loaded_case.value(), "R1"), nullptr);

    ASSERT_TRUE(state.element_edit_controller->RemoveSelected(state, "G2", core::RemoveMode::NodeAndDescendants));
    EXPECT_FALSE(state.element_edit_controller->ShouldShowRemoveConfirm())
        << "a delete with no consequences should not interrupt the user";
    EXPECT_EQ(parser::FindElementById(state.app_state.loaded_case.value(), "G2"), nullptr);
}

// `RemoveMode::NodeOnly` REPARENTS the removed node's children onto its parent
// -- `core::ReparentChildrenToParent` retargets a child's inference rather than
// deleting it -- and the library has no retarget operation, so the delete stays
// on the legacy mutator. Modelling it as a set of deletes would announce that
// the child's inference is being removed when it in fact survives, retargeted.
// A confident wrong disclosure is worse than none, so no preview is offered.
TEST(ElementEditControllerTest, SACM23_INT_002_NodeOnlyOffersNoPreviewRatherThanAWrongOne) {
    namespace fs = std::filesystem;
    constexpr const char* kChain = R"(<?xml version="1.0" encoding="UTF-8"?>
<AssuranceCasePackage xmlns="http://www.omg.org/spec/SACM/20220301" id="ACP1" name="Chain">
  <argumentPackage id="AP1" name="Argument">
    <claim id="G1" name="Top"/>
    <claim id="G2" name="Middle"/>
    <claim id="G3" name="Leaf"/>
    <assertedInference id="R1" source="G2" target="G1"/>
    <assertedInference id="R2" source="G3" target="G2"/>
  </argumentPackage>
</AssuranceCasePackage>)";

    const fs::path path = fs::temp_directory_path() / "af_int002_nodeonly.sacm.xml";
    { std::ofstream(path) << kChain; }

    app::AppRuntimeState state;
    ASSERT_TRUE(state.app_state.load_file(path.string())) << state.app_state.status_message;

    // NodeOnly on the interior goal: R2 must NOT be advertised as removed.
    ASSERT_TRUE(state.element_edit_controller->RemoveSelected(state, "G2", core::RemoveMode::NodeOnly));
    EXPECT_FALSE(state.element_edit_controller->PendingRemovePreviewAvailable())
        << "a preview was offered for a removal the library cannot model; it would have claimed "
           "the promoted child's inference is deleted when it survives, retargeted";
    for (const auto& effect : state.element_edit_controller->PendingRemoveConsequences()) {
        EXPECT_NE(effect.element_id, "R2") << "the preview claimed the retargeted inference is removed";
    }

    // With no preview and a single-element plan there is nothing to confirm, so
    // the removal already happened — the same behaviour as before this feature,
    // which is the point: NodeOnly gains no wrong dialog.
    EXPECT_FALSE(state.element_edit_controller->ShouldShowRemoveConfirm());
    EXPECT_EQ(parser::FindElementById(state.app_state.loaded_case.value(), "G2"), nullptr);

    // And here is why previewing it as a set of deletes would have lied: R2 and
    // G3 both survive. A delete-modelled preview would have listed R2 as
    // removed, because in the library its only target (G2) is doomed.
    EXPECT_NE(parser::FindElementById(state.app_state.loaded_case.value(), "R2"), nullptr)
        << "fixture assumption broken: NodeOnly no longer keeps the child inference";
    EXPECT_NE(parser::FindElementById(state.app_state.loaded_case.value(), "G3"), nullptr)
        << "the promoted child was orphaned";

    fs::remove(path);
}

// The assertion that keeps preview and apply honest: build the preview, confirm
// it through the SAME dispatch path the UI uses, and require the resulting
// model to match what was promised -- exactly, in both directions. A preview
// that under-reports lets a user destroy something unannounced; one that
// over-reports trains them to ignore it.
TEST(ElementEditControllerTest, SACM23_INT_002_ConfirmedRemovalMatchesThePreviewExactly) {
    const std::filesystem::path fixture =
        std::filesystem::path(AF_REPO_ROOT) / "tests" / "data" / "fixture_acp_parity.sacm.xml";
    app::AppRuntimeState state;
    ASSERT_TRUE(state.app_state.load_file(fixture.string())) << state.app_state.status_message;

    ASSERT_TRUE(state.element_edit_controller->RemoveSelected(state, "G2", core::RemoveMode::NodeAndDescendants));
    ASSERT_TRUE(state.element_edit_controller->ShouldShowRemoveConfirm());
    ASSERT_TRUE(state.element_edit_controller->PendingRemovePreviewAvailable());

    // Snapshot the promise and the pre-state before confirming.
    std::vector<std::string> promised_gone;
    std::vector<std::string> promised_kept;
    for (const auto* bucket : {&state.element_edit_controller->PendingRemoveTargets(),
                               &state.element_edit_controller->PendingRemoveConsequences()}) {
        for (const auto& effect : *bucket) {
            // ACP ids are vendor records, not model elements; they are checked
            // separately below.
            if (effect.kind == "AssuranceClaimPoint")
                continue;
            (effect.deleted ? promised_gone : promised_kept).push_back(effect.element_id);
        }
    }
    ASSERT_FALSE(promised_gone.empty());
    std::vector<std::string> before;
    for (const auto& element : state.app_state.loaded_case->elements)
        before.push_back(element.id);

    ASSERT_TRUE(state.element_edit_controller->ConfirmPendingRemoval(state));

    std::vector<std::string> actually_gone;
    for (const std::string& id : before) {
        if (parser::FindElementById(state.app_state.loaded_case.value(), id) == nullptr)
            actually_gone.push_back(id);
    }
    std::sort(promised_gone.begin(), promised_gone.end());
    std::sort(actually_gone.begin(), actually_gone.end());
    EXPECT_EQ(promised_gone, actually_gone) << "the confirmed removal did not match what the dialog promised";

    // Anything the preview said would merely be modified must still exist.
    for (const std::string& id : promised_kept) {
        EXPECT_NE(parser::FindElementById(state.app_state.loaded_case.value(), id), nullptr)
            << id << " was promised to survive, scrubbed, but is gone";
    }

    // The ACP the dialog named is really gone. Asserted against the library
    // document rather than `loaded_case.acps`: the ACP lives in TaggedValues on
    // R1, so deleting R1 destroys it in the document, while the in-memory
    // projection's `acps` vector is a cache that this path does not rebuild.
    // The document is what the user's file will contain.
    ASSERT_NE(state.app_state.library_document, nullptr);
    const core::AssuranceCase reprojected = sacm_adapter::project_case(*state.app_state.library_document);
    EXPECT_EQ(std::find_if(
                  reprojected.acps.begin(), reprojected.acps.end(), [](const auto& acp) { return acp.id == "ACP2"; }),
              reprojected.acps.end())
        << "ACP2 was announced as removed but survives in the library document";
    EXPECT_NE(std::find_if(
                  reprojected.acps.begin(), reprojected.acps.end(), [](const auto& acp) { return acp.id == "ACP1"; }),
              reprojected.acps.end())
        << "ACP1 belongs to a surviving element and must not have been destroyed";
}

// The undeveloped decorator, from the controller the inspector calls down to the
// bytes on disk and back.
//
// This is the coverage whose absence let a user-visible bug survive: the model
// layer was tested, the command was tested, and the inspector checkbox called
// NEITHER -- it assigned the projection's own `undeveloped` field and synced it
// to the legacy package, which reaches neither the library document (what gets
// saved) nor the audit log. The decorator showed on the canvas until the next
// load and was then gone, which is exactly how it was reported.
//
// So this asserts the round trip rather than the in-memory flag: mark, save,
// reload from disk, and expect it back.
TEST(ElementEditControllerTest, SetElementUndevelopedSurvivesSaveAndReload) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("af_undeveloped_roundtrip_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::filesystem::path sacm_absolute = root / "argument.sacm";
    {
        std::ofstream out(sacm_absolute, std::ios::binary);
        out << R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    }

    app::AppRuntimeState state;
    ASSERT_TRUE(state.app_state.load_file(sacm_absolute.string())) << state.app_state.status_message;
    ASSERT_NE(state.app_state.library_document, nullptr);

    core::AssuranceProject project;
    project.id = "p";
    project.name = "Project";
    project.rootPath = root;
    core::ProjectFileEntry entry;
    entry.id = "f1";
    entry.relativePath = "argument.sacm";
    entry.role = core::ProjectFileRole::SacmArgument;
    project.files.push_back(entry);
    core::audit::EnsureAuditStoreResult ensure;
    std::string error;
    ASSERT_TRUE(core::audit::EnsureAuditStore(project, entry.relativePath, ensure, error)) << error;
    state.app_state.current_project = project;
    state.command_bus = core::commands::CommandBus::Open(project, sacm_absolute, error);
    ASSERT_NE(state.command_bus, nullptr) << error;

    ASSERT_TRUE(state.element_edit_controller->SetElementUndeveloped(state, "G1", true));

    // Reload from the file the bus autosaved. An edit that only reached the
    // projection would be absent here, and present in `state` -- which is what
    // made the bug look like it had worked.
    app::AppRuntimeState reopened;
    ASSERT_TRUE(reopened.app_state.load_file(sacm_absolute.string())) << reopened.app_state.status_message;
    const parser::SacmElement* reloaded = parser::FindElementById(reopened.app_state.loaded_case.value(), "G1");
    ASSERT_NE(reloaded, nullptr);
    EXPECT_TRUE(reloaded->undeveloped) << "the undeveloped decorator did not survive save and reload";

    // ...and clearing it survives too, so the round trip is not one-way.
    ASSERT_TRUE(state.element_edit_controller->SetElementUndeveloped(state, "G1", false));
    app::AppRuntimeState cleared;
    ASSERT_TRUE(cleared.app_state.load_file(sacm_absolute.string())) << cleared.app_state.status_message;
    const parser::SacmElement* after_clear = parser::FindElementById(cleared.app_state.loaded_case.value(), "G1");
    ASSERT_NE(after_clear, nullptr);
    EXPECT_FALSE(after_clear->undeveloped);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
