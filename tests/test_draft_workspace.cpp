#include "core/drafts/draft_workspace_store.h"

#include "core/drafts/draft_materializer.h"
#include "core/drafts/draft_persistence.h"
#include "core/drafts/draft_promotion_service.h"
#include "core/reviews/review_proposal_patch_service.h"
#include "core/project_file_io.h"
#include "core/project_service.h"
#include "core/reviews/review_proposal.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

// The working draft is the one place unaccepted changes accumulate, whatever
// made them.
//
// The properties that matter most here are the ones the design this replaces got
// wrong, and the ones a safety-argument tool cannot afford to get wrong at all:
// the accepted file does not change until a human promotes; a proposed element
// keeps its identity across every rebuild; and a group that cannot be applied
// says so about itself rather than leaving a half-applied argument on screen.

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
        std::filesystem::temp_directory_path() / ("af_drafts_" + stem + "_" + std::to_string(++counter));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

core::SacmElement Claim(const std::string& id, const std::string& text, bool undeveloped = false) {
    core::SacmElement element;
    element.id = id;
    element.type = "claim";
    element.name = id;
    element.content = text;
    element.undeveloped = undeveloped;
    return element;
}

core::SacmElement Strategy(const std::string& id, const std::string& text) {
    core::SacmElement element;
    element.id = id;
    element.type = "argumentreasoning";
    element.name = id;
    element.content = text;
    return element;
}

// SACM's source is the premise and its target the conclusion, so support runs
// from the child up to the parent.
core::SacmElement Supports(const std::string& id, const std::string& child, const std::string& parent) {
    core::SacmElement element;
    element.id = id;
    element.type = "assertedinference";
    element.source_refs = {child};
    element.target_refs = {parent};
    return element;
}

core::AssuranceCase BaselineCase() {
    core::AssuranceCase model;
    model.id = "case-1";
    model.name = "Baseline";
    // Deliberately free of the qualifiers CL.5 names -- "safe", "all", "never"
    // and the rest. A baseline that trips a guideline check would make every
    // findings assertion below pass or fail for the wrong reason.
    model.elements.push_back(Claim("G1", "The braking subsystem meets its stated performance targets.", true));
    return model;
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

core::reviews::PatchOperation ClearUndevelopedOp(const std::string& element_id) {
    core::reviews::PatchOperation operation;
    operation.type = core::reviews::PatchOperationType::ClearUndeveloped;
    core::reviews::ElementRef element;
    element.existing_id = element_id;
    operation.element = element;
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

core::drafts::DraftGroupRequest McpRequest(const std::string& title, const std::string& label = "Claude Code") {
    core::drafts::DraftGroupRequest request;
    request.title = title;
    request.source = core::drafts::DraftSource::Mcp;
    request.source_label = label;
    return request;
}

// A store opened on a real project directory, which is what exercises the
// persistence path rather than only the in-memory one.
struct Fixture {
    TempDir dir{UniqueTempPath("store")};
    core::AssuranceCase accepted = BaselineCase();
    core::drafts::DraftWorkspaceStore store;
    std::filesystem::path argument_file;

    Fixture() {
        argument_file = dir.path / "arguments" / "main.sacm";
        std::filesystem::create_directories(argument_file.parent_path());
        store.SetProjectRoot(dir.path);
        std::string error;
        EXPECT_TRUE(store.Open(argument_file, accepted, error)) << error;
    }

    std::string BeginGroup(const std::string& title, const std::string& label = "Claude Code") {
        std::string error;
        const std::string id = store.BeginGroup(McpRequest(title, label), accepted, error);
        EXPECT_FALSE(id.empty()) << error;
        return id;
    }

    void Stage(const std::string& group_id, const std::vector<core::reviews::PatchOperation>& operations) {
        std::string error;
        EXPECT_TRUE(store.StageOperations(group_id, operations, accepted, error)) << error;
    }
};

const core::SacmElement* FindElement(const core::AssuranceCase& model, const std::string& id) {
    for (const core::SacmElement& element : model.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

// The id a group's `create_ref` resolved to, which is the handle an agent is
// given and the canvas selects by.
std::string IdentityFor(const core::drafts::DraftWorkspaceStore& store,
                        const std::string& group_id,
                        const std::string& create_ref) {
    const core::drafts::DraftWorkspace* workspace = store.workspace();
    if (workspace == nullptr)
        return {};
    const core::drafts::DraftChangeGroup* group = workspace->FindGroup(group_id);
    if (group == nullptr)
        return {};
    auto found = group->generated_ids.find(create_ref);
    return found == group->generated_ids.end() ? std::string{} : found->second;
}

} // namespace

// --------------------------------------------------------------------------
// Invariant 1: no draft operation changes the accepted argument.
// --------------------------------------------------------------------------

TEST(DraftWorkspace, StagingLeavesTheAcceptedModelUntouched) {
    Fixture fixture;
    const std::string baseline_hash = core::reviews::ComputeModelSemanticHash(fixture.accepted);

    const std::string group = fixture.BeginGroup("Add a decomposition");
    fixture.Stage(group, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});

    // The model the store was handed is the same model afterwards. Materializing
    // works on a copy, so the accepted argument cannot be changed by looking at
    // the draft.
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(fixture.accepted), baseline_hash);
    EXPECT_EQ(fixture.accepted.elements.size(), 1u);

    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(fixture.accepted), baseline_hash);
    EXPECT_GT(result.working_model.elements.size(), fixture.accepted.elements.size());
}

TEST(DraftWorkspace, DiscardLeavesTheAcceptedModelByteIdentical) {
    Fixture fixture;
    const std::string baseline_hash = core::reviews::ComputeModelSemanticHash(fixture.accepted);

    const std::string group = fixture.BeginGroup("Add a decomposition");
    fixture.Stage(group, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    fixture.store.Materialize(fixture.accepted, 1);

    std::string error;
    ASSERT_TRUE(fixture.store.DiscardWorkspace(error)) << error;
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(fixture.accepted), baseline_hash);
    EXPECT_FALSE(fixture.store.has_workspace());
}

// --------------------------------------------------------------------------
// Invariant 3: one argument file has at most one active workspace.
// --------------------------------------------------------------------------

TEST(DraftWorkspace, ReopeningTheSameArgumentRestoresTheSameWorkspace) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Clarify G1");
    fixture.Stage(group, {UpdateTextOp("G1", "The system is acceptably safe in its operational design domain.")});
    const std::uint64_t revision = fixture.store.revision();

    std::string error;
    fixture.store.Close();
    ASSERT_FALSE(fixture.store.has_workspace());
    ASSERT_TRUE(fixture.store.Open(fixture.argument_file, fixture.accepted, error)) << error;

    ASSERT_TRUE(fixture.store.has_workspace());
    EXPECT_EQ(fixture.store.revision(), revision);
    EXPECT_EQ(fixture.store.workspace()->groups.size(), 1u);
    EXPECT_EQ(fixture.store.workspace()->groups.front().id, group);
}

// The sequence `AppRuntime::SyncDraftWorkspace` drives every frame: the argument
// changes, the store is repointed, and each argument keeps its own draft.
//
// Element ids repeat across a project's arguments -- each is seeded from the same
// template, so each starts with the same top goal -- so a workspace left pointing
// at the previous argument would decorate this one's identically-named elements.
// That defect has been shipped once already, in the change-set overlay.
TEST(DraftWorkspace, SwitchingArgumentsAndBackKeepsEachDraftWithItsOwnArgument) {
    Fixture fixture;
    const std::string first_group = fixture.BeginGroup("Clarify the first argument");
    fixture.Stage(first_group, {UpdateTextOp("G1", "First argument wording.")});

    const std::filesystem::path second_argument = fixture.dir.path / "arguments" / "secondary.sacm";
    std::string error;
    ASSERT_TRUE(fixture.store.Open(second_argument, fixture.accepted, error)) << error;
    EXPECT_FALSE(fixture.store.has_workspace()) << "the second argument has no draft of its own yet";

    const std::string second_group =
        fixture.store.BeginGroup(McpRequest("Clarify the second argument"), fixture.accepted, error);
    ASSERT_FALSE(second_group.empty()) << error;
    ASSERT_TRUE(fixture.store.StageOperations(
        second_group, {UpdateTextOp("G1", "Second argument wording.")}, fixture.accepted, error))
        << error;

    // Back to the first. Its own group is there, and the second argument's is
    // not.
    ASSERT_TRUE(fixture.store.Open(fixture.argument_file, fixture.accepted, error)) << error;
    ASSERT_TRUE(fixture.store.has_workspace());
    ASSERT_EQ(fixture.store.workspace()->groups.size(), 1u);
    EXPECT_EQ(fixture.store.workspace()->groups.front().title, "Clarify the first argument");

    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(result.success) << result.error;
    const core::SacmElement* element = FindElement(result.working_model, "G1");
    ASSERT_NE(element, nullptr);
    EXPECT_EQ(element->content, "First argument wording.");
}

TEST(DraftWorkspace, ClosingTheProjectForgetsTheDraftWithoutDeletingIt) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Clarify G1");
    fixture.Stage(group, {UpdateTextOp("G1", "Clarified.")});

    // What the runtime does when no argument is loaded. Closing the application
    // is not a decision about unaccepted work: the draft is recovery state and
    // waits.
    fixture.store.SetProjectRoot({});
    EXPECT_FALSE(fixture.store.has_workspace());

    fixture.store.SetProjectRoot(fixture.dir.path);
    std::string error;
    ASSERT_TRUE(fixture.store.Open(fixture.argument_file, fixture.accepted, error)) << error;
    ASSERT_TRUE(fixture.store.has_workspace());
    EXPECT_EQ(fixture.store.workspace()->groups.size(), 1u);
}

TEST(DraftWorkspace, ADifferentArgumentGetsItsOwnWorkspace) {
    Fixture fixture;
    fixture.BeginGroup("Clarify G1");

    const std::filesystem::path other = fixture.dir.path / "arguments" / "secondary.sacm";
    std::string error;
    ASSERT_TRUE(fixture.store.Open(other, fixture.accepted, error)) << error;
    // Element ids repeat across a project's arguments, so a draft written
    // against one must not decorate another's identically-named elements.
    EXPECT_FALSE(fixture.store.has_workspace());
}

// --------------------------------------------------------------------------
// Invariant 4 and 5: deterministic materialization, identities allocated once.
// --------------------------------------------------------------------------

TEST(DraftWorkspace, MaterializationIsDeterministicAcrossSaveAndLoad) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Add a decomposition");
    fixture.Stage(group, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});

    const core::drafts::DraftMaterializationResult& first = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(first.success) << first.error;
    const std::string first_hash = core::reviews::ComputeModelSemanticHash(first.working_model);
    const std::string first_identity = IdentityFor(fixture.store, group, "$sub");
    ASSERT_FALSE(first_identity.empty());

    core::drafts::DraftWorkspaceStore reloaded;
    reloaded.SetProjectRoot(fixture.dir.path);
    std::string error;
    ASSERT_TRUE(reloaded.Open(fixture.argument_file, fixture.accepted, error)) << error;

    const core::drafts::DraftMaterializationResult& second = reloaded.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(second.success) << second.error;
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(second.working_model), first_hash);
    EXPECT_EQ(IdentityFor(reloaded, group, "$sub"), first_identity);
}

TEST(DraftWorkspace, AProposedElementKeepsItsIdentityWhenAnotherGroupIsAdded) {
    Fixture fixture;
    const std::string first_group = fixture.BeginGroup("Add a decomposition");
    fixture.Stage(first_group, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    fixture.store.Materialize(fixture.accepted, 1);
    const std::string identity = IdentityFor(fixture.store, first_group, "$sub");
    ASSERT_FALSE(identity.empty());

    // A second client adds its own creation. Nothing about the first client's
    // element has changed, and the id it was told about must still name it --
    // otherwise a multi-turn conversation is developing an element that no
    // longer exists under that name.
    const std::string second_group = fixture.BeginGroup("Add a second branch", "SCCG AI Review");
    fixture.Stage(second_group, {CreateClaimOp("$other", "Residual risk is accepted."), SupportOp("$other", "G1")});

    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(IdentityFor(fixture.store, first_group, "$sub"), identity);
    EXPECT_NE(IdentityFor(fixture.store, second_group, "$other"), identity);
    EXPECT_NE(FindElement(result.working_model, identity), nullptr);
}

TEST(DraftWorkspace, ExtendingAGroupDoesNotRenameWhatItAlreadyCreated) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Add a decomposition");
    fixture.Stage(group, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    fixture.store.Materialize(fixture.accepted, 1);
    const std::string identity = IdentityFor(fixture.store, group, "$sub");
    ASSERT_FALSE(identity.empty());

    fixture.Stage(group, {CreateClaimOp("$later", "Mitigations are verified."), SupportOp("$later", "G1")});
    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(result.success) << result.error;

    // The whole point of allocating rather than regenerating: a create operation
    // added later must not shuffle the ids the earlier ones already produced.
    EXPECT_EQ(IdentityFor(fixture.store, group, "$sub"), identity);
    EXPECT_NE(IdentityFor(fixture.store, group, "$later"), identity);
}

// The case that actually distinguishes pinning identities from regenerating
// them.
//
// Regeneration is deterministic, so it agrees with pinning as long as nothing
// *earlier* in the sequence moves -- which is why appending to a group, or
// saving and loading, cannot tell the two apart. Removing an earlier group can:
// it frees the ids that group was using, and a regenerating materializer hands
// them straight to the group behind it. An agent developing the element it was
// told it created would then be editing a different one.
TEST(DraftWorkspace, RejectingAnEarlierGroupDoesNotRenameALaterGroupsElement) {
    Fixture fixture;
    const std::string first = fixture.BeginGroup("First branch");
    fixture.Stage(first, {CreateClaimOp("$a", "Hazards are mitigated."), SupportOp("$a", "G1")});
    const std::string second = fixture.BeginGroup("Second branch", "SCCG AI Review");
    fixture.Stage(second, {CreateClaimOp("$b", "Residual risk is accepted."), SupportOp("$b", "G1")});

    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    const std::string second_identity = IdentityFor(fixture.store, second, "$b");
    ASSERT_FALSE(second_identity.empty());

    std::string error;
    ASSERT_TRUE(fixture.store.RejectGroup(first, error)) << error;

    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(fixture.accepted, 2);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(IdentityFor(fixture.store, second, "$b"), second_identity);
    EXPECT_NE(FindElement(result.working_model, second_identity), nullptr);
}

TEST(DraftWorkspace, GroupsMaterializeInSequenceOrderRegardlessOfStoredOrder) {
    Fixture fixture;
    const std::string first = fixture.BeginGroup("First");
    const std::string second = fixture.BeginGroup("Second");
    fixture.Stage(first, {UpdateTextOp("G1", "First wording.")});
    fixture.Stage(second, {UpdateTextOp("G1", "Second wording.")});

    const core::drafts::DraftMaterializationResult& ordered = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(ordered.success) << ordered.error;
    const core::SacmElement* element = FindElement(ordered.working_model, "G1");
    ASSERT_NE(element, nullptr);
    EXPECT_EQ(element->content, "Second wording.");

    // Reversing the stored order must change nothing: materialization is by
    // sequence, so the working model survives a save and load that reorders the
    // array.
    core::drafts::DraftWorkspace reversed = *fixture.store.workspace();
    std::reverse(reversed.groups.begin(), reversed.groups.end());
    const core::drafts::DraftMaterializationResult result = core::drafts::MaterializeDraft(reversed, fixture.accepted);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(result.working_model),
              core::reviews::ComputeModelSemanticHash(ordered.working_model));
}

// --------------------------------------------------------------------------
// Invariant 6: the revision moves on every successful mutation.
// --------------------------------------------------------------------------

TEST(DraftWorkspace, EveryMutationBumpsTheWorkingRevision) {
    Fixture fixture;
    const std::uint64_t before_group = fixture.store.revision();

    const std::string group = fixture.BeginGroup("Clarify G1");
    const std::uint64_t after_group = fixture.store.revision();
    EXPECT_GT(after_group, before_group);

    fixture.Stage(group, {UpdateTextOp("G1", "Clarified.")});
    const std::uint64_t after_stage = fixture.store.revision();
    EXPECT_GT(after_stage, after_group);

    std::string error;
    ASSERT_TRUE(fixture.store.RejectGroup(group, error)) << error;
    EXPECT_GT(fixture.store.revision(), after_stage);
}

TEST(DraftWorkspace, ARefusedStagingDoesNotMoveTheRevision) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Clarify a claim that is not there");
    const std::uint64_t revision = fixture.store.revision();

    std::string error;
    EXPECT_FALSE(fixture.store.StageOperations(group, {UpdateTextOp("G404", "Nothing.")}, fixture.accepted, error));
    EXPECT_FALSE(error.empty());
    // A refusal is not a change. An agent told the revision moved would reread
    // for nothing, and a revision that moves without a change makes the
    // staleness token meaningless.
    EXPECT_EQ(fixture.store.revision(), revision);
    EXPECT_TRUE(fixture.store.workspace()->FindGroup(group)->operations.empty());
}

TEST(DraftWorkspace, StagingRefusesAPatchThatCouldNotBeDrawn) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Support something that does not exist");

    std::string error;
    core::reviews::PatchOperation dangling;
    dangling.type = core::reviews::PatchOperationType::AddSupportedBy;
    core::reviews::ElementRef source;
    source.existing_id = "G99";
    core::reviews::ElementRef target;
    target.existing_id = "G1";
    dangling.source = source;
    dangling.target = target;

    EXPECT_FALSE(fixture.store.StageOperations(group, {dangling}, fixture.accepted, error));
    EXPECT_FALSE(error.empty());

    // The canvas has to be able to draw the working model at any moment, so a
    // group is never allowed to hold a patch that cannot be materialized.
    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(fixture.accepted, 1);
    EXPECT_TRUE(result.success) << result.error;
}

// --------------------------------------------------------------------------
// Invariant 8: a failure names the group and leaves the accepted model alone.
// --------------------------------------------------------------------------

TEST(DraftWorkspace, AGroupThatCannotBeAppliedNamesItselfAndYieldsTheAcceptedModel) {
    const core::AssuranceCase accepted = BaselineCase();

    // Built directly rather than through the store, because the store refuses to
    // hold this at staging time. It can still arise from recovery data written
    // against a baseline that has since changed.
    core::drafts::DraftWorkspace workspace;
    workspace.id = "draft-test";
    core::drafts::DraftChangeGroup group;
    group.id = "group-1";
    group.sequence = 1;
    group.title = "Reword a claim that is not there";
    group.source = core::drafts::DraftSource::Mcp;
    group.source_label = "Claude Code";
    group.operations = {UpdateTextOp("G404", "Nothing to reword.")};
    workspace.groups.push_back(std::move(group));

    const core::drafts::DraftMaterializationResult result = core::drafts::MaterializeDraft(workspace, accepted);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.failing_group_id, "group-1");
    EXPECT_FALSE(result.error.empty());
    // Not a partially applied argument. The user sees the accepted case, which
    // is the only thing that is still true.
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(result.working_model),
              core::reviews::ComputeModelSemanticHash(accepted));
}

// --------------------------------------------------------------------------
// The reason the whole design exists: findings computed over the combination.
// --------------------------------------------------------------------------

TEST(DraftWorkspace, TwoIndividuallyReasonableGroupsReportACombinedProblem) {
    Fixture fixture;

    // One client develops G1: a sub-claim, the support for it, the undeveloped
    // marker cleared because it is now developed, and the new leaf marked
    // undeveloped so it does not assert support it has not got. Sound on its own.
    core::reviews::PatchOperation mark_sub_undeveloped;
    mark_sub_undeveloped.type = core::reviews::PatchOperationType::SetUndeveloped;
    core::reviews::ElementRef sub_ref;
    sub_ref.create_ref = "$sub";
    mark_sub_undeveloped.element = sub_ref;

    const std::string mcp_group = fixture.BeginGroup("Develop the top goal");
    fixture.Stage(mcp_group,
                  {CreateClaimOp("$sub", "Hazards are mitigated."),
                   SupportOp("$sub", "G1"),
                   ClearUndevelopedOp("G1"),
                   mark_sub_undeveloped});

    const core::drafts::DraftMaterializationResult& alone = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(alone.success) << alone.error;
    EXPECT_TRUE(alone.sccg_findings.empty()) << "the first group alone is well formed";

    const std::string sub_id = IdentityFor(fixture.store, mcp_group, "$sub");
    ASSERT_FALSE(sub_id.empty());

    // A second source removes that sub-claim. Also defensible in isolation --
    // and between them they leave G1 asserting support it no longer has.
    const std::string review_group = fixture.BeginGroup("Remove the unsupported sub-claim", "SCCG AI Review");
    core::reviews::PatchOperation remove;
    remove.type = core::reviews::PatchOperationType::RemoveElement;
    core::reviews::ElementRef element;
    element.existing_id = sub_id;
    remove.element = element;
    fixture.Stage(review_group, {remove});

    const core::drafts::DraftMaterializationResult& combined = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(combined.success) << combined.error;

    bool reports_unsupported_g1 = false;
    for (const core::sccg::StagedFinding& finding : combined.sccg_findings) {
        if (finding.guideline_id == "EV.1" && finding.element_id == "G1")
            reports_unsupported_g1 = true;
    }
    // This finding exists in neither group on its own. Producing it is the whole
    // reason the draft is materialized as one model rather than as two patches.
    EXPECT_TRUE(reports_unsupported_g1);
}

TEST(DraftWorkspace, TheChangeIndexAttributesEachContributionToItsGroup) {
    Fixture fixture;
    const std::string mcp_group = fixture.BeginGroup("Clarify the top goal");
    fixture.Stage(mcp_group, {UpdateTextOp("G1", "First wording.")});
    const std::string review_group = fixture.BeginGroup("Clarify it further", "SCCG AI Review");
    fixture.Stage(review_group, {UpdateTextOp("G1", "Second wording.")});

    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(result.success) << result.error;

    const core::drafts::DraftElementEntry* entry = result.change_index.Find("G1");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->change, core::drafts::DraftElementChange::Modified);

    // A reviewer approving this element is entitled to see that two sources
    // touched it, in order, rather than the net wording alone.
    const std::vector<std::string> contributors = result.change_index.ContributingGroupIds("G1");
    ASSERT_EQ(contributors.size(), 2u);
    EXPECT_EQ(contributors[0], mcp_group);
    EXPECT_EQ(contributors[1], review_group);
}

TEST(DraftWorkspace, ARejectedGroupLeavesTheWorkingModel) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Clarify G1");
    fixture.Stage(group, {UpdateTextOp("G1", "Clarified.")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);

    std::string error;
    ASSERT_TRUE(fixture.store.RejectGroup(group, error)) << error;

    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(result.working_model),
              core::reviews::ComputeModelSemanticHash(fixture.accepted));
    // Kept, not deleted: the record of what was proposed and declined survives.
    EXPECT_EQ(fixture.store.workspace()->groups.size(), 1u);
}

// --------------------------------------------------------------------------
// Recovery.
// --------------------------------------------------------------------------

TEST(DraftWorkspace, AChangedBaselineEntersNeedsRebaseAndReplaysNothing) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Clarify G1");
    fixture.Stage(group, {UpdateTextOp("G1", "Clarified.")});
    fixture.store.Close();

    // The argument moved while the draft was not loaded.
    core::AssuranceCase changed = BaselineCase();
    changed.elements.push_back(Strategy("S1", "Argue over hazards."));

    std::string error;
    ASSERT_TRUE(fixture.store.Open(fixture.argument_file, changed, error)) << error;
    ASSERT_TRUE(fixture.store.has_workspace());
    EXPECT_EQ(fixture.store.workspace()->state, core::drafts::DraftWorkspaceState::NeedsRebase);

    // Not replayed. Applying a patch to a document it was not written for is how
    // a tool silently reinterprets a safety argument, so the user is shown the
    // accepted case until they decide what to do.
    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(changed, 1);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(result.working_model),
              core::reviews::ComputeModelSemanticHash(changed));

    EXPECT_FALSE(fixture.store.StageOperations(group, {UpdateTextOp("G1", "More.")}, changed, error));
    EXPECT_FALSE(error.empty());
}

TEST(DraftWorkspace, RecoveryDataLivesUnderTheInternalDirectoryAndNotInTheArgument) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Clarify G1");
    fixture.Stage(group, {UpdateTextOp("G1", "Clarified.")});

    const std::filesystem::path drafts = core::drafts::DraftsDirectory(fixture.dir.path);
    EXPECT_TRUE(std::filesystem::exists(drafts));
    EXPECT_EQ(drafts.parent_path().filename(), ".af");

    // Proposal state stays out of SACM entirely: there is no assertion
    // declaration meaning "AI-proposed and not accepted", and a private tag would
    // read as ordinary argument content to any tool that ignores it.
    EXPECT_FALSE(std::filesystem::exists(fixture.argument_file));
}

TEST(DraftWorkspace, AnUnknownSchemaIsRefusedRatherThanGuessedAt) {
    core::drafts::DraftWorkspace workspace;
    std::string error;
    EXPECT_FALSE(core::drafts::DeserializeDraftWorkspace(R"({"schema":"something.else.v9"})", workspace, error));
    EXPECT_FALSE(error.empty());
}

TEST(DraftWorkspace, SerializationRoundTripsGroupsAndIdentities) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Add a decomposition");
    fixture.Stage(group, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    fixture.store.Materialize(fixture.accepted, 1);

    const core::drafts::DraftWorkspace& original = *fixture.store.workspace();
    core::drafts::DraftWorkspace restored;
    std::string error;
    ASSERT_TRUE(
        core::drafts::DeserializeDraftWorkspace(core::drafts::SerializeDraftWorkspace(original), restored, error))
        << error;

    ASSERT_EQ(restored.groups.size(), original.groups.size());
    EXPECT_EQ(restored.working_revision, original.working_revision);
    EXPECT_EQ(restored.base_model_hash, original.base_model_hash);
    EXPECT_EQ(restored.groups.front().id, original.groups.front().id);
    EXPECT_EQ(restored.groups.front().sequence, original.groups.front().sequence);
    EXPECT_EQ(restored.groups.front().source, original.groups.front().source);
    EXPECT_EQ(restored.groups.front().source_label, original.groups.front().source_label);
    EXPECT_EQ(restored.groups.front().operations.size(), original.groups.front().operations.size());
    EXPECT_EQ(restored.groups.front().generated_ids, original.groups.front().generated_ids);
}

// --------------------------------------------------------------------------
// The "changes only" view mode.
// --------------------------------------------------------------------------

TEST(DraftWorkspace, ChangesOnlyKeepsThePathFromEachChangeToTheRoot) {
    Fixture fixture;
    // A deeper baseline, so there is a path to lose.
    fixture.accepted.elements.push_back(Strategy("S1", "Argue over identified hazards."));
    fixture.accepted.elements.push_back(Claim("G2", "Hazard H1 is mitigated.", true));
    fixture.accepted.elements.push_back(Claim("G3", "Hazard H2 is mitigated.", true));
    fixture.accepted.elements.push_back(Supports("R1", "S1", "G1"));
    fixture.accepted.elements.push_back(Supports("R2", "G2", "S1"));
    fixture.accepted.elements.push_back(Supports("R3", "G3", "S1"));

    std::string error;
    ASSERT_TRUE(fixture.store.Open(fixture.argument_file, fixture.accepted, error)) << error;
    const std::string group = fixture.BeginGroup("Clarify one leaf");
    fixture.Stage(group, {UpdateTextOp("G2", "Hazard H1 is mitigated by the redundant channel.")});

    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(result.success) << result.error;

    const core::AssuranceCase view = core::drafts::BuildChangesOnlyView(result.working_model, result.change_index);

    // The changed claim, and the argument above it. A claim shown without the
    // strategy that introduces it and the goal it serves is not reviewable.
    EXPECT_NE(FindElement(view, "G2"), nullptr);
    EXPECT_NE(FindElement(view, "S1"), nullptr);
    EXPECT_NE(FindElement(view, "G1"), nullptr);
    EXPECT_NE(FindElement(view, "R2"), nullptr);
    EXPECT_NE(FindElement(view, "R1"), nullptr);

    // The untouched sibling is not part of this change and is left out.
    EXPECT_EQ(FindElement(view, "G3"), nullptr);
    // ...and so is the relationship that would otherwise dangle from it, which
    // would read as a structural defect the argument does not have.
    EXPECT_EQ(FindElement(view, "R3"), nullptr);
}

// --------------------------------------------------------------------------
// Promotion: compiling the draft into one thing the ordinary apply path takes.
// --------------------------------------------------------------------------

TEST(DraftWorkspace, PromotionCompilesEveryGroupIntoOneProposal) {
    Fixture fixture;
    const std::string first = fixture.BeginGroup("Add a decomposition");
    fixture.Stage(first, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    const std::string second = fixture.BeginGroup("Clarify the top goal", "SCCG AI Review");
    fixture.Stage(second, {UpdateTextOp("G1", "Clarified.")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);

    const core::drafts::CompiledDraftPromotion compiled =
        core::drafts::CompileWorkspacePromotion(*fixture.store.workspace(), "Jesper");
    ASSERT_TRUE(compiled.success) << compiled.error;

    // One proposal, one command, one audit transaction, one undo boundary.
    EXPECT_EQ(compiled.proposal.operations.size(), 3u);
    ASSERT_EQ(compiled.group_ids.size(), 2u);
    EXPECT_EQ(compiled.group_ids[0], first);
    EXPECT_EQ(compiled.group_ids[1], second);

    // Every contributor named, so the audit record can say an AI wrote part of
    // this and which one.
    ASSERT_EQ(compiled.source_labels.size(), 2u);
    EXPECT_EQ(compiled.source_labels[0], "Claude Code");
    EXPECT_EQ(compiled.source_labels[1], "SCCG AI Review");
}

TEST(DraftWorkspace, PromotionKeepsTheIdentitiesTheDraftWasShownUnder) {
    Fixture fixture;
    const std::string first = fixture.BeginGroup("First branch");
    fixture.Stage(first, {CreateClaimOp("$a", "Hazards are mitigated."), SupportOp("$a", "G1")});
    const std::string second = fixture.BeginGroup("Second branch", "SCCG AI Review");
    fixture.Stage(second, {CreateClaimOp("$b", "Residual risk is accepted."), SupportOp("$b", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);

    // Reject the first, so the second's pinned id is no longer the one a fresh
    // allocation would choose. This is the case where promotion would renumber.
    std::string error;
    ASSERT_TRUE(fixture.store.RejectGroup(first, error)) << error;
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 2).success);
    const std::string shown = IdentityFor(fixture.store, second, "$b");
    ASSERT_FALSE(shown.empty());

    const core::drafts::CompiledDraftPromotion compiled =
        core::drafts::CompileWorkspacePromotion(*fixture.store.workspace(), "Jesper");
    ASSERT_TRUE(compiled.success) << compiled.error;

    // Applied with the draft's own identities, the promoted element keeps the id
    // the reviewer read and the agent was told. Element ids reach reports and
    // conversations; renumbering them at the moment of acceptance would quietly
    // invalidate both.
    core::AssuranceCase promoted = fixture.accepted;
    const core::reviews::ReviewProposalPatchService service;
    const core::reviews::ApplyProposalResult result =
        service.ApplyProposalWithIds(compiled.proposal, promoted, compiled.identities);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_NE(FindElement(promoted, shown), nullptr);
}

TEST(DraftWorkspace, PromotionDoesNotFuseTwoSourcesThatChoseTheSameCreateRef) {
    Fixture fixture;
    // Nothing coordinates the patch-local names two clients pick, and `$goal` is
    // the obvious choice for both. Concatenating without namespacing would make
    // one element out of two proposed claims -- and silently, because the result
    // still applies.
    const std::string first = fixture.BeginGroup("One client's branch");
    fixture.Stage(first, {CreateClaimOp("$goal", "Hazards are mitigated."), SupportOp("$goal", "G1")});
    const std::string second = fixture.BeginGroup("Another client's branch", "SCCG AI Review");
    fixture.Stage(second, {CreateClaimOp("$goal", "Residual risk is accepted."), SupportOp("$goal", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);

    const core::drafts::CompiledDraftPromotion compiled =
        core::drafts::CompileWorkspacePromotion(*fixture.store.workspace(), "Jesper");
    ASSERT_TRUE(compiled.success) << compiled.error;
    EXPECT_EQ(compiled.identities.size(), 2u) << "two proposed claims, two identities";

    core::AssuranceCase promoted = fixture.accepted;
    const core::reviews::ReviewProposalPatchService service;
    const core::reviews::ApplyProposalResult result =
        service.ApplyProposalWithIds(compiled.proposal, promoted, compiled.identities);
    ASSERT_TRUE(result.success) << result.error;

    EXPECT_NE(FindElement(promoted, IdentityFor(fixture.store, first, "$goal")), nullptr);
    EXPECT_NE(FindElement(promoted, IdentityFor(fixture.store, second, "$goal")), nullptr);
    EXPECT_NE(IdentityFor(fixture.store, first, "$goal"), IdentityFor(fixture.store, second, "$goal"));
}

// --------------------------------------------------------------------------
// `.af/` must not reach a colleague through version control.
// --------------------------------------------------------------------------

TEST(DraftWorkspace, ANewProjectIgnoresItsInternalDirectory) {
    TempDir dir{UniqueTempPath("scaffold")};
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("Ignored", dir.path, project, report, error)) << error;

    const std::filesystem::path ignore = project.rootPath / ".af" / ".gitignore";
    ASSERT_TRUE(std::filesystem::exists(ignore)) << ignore.string();

    const std::expected<std::string, std::string> content = core::ReadTextFile(ignore);
    ASSERT_TRUE(content.has_value()) << content.error();
    // Caches and backups being committed was untidy. Drafts hold argument text
    // no human has accepted, which is a different order of problem.
    EXPECT_NE(content.value().find('*'), std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(project.rootPath / ".af" / "drafts"));
}
