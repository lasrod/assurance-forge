#include "app/areas/draft_changes_area.h"

#include "app/app_runtime_state.h"
#include "core/drafts/draft_workspace_store.h"
#include "core/reviews/review_proposal.h"
#include "ui/ui_state.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
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

struct TempDir {
    std::filesystem::path path;
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::filesystem::path UniqueTempPath(const std::string& stem) {
    static int counter = 0;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("af_draftpanel_" + stem + "_" + std::to_string(++counter));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

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
    TempDir dir{UniqueTempPath("state")};
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
