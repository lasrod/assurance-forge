#include "app/areas/draft_changes_area.h"

#include "app/app_runtime_state.h"
#include "core/drafts/draft_document_store.h"
#include "core/drafts/draft_operation_apply.h"
#include "core/drafts/draft_provenance.h"
#include "core/drafts/draft_workspace_store.h"
#include "core/project_file_io.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"
#include "core/reviews/review_proposal.h"
#include "parser/model_utils.h"
#include "support/temp_files.h"
#include "ui/ui_state.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// The Draft Changes panel is the surface that replaces the split proposal /
// change-set views, and the only place a reviewer sees every unaccepted change
// to an argument together, whatever wrote it.
//
// What it says has to be true of the working model beside it. A row that offers
// Accept on something that cannot be accepted, or that attributes an AI's change
// to nobody, is worse than no row: the user is being asked to approve a change
// to a safety argument on the strength of what this panel told them.

namespace {

using test_support::TempDir;

core::SacmElement Claim(const std::string& id, const std::string& text) {
    core::SacmElement element;
    element.id = id;
    element.type = "claim";
    element.name = id;
    element.content = text;
    return element;
}

core::reviews::PatchOperation CreateClaimOp(const std::string& create_ref, const std::string& text) {
    core::reviews::PatchOperation operation;
    operation.type = core::reviews::PatchOperationType::CreateClaim;
    operation.create_ref = create_ref;
    operation.text = text;
    return operation;
}

core::reviews::PatchOperation SupportOp(const std::string& child_ref, const std::string& parent_id) {
    core::reviews::PatchOperation operation;
    operation.type = core::reviews::PatchOperationType::AddSupportedBy;
    core::reviews::ElementRef source;
    source.create_ref = child_ref;
    core::reviews::ElementRef target;
    target.existing_id = parent_id;
    operation.source = source;
    operation.target = target;
    return operation;
}

core::reviews::PatchOperation UpdateTextOp(const std::string& element_id, const std::string& text) {
    core::reviews::PatchOperation operation;
    operation.type = core::reviews::PatchOperationType::UpdateElementText;
    core::reviews::ElementRef element;
    element.existing_id = element_id;
    operation.element = element;
    operation.field = "content";
    operation.new_value = text;
    return operation;
}

// An `AppRuntimeState` with a project, an accepted argument and a draft store
// open on it -- everything `BuildDraftChangesPanelModel` reads.
struct Fixture {
    TempDir dir{test_support::UniqueTempDirectory("state")};
    app::AppRuntimeState state;
    std::filesystem::path argument_file;

    Fixture() {
        parser::AssuranceCase accepted;
        accepted.id = "case-1";
        accepted.name = "Baseline";
        accepted.elements.push_back(Claim("G1", "The braking subsystem meets its stated performance targets."));
        state.app_state.loaded_case = accepted;

        argument_file = dir.path / "arguments" / "main.sacm";
        std::filesystem::create_directories(argument_file.parent_path());
        state.draft_workspace.SetProjectRoot(dir.path);
        std::string error;
        EXPECT_TRUE(state.draft_workspace.Open(argument_file, accepted, error)) << error;
    }

    std::string BeginGroup(const core::drafts::DraftGroupRequest& request) {
        std::string error;
        const std::string id = state.draft_workspace.BeginGroup(request, state.app_state.loaded_case.value(), error);
        EXPECT_FALSE(id.empty()) << error;
        return id;
    }

    void Stage(const std::string& group_id, const std::vector<core::reviews::PatchOperation>& operations) {
        std::string error;
        EXPECT_TRUE(
            state.draft_workspace.StageOperations(group_id, operations, state.app_state.loaded_case.value(), error))
            << error;
    }

    // Only Ready work is promotable (ADR 0010); tests asserting a row is
    // promotable submit it first, as an agent would.
    void Submit(const std::string& group_id) {
        std::string error;
        EXPECT_TRUE(state.draft_workspace.MarkGroupReady(group_id, error)) << error;
    }

    // What the frame does before any panel renders: publish the materialization
    // the whole frame reads. The panel model must not recompute it.
    void Materialize() {
        state.draft_frame_materialization =
            state.draft_workspace.MaterializeSnapshot(state.app_state.loaded_case.value(), 1);
        ASSERT_TRUE(state.draft_frame_materialization->success) << state.draft_frame_materialization->error;
    }

    std::string IdentityFor(const std::string& group_id, const std::string& create_ref) {
        const core::drafts::DraftChangeGroup* group = state.draft_workspace.workspace()->FindGroup(group_id);
        if (group == nullptr)
            return {};
        const auto found = group->generated_ids.find(create_ref);
        return found == group->generated_ids.end() ? std::string{} : found->second;
    }
};

core::drafts::DraftGroupRequest McpRequest(const std::string& title, const std::string& label = "Claude Code") {
    core::drafts::DraftGroupRequest request;
    request.title = title;
    request.source = core::drafts::DraftSource::Mcp;
    request.source_label = label;
    return request;
}

const ui::panels::DraftChangeRow* FindRow(const ui::panels::DraftChangesPanelModel& model,
                                          const std::string& group_id) {
    for (const ui::panels::DraftChangeRow& row : model.rows) {
        if (row.group_id == group_id)
            return &row;
    }
    return nullptr;
}

} // namespace

TEST(DraftChangesPanel, NoDraftReadsAsNoDraftRatherThanAnEmptyList) {
    Fixture fixture;
    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(fixture.state);
    EXPECT_FALSE(model.has_workspace);
    EXPECT_TRUE(model.rows.empty());
}

TEST(DraftChangesPanel, ARowNamesWhoWroteItAndWhy) {
    Fixture fixture;
    core::drafts::DraftGroupRequest request = McpRequest("Add a misuse-hazard branch");
    request.rationale = "SCCG AR-02: the hazard analysis identifies foreseeable misuse and the argument does not "
                        "address it.";
    request.source_session_id = "session-7";
    request.guideline_ids = {"AR-02"};
    request.review_item_ids = {"RI-11"};
    const std::string group = fixture.BeginGroup(request);
    fixture.Stage(group, {CreateClaimOp("$sub", "Foreseeable misuse is mitigated."), SupportOp("$sub", "G1")});
    fixture.Materialize();

    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(fixture.state);
    ASSERT_TRUE(model.has_workspace);
    const ui::panels::DraftChangeRow* row = FindRow(model, group);
    ASSERT_NE(row, nullptr);

    // Attribution is the first thing a reviewer needs and the thing the surface
    // this replaces could not show: an AI-authored change to a safety argument
    // must never read as a human's.
    EXPECT_EQ(row->source_label, "Claude Code");
    EXPECT_EQ(row->source_session_id, "session-7");
    EXPECT_FALSE(row->source_kind_label.empty());
    EXPECT_EQ(row->rationale, request.rationale);
    ASSERT_EQ(row->guideline_ids.size(), 1u);
    EXPECT_EQ(row->guideline_ids.front(), "AR-02");
    ASSERT_EQ(row->review_item_ids.size(), 1u);
    EXPECT_EQ(row->review_item_ids.front(), "RI-11");
}

TEST(DraftChangesPanel, AGlossaryGroupShowsItsTermsAndDefinitionsInFull) {
    Fixture fixture;
    const std::string terms = fixture.BeginGroup(McpRequest("Bound the safety vocabulary"));
    core::reviews::PatchOperation hazard;
    hazard.type = core::reviews::PatchOperationType::CreateTerm;
    hazard.create_ref = "$hazard";
    hazard.text = "hazard";
    hazard.new_value = "A system state that, with environmental conditions, could lead to harm.";
    core::reviews::PatchOperation alarp;
    alarp.type = core::reviews::PatchOperationType::CreateTerm;
    alarp.create_ref = "$alarp";
    alarp.text = "ALARP";
    alarp.new_value = "Risk reduced as low as reasonably practicable.";
    fixture.Stage(terms, {hazard, alarp});

    const std::string argument = fixture.BeginGroup(McpRequest("Develop the braking claim"));
    fixture.Stage(argument, {CreateClaimOp("$sub", "Brake wear is monitored."), SupportOp("$sub", "G1")});
    fixture.Materialize();

    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(fixture.state);

    // A term is deliberately not a GSN node, so unlike an argument change there
    // is no canvas rendering beside this row to read it from. The row itself has
    // to carry the full text -- a reviewer accepting a glossary is accepting the
    // definitions, and "2 elements added" is not a reviewable statement of them.
    const ui::panels::DraftChangeRow* glossary_row = FindRow(model, terms);
    ASSERT_NE(glossary_row, nullptr);
    ASSERT_EQ(glossary_row->glossary_lines.size(), 2u);
    const auto has_line = [&](const std::string& line) {
        return std::find(glossary_row->glossary_lines.begin(), glossary_row->glossary_lines.end(), line) !=
               glossary_row->glossary_lines.end();
    };
    EXPECT_TRUE(has_line("hazard: A system state that, with environmental conditions, could lead to harm."));
    EXPECT_TRUE(has_line("ALARP: Risk reduced as low as reasonably practicable."));

    // A group that only classifies and cites terms must show what it changed.
    // Listing the terms with their definitions and nothing else would read as a
    // definition change, which is not what the reviewer is being asked to accept.
    const std::string classify = fixture.BeginGroup(McpRequest("Classify the glossary"));
    core::reviews::PatchOperation create_category;
    create_category.type = core::reviews::PatchOperationType::CreateCategory;
    create_category.create_ref = "$regulatory";
    create_category.text = "Regulatory terms";
    fixture.Stage(classify, {create_category});
    // Materialized before the id is read: staging only rehearses the
    // allocation, so the identity is not pinned until the draft is really
    // materialized. An agent reads the same id out of its staging result.
    fixture.Materialize();
    const std::string category_id = fixture.IdentityFor(classify, "$regulatory");
    ASSERT_FALSE(category_id.empty());

    core::reviews::PatchOperation set_category;
    set_category.type = core::reviews::PatchOperationType::UpdateTerm;
    core::reviews::ElementRef alarp_ref;
    alarp_ref.existing_id = fixture.IdentityFor(terms, "$alarp");
    set_category.element = alarp_ref;
    set_category.field = "category";
    set_category.new_value = category_id;
    core::reviews::PatchOperation set_source = set_category;
    set_source.field = "external_reference";
    set_source.new_value = "HSE R2P2, 2001";
    fixture.Stage(classify, {set_category, set_source});
    fixture.Materialize();

    const ui::panels::DraftChangesPanelModel classified = app::areas::BuildDraftChangesPanelModel(fixture.state);
    const ui::panels::DraftChangeRow* classify_row = FindRow(classified, classify);
    ASSERT_NE(classify_row, nullptr);
    ASSERT_FALSE(classify_row->glossary_lines.empty());
    const auto mentions = [&](const std::string& needle) {
        for (const std::string& line : classify_row->glossary_lines) {
            if (line.find(needle) != std::string::npos)
                return true;
        }
        return false;
    };
    // The category by name, not by the id the operation carried.
    EXPECT_TRUE(mentions("[Regulatory terms]"));
    EXPECT_TRUE(mentions("(HSE R2P2, 2001)"));

    // An argument-only group carries no glossary section at all.
    const ui::panels::DraftChangeRow* argument_row = FindRow(model, argument);
    ASSERT_NE(argument_row, nullptr);
    EXPECT_TRUE(argument_row->glossary_lines.empty());
}

// Clicking a row takes the user to the change. Which view that is depends on
// what the group touched, because the GSN canvas deliberately does not draw
// terms -- sending a glossary group there lands on a diagram where nothing is
// highlighted, which reads as a click that did nothing.
TEST(DraftChangesPanel, ARowGoesToTheViewThatCanActuallyShowTheChange) {
    Fixture fixture;

    // An argument group still goes to the canvas, where its claim is drawn.
    const std::string argument = fixture.BeginGroup(McpRequest("Develop the braking claim"));
    fixture.Stage(argument, {CreateClaimOp("$sub", "Brake wear is monitored."), SupportOp("$sub", "G1")});
    fixture.Materialize();

    const app::areas::DraftGroupFocus argument_focus = app::areas::ResolveDraftGroupFocus(fixture.state, argument);
    EXPECT_EQ(argument_focus.kind, app::areas::DraftGroupFocusKind::Canvas);
    EXPECT_EQ(argument_focus.element_id, fixture.IdentityFor(argument, "$sub"));

    // A group defining a term does not: the term is not in the accepted
    // glossary the terminology view reads, so going there would report it
    // missing. The row's own glossary lines are where it is readable.
    const std::string terms = fixture.BeginGroup(McpRequest("Define hazard"));
    core::reviews::PatchOperation hazard;
    hazard.type = core::reviews::PatchOperationType::CreateTerm;
    hazard.create_ref = "$hazard";
    hazard.text = "hazard";
    hazard.new_value = "A system state that could lead to harm.";
    fixture.Stage(terms, {hazard});
    fixture.Materialize();

    const app::areas::DraftGroupFocus staged_focus = app::areas::ResolveDraftGroupFocus(fixture.state, terms);
    EXPECT_EQ(staged_focus.kind, app::areas::DraftGroupFocusKind::None)
        << "a term this draft created is not in the accepted glossary yet";
}

// Once a term IS part of the accepted glossary, a group that edits it goes to
// the terminology view, where its definition, category and source are readable.
TEST(DraftChangesPanel, AGroupEditingAnAcceptedTermGoesToTheTerminologyView) {
    Fixture fixture;

    // An accepted glossary, as the terminology view reads it.
    sacm::AssuranceCasePackage package;
    package.id = "AC1";
    sacm::TerminologyPackage terminology;
    terminology.id = "TP1";
    terminology.gid = "gid-TP1";
    sacm::Term term;
    term.id = "T1";
    term.gid = "gid-T1";
    term.value = "ALARP";
    term.description = "As low as reasonably practicable.";
    terminology.terms.push_back(term);
    package.terminologyPackages.push_back(terminology);
    fixture.state.app_state.sacm_package = package;

    // The same term in the accepted flat model the draft is written against.
    core::SacmElement projected;
    projected.id = "T1";
    projected.gid = "gid-T1";
    projected.type = "term";
    projected.content = "ALARP";
    projected.description = "As low as reasonably practicable.";
    fixture.state.app_state.loaded_case->elements.push_back(projected);

    const std::string classify = fixture.BeginGroup(McpRequest("Cite the ALARP definition"));
    core::reviews::PatchOperation cite;
    cite.type = core::reviews::PatchOperationType::UpdateTerm;
    core::reviews::ElementRef target;
    target.existing_id = "T1";
    cite.element = target;
    cite.field = "external_reference";
    cite.new_value = "HSE R2P2, 2001";
    fixture.Stage(classify, {cite});
    fixture.Materialize();

    const app::areas::DraftGroupFocus focus = app::areas::ResolveDraftGroupFocus(fixture.state, classify);
    EXPECT_EQ(focus.kind, app::areas::DraftGroupFocusKind::Terminology);
    EXPECT_EQ(focus.term_ref.id, "T1");
    EXPECT_EQ(focus.package_ref.id, "TP1");
}

TEST(DraftChangesPanel, RelationshipChangesAreCountedSeparatelyFromElements) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup(McpRequest("Add a sub-claim"));
    fixture.Stage(group, {CreateClaimOp("$sub", "Foreseeable misuse is mitigated."), SupportOp("$sub", "G1")});
    fixture.Materialize();

    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(fixture.state);
    const ui::panels::DraftChangeRow* row = FindRow(model, group);
    ASSERT_NE(row, nullptr);

    // A changed support relationship can alter the meaning of an argument more
    // than a reworded claim can. Folding both into one "2 changes" is how a
    // reviewer approves a restructure believing they approved an addition.
    EXPECT_EQ(row->elements_added, 1);
    EXPECT_EQ(row->relationships_added, 1);
    EXPECT_EQ(row->elements_modified, 0);
    EXPECT_EQ(row->elements_removed, 0);
}

TEST(DraftChangesPanel, ARowNamesWhatAcceptingItWouldAlsoAccept) {
    Fixture fixture;
    const std::string creator = fixture.BeginGroup(McpRequest("Add a sub-claim"));
    fixture.Stage(creator, {CreateClaimOp("$sub", "Foreseeable misuse is mitigated."), SupportOp("$sub", "G1")});
    fixture.Materialize();
    const std::string sub_id = fixture.IdentityFor(creator, "$sub");
    ASSERT_FALSE(sub_id.empty());

    const std::string editor = fixture.BeginGroup(McpRequest("Reword the sub-claim", "SCCG AI Review"));
    fixture.Stage(editor, {UpdateTextOp(sub_id, "Foreseeable misuse is mitigated to ALARP.")});
    fixture.Submit(creator);
    fixture.Submit(editor);
    fixture.Materialize();

    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(fixture.state);
    const ui::panels::DraftChangeRow* row = FindRow(model, editor);
    ASSERT_NE(row, nullptr);

    // The rewording cannot be accepted without the creation it edits. Saying so
    // before the button is pressed is the whole point -- widening a selection
    // silently is the failure the closure exists to prevent.
    EXPECT_TRUE(row->promotable);
    ASSERT_EQ(row->also_accepts_titles.size(), 1u);
    EXPECT_EQ(row->also_accepts_titles.front(), "Add a sub-claim");

    const ui::panels::DraftChangeRow* independent = FindRow(model, creator);
    ASSERT_NE(independent, nullptr);
    EXPECT_TRUE(independent->also_accepts_titles.empty()) << "the creation depends on nothing";
}

TEST(DraftChangesPanel, AStrandedRowOffersNoAcceptAndSaysWhy) {
    Fixture fixture;
    const std::string creator = fixture.BeginGroup(McpRequest("Add a sub-claim"));
    fixture.Stage(creator, {CreateClaimOp("$sub", "Foreseeable misuse is mitigated."), SupportOp("$sub", "G1")});
    fixture.Materialize();
    const std::string sub_id = fixture.IdentityFor(creator, "$sub");

    const std::string editor = fixture.BeginGroup(McpRequest("Reword the sub-claim", "SCCG AI Review"));
    fixture.Stage(editor, {UpdateTextOp(sub_id, "Foreseeable misuse is mitigated to ALARP.")});

    std::string error;
    ASSERT_TRUE(fixture.state.draft_workspace.RejectGroup(creator, error)) << error;
    ASSERT_TRUE(fixture.state.draft_workspace.MarkGroupNeedsAttention(editor, error)) << error;
    fixture.Materialize();

    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(fixture.state);

    // The rejected group is gone from the list; the stranded one is still there,
    // because it is work the user deliberately chose to keep.
    EXPECT_EQ(FindRow(model, creator), nullptr);
    const ui::panels::DraftChangeRow* row = FindRow(model, editor);
    ASSERT_NE(row, nullptr);

    // Offering Accept here would offer to apply an update to an element that
    // will never exist. The refusal is on the row, not in the status bar --
    // "Accept does nothing" was the reported defect in the surface this replaces.
    EXPECT_TRUE(row->needs_attention);
    EXPECT_FALSE(row->promotable);
    EXPECT_FALSE(row->blocked_reason.empty());
}

TEST(DraftChangesPanel, RowsAreListedInMaterializationOrder) {
    Fixture fixture;
    const std::string first = fixture.BeginGroup(McpRequest("Add a sub-claim"));
    fixture.Stage(first, {CreateClaimOp("$sub", "Foreseeable misuse is mitigated."), SupportOp("$sub", "G1")});
    const std::string second = fixture.BeginGroup(McpRequest("Clarify the top goal", "SCCG AI Review"));
    fixture.Stage(second, {UpdateTextOp("G1", "Clarified.")});
    fixture.Materialize();

    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(fixture.state);
    ASSERT_EQ(model.rows.size(), 2u);
    // Sequence order, which is the order they were applied in. Any other order
    // can put a group above the one it depends on, so the list would read as
    // though the later change came first.
    EXPECT_EQ(model.rows.front().group_id, first);
    EXPECT_EQ(model.rows.back().group_id, second);
}

TEST(DraftChangesPanel, AWholeDraftHeldBackIsExplainedOnceRatherThanPerRow) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup(McpRequest("Add a sub-claim"));
    fixture.Stage(group, {CreateClaimOp("$sub", "Foreseeable misuse is mitigated."), SupportOp("$sub", "G1")});
    fixture.Materialize();

    // The accepted argument moved underneath the draft.
    parser::AssuranceCase moved = fixture.state.app_state.loaded_case.value();
    moved.elements.push_back(Claim("G2", "Something else was accepted in the meantime."));
    std::string error;
    ASSERT_TRUE(fixture.state.draft_workspace.Open(fixture.argument_file, moved, error)) << error;
    fixture.state.app_state.loaded_case = moved;
    ASSERT_EQ(fixture.state.draft_workspace.workspace()->state, core::drafts::DraftWorkspaceState::NeedsRebase);
    fixture.state.draft_frame_materialization = fixture.state.draft_workspace.MaterializeSnapshot(moved, 2);

    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(fixture.state);

    // In this state no single group is the explanation, and a per-row reason
    // would send the user hunting for a problem in the wrong place.
    EXPECT_FALSE(model.workspace_blocked_reason.empty());
    for (const ui::panels::DraftChangeRow& row : model.rows)
        EXPECT_FALSE(row.promotable) << "nothing is acceptable while the draft is stale";
}

// A refusal outlives the frame it happened in.
//
// The reported defect: Accept All was pressed, the accept refused, and the user
// was left looking at a draft that still said "1 unaccepted change" with nothing
// on screen saying why. The reason existed -- it went to the status bar, which
// is one line, transient, and truncated the sentence before the reason.
TEST(DraftChangesPanel, ARefusedAcceptIsStillExplainedAfterTheStatusLineIsGone) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup(McpRequest("Restate the top goal in English and Japanese"));
    fixture.Stage(group, {UpdateTextOp("G1", "Clarified.")});
    fixture.Materialize();

    ui::UiState& ui_state = ui::GetUiState();
    ui_state.draft_accept_error = "the library could not express this proposal";
    ui_state.draft_accept_error_revision = fixture.state.draft_workspace.workspace()->working_revision;

    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(fixture.state);
    EXPECT_EQ(model.accept_error, "the library could not express this proposal");

    // And it expires with the draft it was about. An agent staging more work
    // moves the revision, and a stale refusal would describe work that may no
    // longer be there -- worse than saying nothing.
    fixture.Stage(group, {UpdateTextOp("G1", "Clarified again.")});
    const ui::panels::DraftChangesPanelModel after = app::areas::BuildDraftChangesPanelModel(fixture.state);
    EXPECT_TRUE(after.accept_error.empty()) << "a refusal about an older draft must not be shown against this one";

    ui_state.draft_accept_error.clear();
    ui_state.draft_accept_error_revision = 0;
}

// --- Element rows: a document-backed draft (ADR 0016) -----------------------
//
// In a draft that is a SACM document the reviewable unit is the element, and a
// hand edit has no change group at all. These rows are what has to keep working
// when the workspace store is deleted, so they are built from the comparison and
// the elements' own provenance and consult no group.

namespace {

// A second fixture, because the group fixture's accepted case is an in-memory
// POD and the document path needs a real SACM file the library can load.
struct DocumentFixture {
    TempDir dir{test_support::UniqueTempDirectory("document")};
    app::AppRuntimeState state;
    std::filesystem::path argument_file;
    std::unique_ptr<sacm_adapter::LibraryDocument> accepted;

    bool Ready() const {
        return accepted != nullptr;
    }

    DocumentFixture() {
        argument_file = dir.path / "arguments" / "main.sacm";
        std::filesystem::create_directories(argument_file.parent_path());
        const sacm_adapter::SaveOutcome seed = sacm_adapter::new_case_document_xmi("Blender");
        if (!seed.ok || !core::WriteTextFileAtomic(argument_file, seed.xml).has_value())
            return;
        sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(argument_file);
        if (!loaded.ok)
            return;
        accepted = std::move(loaded.document);
        state.app_state.loaded_case = sacm_adapter::project_case(*accepted);
        std::string error;
        if (!state.draft_document.Open(dir.path, argument_file, *accepted, error))
            accepted = nullptr;
        else if (!state.draft_document.EnsureDraft(*accepted, error))
            accepted = nullptr;
    }

    std::string TopClaimId() const {
        for (const core::SacmElement& element : state.app_state.loaded_case.value().elements) {
            if (element.type == "claim")
                return element.id;
        }
        return {};
    }

    core::drafts::DraftOperationResult Apply(const std::vector<core::reviews::PatchOperation>& operations,
                                             const std::string& anchor,
                                             const core::drafts::DraftProvenance& provenance) {
        core::drafts::DraftOperationResult result = core::drafts::ApplyOperationsToDraftDocument(
            *state.draft_document.document(), operations, anchor, &provenance);
        if (result.applied)
            state.draft_document.MarkChanged();
        return result;
    }
};

core::drafts::DraftProvenance
By(core::drafts::DraftSource source, const std::string& label, const std::string& why = {}) {
    core::drafts::DraftProvenance provenance;
    provenance.contribution_id = label == "Jesper" ? "human-jesper" : "group-1";
    provenance.source = source;
    provenance.label = label;
    provenance.rationale = why;
    return provenance;
}

core::drafts::DraftProvenance FromAgent(const std::string& why) {
    return By(core::drafts::DraftSource::Mcp, "Claude Desktop", why);
}

core::drafts::DraftProvenance FromReviewer(const std::string& why) {
    return By(core::drafts::DraftSource::Human, "Jesper", why);
}

const ui::panels::DraftElementRow* RowFor(const ui::panels::DraftChangesPanelModel& model, const std::string& id) {
    for (const ui::panels::DraftElementRow& row : model.element_rows) {
        if (row.element_id == id)
            return &row;
    }
    return nullptr;
}

core::reviews::PatchOperation RemoveSubtreeOp(const std::string& element_id) {
    core::reviews::PatchOperation operation;
    operation.type = core::reviews::PatchOperationType::RemoveElement;
    core::reviews::ElementRef target;
    target.existing_id = element_id;
    operation.element = target;
    operation.field = "subtree";
    return operation;
}

} // namespace

TEST(DraftChangesPanelElementRows, ADocumentBackedDraftListsItsChangedElements) {
    DocumentFixture f;
    ASSERT_TRUE(f.Ready());
    const std::string top = f.TopClaimId();
    ASSERT_FALSE(top.empty());

    const core::drafts::DraftOperationResult created =
        f.Apply({CreateClaimOp("$sub", "Blade hazards are controlled."), SupportOp("$sub", top)},
                top,
                FromAgent("hazard branch"));
    ASSERT_TRUE(created.applied) << created.error;

    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(f.state);
    EXPECT_TRUE(model.has_workspace) << "the panel reported no draft at all";

    const ui::panels::DraftElementRow* row = RowFor(model, created.created_ids.at("$sub"));
    ASSERT_NE(row, nullptr) << "the created claim is not listed";
    EXPECT_EQ(row->label, "Blade hazards are controlled.");
    EXPECT_FALSE(row->kind_label.empty());
    EXPECT_FALSE(row->change_label.empty());
}

// The reported shape: a reviewer's own edit has no change group and no workspace
// holds it, so the tab was hidden and the panel said there was nothing to accept
// while the draft held an unaccepted change.
TEST(DraftChangesPanelElementRows, AHandEditWithNoChangeGroupIsStillShown) {
    DocumentFixture f;
    ASSERT_TRUE(f.Ready());
    const std::string top = f.TopClaimId();
    ASSERT_FALSE(top.empty());
    ASSERT_FALSE(app::areas::DraftChangesTabVisible(f.state)) << "a draft that changes nothing needs no tab";

    const core::drafts::DraftOperationResult edited =
        f.Apply({UpdateTextOp(top, "The mower is safe to operate.")}, top, FromReviewer({}));
    ASSERT_TRUE(edited.applied) << edited.error;
    ASSERT_EQ(f.state.draft_workspace.workspace(), nullptr) << "the case under test has no change group";

    EXPECT_TRUE(app::areas::DraftChangesTabVisible(f.state)) << "the changes are in a tab that does not exist";
    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(f.state);
    const ui::panels::DraftElementRow* row = RowFor(model, top);
    ASSERT_NE(row, nullptr) << "an unaccepted hand edit is not listed";
    EXPECT_EQ(row->contributions.size(), 1u);
}

// The rows are cached across frames, so what has to be pinned is that the cache
// does not outlive the draft it describes, and that selecting a row is seen
// without the draft changing at all.
TEST(DraftChangesPanelElementRows, RowsFollowTheDraftAndTheSelection) {
    DocumentFixture f;
    ASSERT_TRUE(f.Ready());
    const std::string top = f.TopClaimId();
    ASSERT_FALSE(top.empty());

    ASSERT_TRUE(f.Apply({UpdateTextOp(top, "First wording.")}, top, FromReviewer({})).applied);
    const ui::panels::DraftChangesPanelModel first = app::areas::BuildDraftChangesPanelModel(f.state);
    ASSERT_NE(RowFor(first, top), nullptr);
    EXPECT_EQ(RowFor(first, top)->label, "First wording.");

    ui::UiState& ui_state = ui::GetUiState();
    const std::string selection_before = ui_state.selected_element_id;
    ui_state.selected_element_id = top;
    const ui::panels::DraftChangesPanelModel selected = app::areas::BuildDraftChangesPanelModel(f.state);
    ui_state.selected_element_id = selection_before;
    ASSERT_NE(RowFor(selected, top), nullptr);
    EXPECT_TRUE(RowFor(selected, top)->selected) << "the selection was frozen with the cached rows";

    ASSERT_TRUE(f.Apply({UpdateTextOp(top, "Second wording.")}, top, FromReviewer({})).applied);
    const ui::panels::DraftChangesPanelModel second = app::areas::BuildDraftChangesPanelModel(f.state);
    ASSERT_NE(RowFor(second, top), nullptr);
    EXPECT_EQ(RowFor(second, top)->label, "Second wording.") << "the panel kept describing the previous draft";
}

// The element rows and the group rows would describe one draft twice, and a
// group row's Accept and Reject mean nothing on a draft that is accepted whole.
TEST(DraftChangesPanelElementRows, ADocumentDraftIsNotListedASecondTimeAsGroups) {
    DocumentFixture f;
    ASSERT_TRUE(f.Ready());
    const std::string top = f.TopClaimId();
    ASSERT_FALSE(top.empty());

    core::drafts::DraftGroupRequest request;
    request.source = core::drafts::DraftSource::Mcp;
    request.source_label = "Claude Desktop";
    request.title = "Reword the top claim";
    std::string error;
    const core::AssuranceCase& accepted = f.state.app_state.loaded_case.value();
    ASSERT_TRUE(f.state.draft_workspace.Open(f.argument_file, accepted, error)) << error;
    ASSERT_FALSE(f.state.draft_workspace.BeginGroup(request, accepted, error).empty()) << error;

    ASSERT_TRUE(f.Apply({UpdateTextOp(top, "Reworded.")}, top, FromAgent("clearer")).applied);

    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(f.state);
    EXPECT_FALSE(model.element_rows.empty());
    EXPECT_TRUE(model.rows.empty()) << "the same draft was listed twice";
}

// The row says who proposed it. A contributor who is missing from this list has
// been dropped from what the reviewer is approving.
TEST(DraftChangesPanelElementRows, ARowNamesEveryContributor) {
    DocumentFixture f;
    ASSERT_TRUE(f.Ready());
    const std::string top = f.TopClaimId();
    ASSERT_FALSE(top.empty());

    const core::drafts::DraftOperationResult created =
        f.Apply({CreateClaimOp("$sub", "Blade hazards are controlled."), SupportOp("$sub", top)},
                top,
                FromAgent("hazard branch"));
    ASSERT_TRUE(created.applied) << created.error;
    const std::string claim_id = created.created_ids.at("$sub");

    const core::drafts::DraftOperationResult reworded =
        f.Apply({UpdateTextOp(claim_id, "Blade hazards are controlled during normal operation.")},
                top,
                FromReviewer("bounded the qualifier"));
    ASSERT_TRUE(reworded.applied) << reworded.error;

    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(f.state);
    const ui::panels::DraftElementRow* row = RowFor(model, claim_id);
    ASSERT_NE(row, nullptr);
    ASSERT_EQ(row->contributions.size(), 2u) << "a contributor is missing from the row";
    const auto named = [&](const std::string& label, const std::string& rationale) {
        return std::any_of(row->contributions.begin(), row->contributions.end(), [&](const auto& contribution) {
            return contribution.source_label == label && contribution.rationale == rationale;
        });
    };
    EXPECT_TRUE(named("Claude Desktop", "hazard branch"));
    EXPECT_TRUE(named("Jesper", "bounded the qualifier"));
}

// A contributor that gave no label is still named by what kind of source it was.
TEST(DraftChangesPanelElementRows, AContributorWithNoLabelIsNamedByItsSource) {
    DocumentFixture f;
    ASSERT_TRUE(f.Ready());
    const std::string top = f.TopClaimId();
    ASSERT_FALSE(top.empty());

    core::drafts::DraftProvenance unnamed = FromAgent("clearer");
    unnamed.label.clear();
    ASSERT_TRUE(f.Apply({UpdateTextOp(top, "Reworded.")}, top, unnamed).applied);

    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(f.state);
    const ui::panels::DraftElementRow* row = RowFor(model, top);
    ASSERT_NE(row, nullptr);
    ASSERT_EQ(row->contributions.size(), 1u);
    EXPECT_FALSE(row->contributions.front().source_label.empty());
}

// A removed element is not in the draft to be listed from, so its words come
// from the accepted argument. It is not in the draft to carry a tag either, so
// nobody is named (ADR 0016); the panel shows that gap, and the test pins that
// the row is not given an author it does not have. A reviewer who cannot see
// what a draft deletes is being asked to approve a deletion they were never shown.
//
// The element removed here is one the ACCEPTED argument holds. A claim the draft
// both creates and removes is deliberately not a row: its net effect on the
// accepted argument is nothing, and there is nothing for a reviewer to decide.
TEST(DraftChangesPanelElementRows, ARemovedElementIsListedWithNoContributor) {
    DocumentFixture f;
    ASSERT_TRUE(f.Ready());
    const std::string top = f.TopClaimId();
    ASSERT_FALSE(top.empty());
    const core::SacmElement* accepted = parser::FindElementById(f.state.app_state.loaded_case.value(), top);
    ASSERT_NE(accepted, nullptr);
    const std::string accepted_words = accepted->content.empty() ? accepted->name : accepted->content;

    const core::drafts::DraftOperationResult removed =
        f.Apply({RemoveSubtreeOp(top)}, top, FromReviewer("superseded by the hazard branch"));
    ASSERT_TRUE(removed.applied) << removed.error;

    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(f.state);
    const ui::panels::DraftElementRow* row = RowFor(model, top);
    ASSERT_NE(row, nullptr) << "a deletion the reviewer must approve is not on the list";
    EXPECT_EQ(row->label, accepted_words) << "the row must say what is being deleted";
    EXPECT_TRUE(row->contributions.empty()) << "the row names a contributor no tag recorded";
}

// The counterpart, stated so the behaviour above is a decision rather than an
// accident of the comparison.
TEST(DraftChangesPanelElementRows, AChangeTheDraftUndoesItselfIsNotARow) {
    DocumentFixture f;
    ASSERT_TRUE(f.Ready());
    const std::string top = f.TopClaimId();
    ASSERT_FALSE(top.empty());

    const core::drafts::DraftOperationResult created =
        f.Apply({CreateClaimOp("$sub", "Blade hazards are controlled."), SupportOp("$sub", top)},
                top,
                FromAgent("hazard branch"));
    ASSERT_TRUE(created.applied) << created.error;
    const std::string claim_id = created.created_ids.at("$sub");

    const core::drafts::DraftOperationResult removed =
        f.Apply({RemoveSubtreeOp(claim_id)}, top, FromReviewer("duplicated an existing claim"));
    ASSERT_TRUE(removed.applied) << removed.error;

    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(f.state);
    EXPECT_EQ(RowFor(model, claim_id), nullptr) << "a claim the draft created and then removed was offered for review";
}

TEST(DraftChangesPanelElementRows, AnUntouchedDraftListsNothing) {
    DocumentFixture f;
    ASSERT_TRUE(f.Ready());
    const ui::panels::DraftChangesPanelModel model = app::areas::BuildDraftChangesPanelModel(f.state);
    EXPECT_TRUE(model.element_rows.empty()) << "a draft that changes nothing reported changes";
}
