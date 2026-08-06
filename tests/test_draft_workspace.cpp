#include "core/drafts/draft_workspace_store.h"

#include "core/drafts/draft_materializer.h"
#include "core/drafts/draft_dependency_graph.h"
#include "core/audit/audit_transaction.h"
#include "core/audit/undo_boundary.h"
#include "core/drafts/draft_persistence.h"
#include "core/drafts/draft_promotion_service.h"
#include "core/reviews/review_proposal_patch_service.h"
#include "core/project_file_io.h"
#include "core/project_service.h"
#include "core/reviews/review_proposal.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
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

// Makes a fixture's project root unwritable in a platform-independent way: the
// path becomes a regular file while the original directory is kept beside it.
// Destruction restores the directory before TempDir performs its cleanup.
struct TemporarilyUnavailableProjectRoot {
    std::filesystem::path original;
    std::filesystem::path relocated;

    explicit TemporarilyUnavailableProjectRoot(const std::filesystem::path& path) : original(path) {
        relocated = original;
        relocated += "_relocated";
        std::filesystem::rename(original, relocated);
        std::ofstream blocker(original, std::ios::binary | std::ios::trunc);
        blocker << "not a directory";
    }

    ~TemporarilyUnavailableProjectRoot() {
        std::error_code ec;
        std::filesystem::remove(original, ec);
        ec.clear();
        std::filesystem::rename(relocated, original, ec);
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

TEST(DraftWorkspace, PublishedFrameSnapshotSurvivesDiscard) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Add a decomposition");
    fixture.Stage(group, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});

    const std::shared_ptr<const core::drafts::DraftMaterializationResult> frame =
        fixture.store.MaterializeSnapshot(fixture.accepted, 1);
    ASSERT_TRUE(frame->success) << frame->error;
    const std::string shown_id = IdentityFor(fixture.store, group, "$sub");
    ASSERT_NE(FindElement(frame->working_model, shown_id), nullptr);

    std::string error;
    ASSERT_TRUE(fixture.store.DiscardWorkspace(error)) << error;
    EXPECT_FALSE(fixture.store.has_workspace());

    // A banner click happens before the canvas and inspector finish rendering
    // the frame. The published model must remain readable until those consumers
    // release it even though the workspace itself is already gone.
    EXPECT_NE(FindElement(frame->working_model, shown_id), nullptr);
    EXPECT_TRUE(frame->success);
}

TEST(DraftWorkspace, PublishedFrameSnapshotSurvivesPromotionCleanup) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Add a decomposition");
    fixture.Stage(group, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});

    const std::shared_ptr<const core::drafts::DraftMaterializationResult> frame =
        fixture.store.MaterializeSnapshot(fixture.accepted, 1);
    ASSERT_TRUE(frame->success) << frame->error;
    const std::string shown_id = IdentityFor(fixture.store, group, "$sub");

    std::string error;
    ASSERT_TRUE(fixture.store.RemovePromotedGroups({group}, frame->working_model, error)) << error;
    EXPECT_FALSE(fixture.store.has_workspace());

    // Successful Accept All removes the last active group in the same frame.
    // The old working snapshot is still the one that frame's canvas owns.
    EXPECT_NE(FindElement(frame->working_model, shown_id), nullptr);
    EXPECT_TRUE(frame->success);
}

TEST(DraftWorkspace, PendingPromotionIsInertAndCancelsWhenAcceptedFileStayedAtBaseline) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Clarify G1");
    fixture.Stage(group, {UpdateTextOp("G1", "Clarified.")});
    const core::AssuranceCase promoted = fixture.store.Materialize(fixture.accepted, 1).working_model;

    std::string error;
    ASSERT_TRUE(fixture.store.BeginPromotion({group}, promoted, error)) << error;
    ASSERT_NE(fixture.store.workspace(), nullptr);
    EXPECT_EQ(fixture.store.workspace()->state, core::drafts::DraftWorkspaceState::Promoting);
    EXPECT_TRUE(fixture.store.workspace()->pending_promotion.has_value());
    const core::drafts::DraftMaterializationResult& inert = fixture.store.Materialize(fixture.accepted, 1);
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(inert.working_model),
              core::reviews::ComputeModelSemanticHash(fixture.accepted));
    EXPECT_FALSE(fixture.store.DiscardWorkspace(error));

    fixture.store.Close();
    ASSERT_TRUE(fixture.store.Open(fixture.argument_file, fixture.accepted, error)) << error;
    ASSERT_NE(fixture.store.workspace(), nullptr);
    EXPECT_EQ(fixture.store.workspace()->state, core::drafts::DraftWorkspaceState::Active);
    EXPECT_FALSE(fixture.store.workspace()->pending_promotion.has_value());
    EXPECT_NE(fixture.store.workspace()->FindGroup(group), nullptr);
}

TEST(DraftWorkspace, PendingPromotionFinalizesWhenAcceptedFileHasExpectedResult) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Clarify G1");
    fixture.Stage(group, {UpdateTextOp("G1", "Clarified.")});
    const core::AssuranceCase promoted = fixture.store.Materialize(fixture.accepted, 1).working_model;

    std::string error;
    ASSERT_TRUE(fixture.store.BeginPromotion({group}, promoted, error)) << error;
    fixture.store.Close();

    ASSERT_TRUE(fixture.store.Open(fixture.argument_file, promoted, error)) << error;
    EXPECT_FALSE(fixture.store.has_workspace()) << "recovery should finish cleanup, not replay accepted operations";
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

TEST(DraftWorkspace, AllocationReservesIdsHiddenFromTheCanvasProjection) {
    Fixture fixture;
    // G2 represents an authoritative package/utility identity that the flat
    // accepted canvas model does not contain. Allocation must still see it.
    fixture.store.SetAuthoritativeIdentities({"G1", "G2"});

    const std::string group = fixture.BeginGroup("Add a decomposition");
    fixture.Stage(group, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});

    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(IdentityFor(fixture.store, group, "$sub"), "G3");
    EXPECT_NE(FindElement(result.working_model, "G3"), nullptr);
}

TEST(DraftWorkspace, APinnedIdentityCollisionIsNotSilentlyRenumbered) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Add a decomposition");
    fixture.Stage(group, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    ASSERT_EQ(IdentityFor(fixture.store, group, "$sub"), "G2");

    // Discovering that G2 exists in the full SACM document after it has been
    // published is a rebase conflict. Picking G3 here would break every agent
    // and UI reference that already names the proposed element as G2.
    fixture.store.SetAuthoritativeIdentities({"G1", "G2"});
    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(fixture.accepted, 1);
    EXPECT_TRUE(result.success);
    ASSERT_NE(fixture.store.workspace(), nullptr);
    EXPECT_EQ(fixture.store.workspace()->state, core::drafts::DraftWorkspaceState::NeedsRebase);
    EXPECT_EQ(FindElement(result.working_model, "G2"), nullptr)
        << "a conflicting draft must not be replayed against the authoritative document";
    EXPECT_EQ(IdentityFor(fixture.store, group, "$sub"), "G2");
}

TEST(DraftWorkspace, ReplacingOperationsRehearsesWithEveryReusedIdentityStillPinned) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Add a decomposition");
    fixture.Stage(group, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    const std::string shown_identity = IdentityFor(fixture.store, group, "$sub");
    ASSERT_EQ(shown_identity, "G2");

    // The full authoritative document now reveals that the published identity
    // is occupied. Replacement must rehearse with G2 still pinned and refuse;
    // rehearsing after clearing it would silently try G3, pass, then restore the
    // conflicting G2 into the real workspace.
    fixture.store.SetAuthoritativeIdentities({"G1", shown_identity});
    std::string error;
    EXPECT_FALSE(fixture.store.ReplaceOperations(
        group,
        {CreateClaimOp("$sub", "Identified hazards are mitigated."), SupportOp("$sub", "G1")},
        fixture.accepted,
        error));
    EXPECT_NE(error.find("already used"), std::string::npos) << error;

    const core::drafts::DraftChangeGroup* unchanged = fixture.store.workspace()->FindGroup(group);
    ASSERT_NE(unchanged, nullptr);
    ASSERT_EQ(unchanged->operations.size(), 2u);
    EXPECT_EQ(unchanged->operations.front().text, "Hazards are mitigated.");
    EXPECT_EQ(IdentityFor(fixture.store, group, "$sub"), shown_identity);
}

TEST(DraftWorkspace, MaterializationFailsClosedWhenNewIdentitiesCannotBePersisted) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Add a decomposition");
    fixture.Stage(group, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(IdentityFor(fixture.store, group, "$sub").empty()) << "the first materialization allocates it";

    {
        TemporarilyUnavailableProjectRoot unavailable(fixture.dir.path);
        const core::drafts::DraftMaterializationResult& failed = fixture.store.Materialize(fixture.accepted, 1);
        EXPECT_FALSE(failed.success);
        EXPECT_NE(failed.error.find("persist"), std::string::npos) << failed.error;
        EXPECT_EQ(failed.working_model.elements.size(), fixture.accepted.elements.size());
        EXPECT_TRUE(IdentityFor(fixture.store, group, "$sub").empty())
            << "an identity that was not saved must not remain published in memory";
    }

    // The failure is not cached. Once storage is available, the next frame can
    // allocate, persist, and publish the same draft normally.
    const core::drafts::DraftMaterializationResult& retried = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(retried.success) << retried.error;
    const std::string persisted_identity = IdentityFor(fixture.store, group, "$sub");
    ASSERT_FALSE(persisted_identity.empty());

    std::vector<std::filesystem::path> workspace_files;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(fixture.dir.path / ".af" / "drafts")) {
        if (entry.is_regular_file() && entry.path().filename() == "workspace.json")
            workspace_files.push_back(entry.path());
    }
    ASSERT_EQ(workspace_files.size(), 1u) << "the successful retry must write exactly one recovery workspace";

    const std::expected<std::string, std::string> stored_json = core::ReadTextFile(workspace_files.front());
    ASSERT_TRUE(stored_json.has_value()) << stored_json.error();
    core::drafts::DraftWorkspace stored;
    std::string error;
    ASSERT_TRUE(core::drafts::DeserializeDraftWorkspace(stored_json.value(), stored, error)) << error;
    const core::drafts::DraftChangeGroup* stored_group = stored.FindGroup(group);
    ASSERT_NE(stored_group, nullptr);
    EXPECT_EQ(stored_group->generated_ids.at("$sub"), persisted_identity);
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

TEST(DraftWorkspace, ARemovalKeepsAnUnmodifiedPresentationTombstone) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Remove G1");

    core::reviews::PatchOperation remove;
    remove.type = core::reviews::PatchOperationType::RemoveElement;
    core::reviews::ElementRef element;
    element.existing_id = "G1";
    remove.element = element;
    fixture.Stage(group, {remove});

    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(FindElement(result.working_model, "G1"), nullptr)
        << "deleted elements must stay absent from the semantic working model";

    const core::drafts::DraftElementEntry* entry = result.change_index.Find("G1");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->change, core::drafts::DraftElementChange::Removed);
    ASSERT_EQ(result.change_index.removed.size(), 1u);
    EXPECT_EQ(result.change_index.removed.front().id, "G1");
    EXPECT_EQ(result.change_index.removed.front().content, fixture.accepted.elements.front().content)
        << "the canvas tombstone must show the accepted content, not synthesize a replacement";
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

TEST(DraftWorkspace, AChangedOpenBaselineEntersNeedsRebaseBeforeReplay) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Clarify G1");
    fixture.Stage(group, {UpdateTextOp("G1", "Clarified.")});

    // The accepted argument can change through a direct human edit while the
    // project remains open. Open() is not called again in that workflow, so the
    // materialization boundary must enforce the same base-hash invariant.
    core::AssuranceCase changed = fixture.accepted;
    changed.elements.push_back(Strategy("S1", "Argue over hazards."));

    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(changed, 2);
    ASSERT_TRUE(result.success) << result.error;
    ASSERT_NE(fixture.store.workspace(), nullptr);
    EXPECT_EQ(fixture.store.workspace()->state, core::drafts::DraftWorkspaceState::NeedsRebase);
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(result.working_model),
              core::reviews::ComputeModelSemanticHash(changed));

    std::string error;
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
    const core::AssuranceCase promoted = fixture.store.Materialize(fixture.accepted, 1).working_model;

    std::string begin_error;
    ASSERT_TRUE(fixture.store.BeginPromotion({group}, promoted, begin_error)) << begin_error;

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
    ASSERT_TRUE(restored.pending_promotion.has_value());
    EXPECT_EQ(restored.state, core::drafts::DraftWorkspaceState::Promoting);
    EXPECT_EQ(restored.pending_promotion->group_ids, original.pending_promotion->group_ids);
    EXPECT_EQ(restored.pending_promotion->expected_model_hash, original.pending_promotion->expected_model_hash);
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

TEST(DraftWorkspace, PromotionAuthorKeepsLabelsThatAreSubstringsOfOtherLabels) {
    Fixture fixture;
    const std::string long_label = fixture.BeginGroup("AI review", "SCCG AI Review");
    fixture.Stage(long_label, {UpdateTextOp("G1", "Clarified by the review.")});
    const std::string short_label = fixture.BeginGroup("AI follow-up", "AI");
    fixture.Stage(short_label, {UpdateTextOp("G1", "Clarified by both contributors.")});

    EXPECT_EQ(core::drafts::DraftPromotionAuthor(*fixture.store.workspace(), {long_label, short_label}),
              "SCCG AI Review, AI");
    EXPECT_EQ(core::drafts::DraftPromotionAuthor(*fixture.store.workspace(), {short_label, long_label, short_label}),
              "AI, SCCG AI Review");
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
// A human's own edits while a draft is active.
// --------------------------------------------------------------------------

TEST(DraftWorkspace, AHumanEditOfADraftCreatedElementStaysInTheDraft) {
    Fixture fixture;
    const std::string baseline_hash = core::reviews::ComputeModelSemanticHash(fixture.accepted);

    // An agent proposes a claim. It exists only in the working model -- the
    // accepted argument has never heard of it.
    const std::string mcp_group = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(mcp_group, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    const std::string sub_id = IdentityFor(fixture.store, mcp_group, "$sub");
    ASSERT_FALSE(sub_id.empty());
    ASSERT_EQ(FindElement(fixture.accepted, sub_id), nullptr) << "not in the accepted argument";

    // The user rewords it. This is the case the accepted-model edit path could
    // not handle at all: the element it names does not exist there, so the edit
    // either failed or landed on something else.
    std::string error;
    const std::string human_group = fixture.store.BeginGroup(
        [] {
            core::drafts::DraftGroupRequest request;
            request.title = "My edits";
            request.source = core::drafts::DraftSource::Human;
            request.source_label = "Jesper";
            return request;
        }(),
        fixture.accepted,
        error);
    ASSERT_FALSE(human_group.empty()) << error;
    ASSERT_TRUE(fixture.store.StageOperations(
        human_group, {UpdateTextOp(sub_id, "Identified hazards are mitigated to ALARP.")}, fixture.accepted, error))
        << error;

    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(result.success) << result.error;
    const core::SacmElement* working = FindElement(result.working_model, sub_id);
    ASSERT_NE(working, nullptr);
    EXPECT_EQ(working->content, "Identified hazards are mitigated to ALARP.");

    // And the accepted argument is untouched by any of it.
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(fixture.accepted), baseline_hash);
    EXPECT_EQ(FindElement(fixture.accepted, sub_id), nullptr);

    // The human edit is attributed to the person, not folded into the agent's
    // group: a reviewer needs to see that a human touched an AI's proposal.
    const core::drafts::DraftElementEntry* entry = result.change_index.Find(sub_id);
    ASSERT_NE(entry, nullptr);
    const std::vector<std::string> contributors = result.change_index.ContributingGroupIds(sub_id);
    ASSERT_EQ(contributors.size(), 2u);
    EXPECT_EQ(contributors[0], mcp_group);
    EXPECT_EQ(contributors[1], human_group);
}

// --------------------------------------------------------------------------
// Dependencies and selective acceptance.
// --------------------------------------------------------------------------

TEST(DraftWorkspace, AnEditOfAnotherGroupsCreationDependsOnIt) {
    Fixture fixture;
    const std::string creator = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(creator, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    const std::string sub_id = IdentityFor(fixture.store, creator, "$sub");
    ASSERT_FALSE(sub_id.empty());

    // The SCCG group edits an element that does not exist in the accepted
    // argument at all. Accepting it alone is not a smaller change; it is an
    // impossible one.
    const std::string editor = fixture.BeginGroup("Reword the sub-claim", "SCCG AI Review");
    fixture.Stage(editor, {UpdateTextOp(sub_id, "Identified hazards are mitigated to ALARP.")});

    const std::map<std::string, std::vector<std::string>> dependencies =
        core::drafts::InferDraftDependencies(*fixture.store.workspace());
    ASSERT_EQ(dependencies.at(editor).size(), 1u);
    EXPECT_EQ(dependencies.at(editor).front(), creator);
    EXPECT_TRUE(dependencies.at(creator).empty());

    const std::vector<std::string> closure = core::drafts::DependencyClosure(*fixture.store.workspace(), {editor});
    ASSERT_EQ(closure.size(), 2u);
    EXPECT_EQ(closure[0], creator) << "sequence order, so the creation is applied first";
    EXPECT_EQ(closure[1], editor);
}

TEST(DraftWorkspace, AWordingEditWithNoDependenciesPromotesAlone) {
    Fixture fixture;
    const std::string branch = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(branch, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    const std::string wording = fixture.BeginGroup("Clarify the top goal", "SCCG AI Review");
    fixture.Stage(wording, {UpdateTextOp("G1", "Clarified.")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);

    const core::drafts::DraftPromotionPlan plan =
        core::drafts::PlanDraftPromotion(*fixture.store.workspace(), fixture.accepted, {wording}, "Jesper");
    ASSERT_TRUE(plan.ok) << plan.error;
    ASSERT_EQ(plan.closure.size(), 1u) << "it depends on nothing, so nothing else comes with it";
    EXPECT_EQ(plan.closure.front(), wording);
    EXPECT_TRUE(plan.added_by_closure.empty());

    // The unrelated branch is still only proposed.
    const core::SacmElement* promoted_claim = FindElement(plan.promoted_model, "G1");
    ASSERT_NE(promoted_claim, nullptr);
    EXPECT_EQ(promoted_claim->content, "Clarified.");
    EXPECT_EQ(FindElement(plan.promoted_model, IdentityFor(fixture.store, branch, "$sub")), nullptr);
}

TEST(DraftWorkspace, AcceptingADependentChangeTakesWhatItNeedsAndSaysSo) {
    Fixture fixture;
    const std::string creator = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(creator, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    const std::string sub_id = IdentityFor(fixture.store, creator, "$sub");

    const std::string editor = fixture.BeginGroup("Reword the sub-claim", "SCCG AI Review");
    fixture.Stage(editor, {UpdateTextOp(sub_id, "Identified hazards are mitigated to ALARP.")});

    const core::drafts::DraftPromotionPlan plan =
        core::drafts::PlanDraftPromotion(*fixture.store.workspace(), fixture.accepted, {editor}, "Jesper");
    ASSERT_TRUE(plan.ok) << plan.error;

    // The creation comes with it -- and is reported, so the UI can tell the user
    // before they commit rather than widening what they asked for in silence.
    ASSERT_EQ(plan.closure.size(), 2u);
    ASSERT_EQ(plan.added_by_closure.size(), 1u);
    EXPECT_EQ(plan.added_by_closure.front(), creator);

    const core::SacmElement* promoted = FindElement(plan.promoted_model, sub_id);
    ASSERT_NE(promoted, nullptr) << "the created element, at the id it was shown under";
    EXPECT_EQ(promoted->content, "Identified hazards are mitigated to ALARP.");
}

TEST(DraftWorkspace, PromotionIsRefusedWhenTheRemainderCannotBeRebased) {
    Fixture fixture;
    const std::string creator = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(creator, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    const std::string sub_id = IdentityFor(fixture.store, creator, "$sub");

    // A later group removes what the first created.
    const std::string remover = fixture.BeginGroup("Remove the sub-claim", "SCCG AI Review");
    core::reviews::PatchOperation remove;
    remove.type = core::reviews::PatchOperationType::RemoveElement;
    core::reviews::ElementRef element;
    element.existing_id = sub_id;
    remove.element = element;
    fixture.Stage(remover, {remove});

    // Promoting the removal alone would accept a baseline in which the element
    // never existed, leaving the creation group with nothing to do -- and the
    // removal itself referring to an element that was never created. Refused,
    // and refused *before* the accepted argument is touched.
    const core::drafts::DraftPromotionPlan plan =
        core::drafts::PlanDraftPromotion(*fixture.store.workspace(), fixture.accepted, {remover}, "Jesper");
    if (!plan.ok) {
        EXPECT_FALSE(plan.error.empty());
    } else {
        // If the closure pulled the creation in, that is equally correct: the two
        // are promoted together and nothing is stranded.
        EXPECT_EQ(plan.closure.size(), 2u);
    }
}

TEST(DraftWorkspace, RejectingACreationIdentifiesWhatDependsOnIt) {
    Fixture fixture;
    const std::string creator = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(creator, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    const std::string sub_id = IdentityFor(fixture.store, creator, "$sub");

    const std::string editor = fixture.BeginGroup("Reword the sub-claim", "SCCG AI Review");
    fixture.Stage(editor, {UpdateTextOp(sub_id, "Identified hazards are mitigated to ALARP.")});

    // Rejecting the creation strands the rewording: it would refer to an element
    // that will never exist. The user is offered the cascade rather than finding
    // out at promotion.
    const std::vector<std::string> dependents = core::drafts::DependentsOf(*fixture.store.workspace(), {creator});
    ASSERT_EQ(dependents.size(), 1u);
    EXPECT_EQ(dependents.front(), editor);

    EXPECT_TRUE(core::drafts::DependentsOf(*fixture.store.workspace(), {editor}).empty())
        << "nothing depends on the rewording";
}

// --------------------------------------------------------------------------
// Undoing a promotion.
//
// Promotion is one boundary on the accepted undo stack, but a draft is not a
// command and has no entry on that stack. Undo therefore restores an accepted
// model the remaining groups were never rebased onto -- and, when the promotion
// consumed the last group, one whose work has already been deleted from the
// draft. The draft was the only copy.
// --------------------------------------------------------------------------

namespace {

// The store-side half of `AppRuntime::PromoteDraftGroups`: everything it does
// around the audited command, in the order it does it. `sequence` stands in for
// the audit transaction the real promotion records.
core::drafts::DraftPromotionPlan
PromoteThroughStore(Fixture& fixture, const std::vector<std::string>& group_ids, std::uint64_t sequence) {
    const core::drafts::DraftPromotionPlan plan =
        core::drafts::PlanDraftPromotion(*fixture.store.workspace(), fixture.accepted, group_ids, "Jesper");
    EXPECT_TRUE(plan.ok) << plan.error;
    if (!plan.ok)
        return plan;

    const core::drafts::DraftWorkspace pre_promotion = *fixture.store.workspace();
    std::string error;
    EXPECT_TRUE(fixture.store.BeginPromotion(plan.closure, plan.promoted_model, error)) << error;
    EXPECT_TRUE(fixture.store.SavePromotionSnapshot(sequence, pre_promotion, error)) << error;
    EXPECT_TRUE(fixture.store.RemovePromotedGroups(plan.closure, plan.promoted_model, error)) << error;
    return plan;
}

} // namespace

TEST(DraftWorkspace, PromotingTheLastGroupStillLeavesSomethingToUndoInto) {
    Fixture fixture;
    const std::string branch = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(branch, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);

    PromoteThroughStore(fixture, {branch}, 7);

    // Accepting the last group deletes the workspace directory outright, which is
    // exactly why the snapshot is not kept inside it.
    EXPECT_EQ(fixture.store.workspace(), nullptr);
    EXPECT_FALSE(std::filesystem::exists(core::drafts::DraftWorkspaceDirectory(
        fixture.dir.path, core::drafts::ArgumentStableKey(std::filesystem::path("arguments") / "main.sacm"))));
    EXPECT_TRUE(fixture.store.HasPromotionSnapshot(7));
}

TEST(DraftWorkspace, UndoingAPromotionPutsBackTheDraftItConsumed) {
    Fixture fixture;
    const std::string branch = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(branch, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    const std::string sub_id = IdentityFor(fixture.store, branch, "$sub");
    ASSERT_FALSE(sub_id.empty());

    PromoteThroughStore(fixture, {branch}, 7);
    ASSERT_EQ(fixture.store.workspace(), nullptr);

    // The undo restored the accepted model to the baseline. Without the restore
    // below, the created claim is now in neither the argument nor the draft.
    core::drafts::DraftWorkspace snapshot;
    std::string error;
    ASSERT_TRUE(fixture.store.LoadPromotionSnapshot(7, snapshot, error)) << error;
    ASSERT_TRUE(fixture.store.RestorePromotionSnapshot(snapshot, fixture.accepted, error)) << error;

    const core::drafts::DraftWorkspace* restored = fixture.store.workspace();
    ASSERT_NE(restored, nullptr);
    ASSERT_EQ(restored->groups.size(), 1u);
    EXPECT_EQ(restored->groups.front().id, branch);
    EXPECT_EQ(restored->groups.front().title, "Add a sub-claim");
    EXPECT_EQ(restored->groups.front().source_label, "Claude Code")
        << "provenance comes back with the work, or the audit record of a re-acceptance is wrong";

    // And it comes back at the identity it was shown under, so a selection or an
    // agent's reference to it still resolves.
    EXPECT_EQ(IdentityFor(fixture.store, branch, "$sub"), sub_id);
    const core::drafts::DraftMaterializationResult& materialized = fixture.store.Materialize(fixture.accepted, 2);
    ASSERT_TRUE(materialized.success) << materialized.error;
    EXPECT_NE(FindElement(materialized.working_model, sub_id), nullptr);
}

TEST(DraftWorkspace, ARestoredDraftIsNotStaleAgainstTheArgumentTheUndoRestored) {
    Fixture fixture;
    const std::string branch = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(branch, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    // A second group, so the promotion is partial and the workspace survives it
    // carrying the post-promotion base hash. Promoting the last group instead
    // would delete the workspace and restore the snapshot's own hash, which is
    // already the right one -- and would assert nothing about the restore.
    const std::string unrelated = fixture.BeginGroup("Clarify the top goal", "SCCG AI Review");
    fixture.Stage(unrelated, {UpdateTextOp("G1", "Clarified.")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);

    PromoteThroughStore(fixture, {branch}, 7);
    ASSERT_NE(fixture.store.workspace(), nullptr);

    core::drafts::DraftWorkspace snapshot;
    std::string error;
    ASSERT_TRUE(fixture.store.LoadPromotionSnapshot(7, snapshot, error)) << error;
    ASSERT_TRUE(fixture.store.RestorePromotionSnapshot(snapshot, fixture.accepted, error)) << error;

    // `RemovePromotedGroups` moved the base hash forward to the promoted model.
    // If the restore leaves it there, the very next open compares the draft
    // against an argument the undo has already taken away and declares it stale
    // -- inert work, restored into a state it can never leave.
    core::drafts::DraftWorkspaceStore reopened;
    reopened.SetProjectRoot(fixture.dir.path);
    ASSERT_TRUE(reopened.Open(fixture.argument_file, fixture.accepted, error)) << error;
    ASSERT_NE(reopened.workspace(), nullptr);
    EXPECT_EQ(reopened.workspace()->state, core::drafts::DraftWorkspaceState::Active);
    ASSERT_EQ(reopened.workspace()->groups.size(), 2u);
    EXPECT_EQ(reopened.workspace()->groups.front().id, branch);
}

TEST(DraftWorkspace, UndoingAPromotionKeepsWorkStagedAfterIt) {
    Fixture fixture;
    const std::string branch = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(branch, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    const std::string unrelated = fixture.BeginGroup("Clarify the top goal", "SCCG AI Review");
    fixture.Stage(unrelated, {UpdateTextOp("G1", "Clarified.")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    const std::string sub_id = IdentityFor(fixture.store, branch, "$sub");

    const core::drafts::DraftPromotionPlan plan = PromoteThroughStore(fixture, {branch}, 11);
    ASSERT_NE(fixture.store.workspace(), nullptr);
    ASSERT_EQ(fixture.store.workspace()->groups.size(), 1u) << "the unrelated group stays behind";

    // A third group arrives after the promotion, against the promoted baseline,
    // and develops the element the promotion accepted.
    std::string error;
    const std::string later = fixture.store.BeginGroup(McpRequest("Qualify the sub-claim"), plan.promoted_model, error);
    ASSERT_FALSE(later.empty()) << error;
    ASSERT_TRUE(fixture.store.StageOperations(
        later, {UpdateTextOp(sub_id, "Identified hazards are mitigated to ALARP.")}, plan.promoted_model, error))
        << error;

    core::drafts::DraftWorkspace snapshot;
    ASSERT_TRUE(fixture.store.LoadPromotionSnapshot(11, snapshot, error)) << error;
    ASSERT_TRUE(fixture.store.RestorePromotionSnapshot(snapshot, fixture.accepted, error)) << error;

    // Restoring the snapshot wholesale would take the workspace back to two
    // groups and drop the third, trading one silent loss of unaccepted work for
    // another.
    const core::drafts::DraftWorkspace* restored = fixture.store.workspace();
    ASSERT_NE(restored, nullptr);
    ASSERT_EQ(restored->groups.size(), 3u);
    EXPECT_EQ(restored->groups[0].id, branch) << "reinstated at its original sequence, ahead of what followed it";
    EXPECT_EQ(restored->groups[1].id, unrelated);
    EXPECT_EQ(restored->groups[2].id, later);

    // The group staged after the promotion referred to an element only the
    // promoted group creates. Reinstating that group with its generated ids is
    // what keeps the reference resolvable.
    const core::drafts::DraftMaterializationResult& materialized = fixture.store.Materialize(fixture.accepted, 2);
    ASSERT_TRUE(materialized.success) << materialized.error;
    const core::SacmElement* developed = FindElement(materialized.working_model, sub_id);
    ASSERT_NE(developed, nullptr);
    EXPECT_EQ(developed->content, "Identified hazards are mitigated to ALARP.");
}

TEST(DraftWorkspace, AnOrdinaryTransactionHasNoPromotionSnapshot) {
    Fixture fixture;
    const std::string branch = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(branch, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    PromoteThroughStore(fixture, {branch}, 7);

    // The file existing is what marks a transaction as a promotion, so undo never
    // has to infer it from a command name that ordinary proposal application
    // shares.
    EXPECT_TRUE(fixture.store.HasPromotionSnapshot(7));
    EXPECT_FALSE(fixture.store.HasPromotionSnapshot(6));
    EXPECT_FALSE(fixture.store.HasPromotionSnapshot(8));

    core::drafts::DraftWorkspace ignored;
    std::string error;
    EXPECT_FALSE(fixture.store.LoadPromotionSnapshot(6, ignored, error));
    EXPECT_TRUE(error.empty()) << "not a promotion is not a failure";
}

TEST(DraftWorkspace, AnUnreadableSnapshotIsNotReportedAsAbsent) {
    Fixture fixture;
    const std::string branch = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(branch, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    PromoteThroughStore(fixture, {branch}, 7);

    // A directory where the snapshot file should be: the sort of thing a botched
    // sync or a half-restored backup leaves behind. It exists, so this is not
    // "that transaction was not a promotion" -- but it cannot be read.
    const std::filesystem::path path = core::drafts::DraftPromotionSnapshotPath(fixture.dir.path, 7);
    std::filesystem::remove(path);
    std::filesystem::create_directories(path);

    // The two answers must stay distinguishable. Undo discards unaccepted work on
    // this distinction, and reading "cannot be read" as "not a promotion" sends a
    // transaction that *is* one down the ordinary path -- destroying the draft in
    // precisely the case the check exists to protect.
    core::drafts::DraftWorkspace snapshot;
    std::string error;
    EXPECT_FALSE(fixture.store.LoadPromotionSnapshot(7, snapshot, error));
    EXPECT_FALSE(error.empty()) << "an unreadable snapshot must not read as 'this was not a promotion'";
}

TEST(DraftWorkspace, APromotionSnapshotIsRefusedRatherThanGuessedAt) {
    Fixture fixture;
    const std::string branch = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(branch, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    PromoteThroughStore(fixture, {branch}, 7);

    {
        std::ofstream damaged(core::drafts::DraftPromotionSnapshotPath(fixture.dir.path, 7),
                              std::ios::binary | std::ios::trunc);
        damaged << R"({"schema":"assurance-forge.draft-workspace.v99"})";
    }

    // Read through the wrong schema, a draft is a draft quietly altered. The
    // caller is told, and `AppRuntime::Undo` refuses the undo rather than
    // destroying the accepted copy of work it cannot put back.
    core::drafts::DraftWorkspace snapshot;
    std::string error;
    EXPECT_FALSE(fixture.store.LoadPromotionSnapshot(7, snapshot, error));
    EXPECT_FALSE(error.empty());
}

TEST(DraftWorkspace, APromotionFromAnotherArgumentIsNotRestoredHere) {
    Fixture fixture;
    const std::string branch = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(branch, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    PromoteThroughStore(fixture, {branch}, 7);

    core::drafts::DraftWorkspace snapshot;
    std::string error;
    ASSERT_TRUE(fixture.store.LoadPromotionSnapshot(7, snapshot, error)) << error;
    snapshot.argument_file = fixture.dir.path / "arguments" / "main2.sacm";

    // Element ids repeat across a project's arguments -- every argument seeded
    // from the template starts with the same top goal -- so a draft restored into
    // the wrong one lands on ids that happen to match.
    EXPECT_FALSE(fixture.store.RestorePromotionSnapshot(snapshot, fixture.accepted, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(fixture.store.workspace(), nullptr) << "and nothing is left half-restored";
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

// --------------------------------------------------------------------------
// Undo needs two stacks.
//
// A draft edit records no audit transaction and triggers no `.sacm` autosave --
// that is what keeps the accepted file byte-stable while a draft is built -- so
// it cannot use the accepted model's undo stack. These cover the stack it uses
// instead, and the places where the two must not be confused for each other.
// --------------------------------------------------------------------------

TEST(DraftWorkspace, UndoingADraftEditRestoresThePreviousWorkingModel) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Reword the top goal");
    fixture.Stage(group, {UpdateTextOp("G1", "First wording.")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);

    fixture.Stage(group, {UpdateTextOp("G1", "Second wording.")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    ASSERT_EQ(FindElement(fixture.store.Materialize(fixture.accepted, 1).working_model, "G1")->content,
              "Second wording.");

    ASSERT_TRUE(fixture.store.CanUndoDraftEdit());
    std::string error;
    ASSERT_TRUE(fixture.store.UndoDraftEdit(error)) << error;

    const core::drafts::DraftMaterializationResult& after = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(after.success);
    EXPECT_EQ(FindElement(after.working_model, "G1")->content, "First wording.")
        << "the working model is back to what it was before the last edit";
}

TEST(DraftWorkspace, UndoingADraftEditWritesNoByteOfTheAcceptedArgument) {
    Fixture fixture;
    // Pinned on the bytes on disk, not on a hash of the in-memory baseline. The
    // store is never handed the accepted model to write, so a hash comparison
    // here would pass however the code behaved -- it would be asserting that a
    // local test object the code cannot reach did not change itself.
    {
        std::ofstream accepted(fixture.argument_file, std::ios::binary | std::ios::trunc);
        accepted << "<sacm:AssuranceCasePackage id=\"case-1\"/>";
    }
    const std::expected<std::string, std::string> before = core::ReadTextFile(fixture.argument_file);
    ASSERT_TRUE(before.has_value()) << before.error();

    const std::string group = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(group, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);

    std::string error;
    ASSERT_TRUE(fixture.store.UndoDraftEdit(error)) << error;

    // Invariant 1, on the undo path. Reversing unaccepted work is not an edit to
    // the safety argument and must not read as one.
    const std::expected<std::string, std::string> after = core::ReadTextFile(fixture.argument_file);
    ASSERT_TRUE(after.has_value()) << after.error();
    EXPECT_EQ(after.value(), before.value());
}

TEST(DraftWorkspace, UndoingADraftEditMovesTheRevisionForwardNotBack) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Reword the top goal");
    fixture.Stage(group, {UpdateTextOp("G1", "First wording.")});
    const std::uint64_t before_second_edit = fixture.store.revision();
    fixture.Stage(group, {UpdateTextOp("G1", "Second wording.")});
    const std::uint64_t after_second_edit = fixture.store.revision();
    ASSERT_GT(after_second_edit, before_second_edit);

    std::string error;
    ASSERT_TRUE(fixture.store.UndoDraftEdit(error)) << error;

    // Invariant 6. The content is back to what revision `before_second_edit`
    // described, but a client holding that token must still be refused: it has
    // not seen the undo, and letting a stale token become valid again because the
    // content happens to match is exactly the race the token exists to stop.
    EXPECT_GT(fixture.store.revision(), after_second_edit);
}

TEST(DraftWorkspace, ARefusedDraftEditLeavesNothingToUndo) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Reword the top goal");
    fixture.Stage(group, {UpdateTextOp("G1", "First wording.")});
    const std::uint64_t settled = fixture.store.revision();

    // Refused: nothing in the accepted argument or the draft has this id.
    std::string error;
    EXPECT_FALSE(fixture.store.StageOperations(group, {UpdateTextOp("NOPE", "x")}, fixture.accepted, error));
    EXPECT_EQ(fixture.store.revision(), settled);

    // An undo entry for an edit that never happened would silently reverse the
    // edit *before* it -- the user's last real change -- while appearing to
    // reverse the one they just saw refused.
    ASSERT_TRUE(fixture.store.CanUndoDraftEdit());
    ASSERT_TRUE(fixture.store.UndoDraftEdit(error)) << error;
    const core::drafts::DraftMaterializationResult& after = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(after.success);
    EXPECT_EQ(FindElement(after.working_model, "G1")->content, BaselineCase().elements.front().content)
        << "one undo reached the staging, not something before it";

    // Two real edits happened -- opening the group and staging into it -- so the
    // stack holds two entries and no more. Counted by draining it, because the
    // depth is what a refused edit would have inflated.
    ASSERT_TRUE(fixture.store.CanUndoDraftEdit());
    ASSERT_TRUE(fixture.store.UndoDraftEdit(error)) << error;
    EXPECT_TRUE(fixture.store.workspace()->groups.empty());
    EXPECT_FALSE(fixture.store.CanUndoDraftEdit());
}

TEST(DraftWorkspace, AcceptingClearsTheDraftUndoHistory) {
    Fixture fixture;
    const std::string branch = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(branch, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    const std::string wording = fixture.BeginGroup("Clarify the top goal", "SCCG AI Review");
    fixture.Stage(wording, {UpdateTextOp("G1", "Clarified.")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);

    const core::drafts::DraftPromotionPlan plan =
        core::drafts::PlanDraftPromotion(*fixture.store.workspace(), fixture.accepted, {wording}, "Jesper");
    ASSERT_TRUE(plan.ok) << plan.error;

    std::string error;
    ASSERT_TRUE(fixture.store.BeginPromotion(plan.closure, plan.promoted_model, error)) << error;
    ASSERT_TRUE(fixture.store.RemovePromotedGroups(plan.closure, plan.promoted_model, error)) << error;

    // The states below the promotion describe a workspace that still held the
    // wording group. Undoing into one would re-stage a change the user has just
    // accepted, so it would appear twice: once in the accepted argument and once
    // as an unaccepted draft change proposing what is already there.
    EXPECT_FALSE(fixture.store.CanUndoDraftEdit());
    EXPECT_FALSE(fixture.store.UndoDraftEdit(error));
    EXPECT_FALSE(error.empty());
}

TEST(DraftWorkspace, AnUndoneGroupIdIsNeverReusedForADifferentGroup) {
    Fixture fixture;
    const std::string first = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(first, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});

    std::string error;
    ASSERT_TRUE(fixture.store.UndoDraftEdit(error)) << error; // the staging
    ASSERT_TRUE(fixture.store.UndoDraftEdit(error)) << error; // the group itself
    ASSERT_EQ(fixture.store.workspace()->groups.size(), 0u);

    const std::string second = fixture.BeginGroup("A different change");
    // The event log still carries `group_created` for the first group. Handing
    // its id to a second, unrelated group would make an MCP client polling
    // `get_draft_events` read one history as though it described the other.
    EXPECT_NE(second, first);
}

// --------------------------------------------------------------------------
// Rejecting one change, and what becomes of the changes built on top of it.
//
// Cascading silently discards work the user never chose to discard. Applying
// only the selection is not an alternative: the stranded groups cannot apply,
// and leaving them in materialization takes the whole draft down. So there is a
// third state, and these cover what it has to mean.
// --------------------------------------------------------------------------

TEST(DraftWorkspace, AStrandedGroupLeavesMaterializationRatherThanBlockingIt) {
    Fixture fixture;
    const std::string creator = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(creator, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    const std::string sub_id = IdentityFor(fixture.store, creator, "$sub");

    const std::string editor = fixture.BeginGroup("Reword the sub-claim", "SCCG AI Review");
    fixture.Stage(editor, {UpdateTextOp(sub_id, "Identified hazards are mitigated to ALARP.")});

    const std::string unrelated = fixture.BeginGroup("Clarify the top goal", "SCCG AI Review");
    fixture.Stage(unrelated, {UpdateTextOp("G1", "Clarified.")});

    std::string error;
    ASSERT_TRUE(fixture.store.RejectGroup(creator, error)) << error;
    ASSERT_TRUE(fixture.store.MarkGroupNeedsAttention(editor, error)) << error;

    // This is the whole reason the stranded group leaves materialization. Left
    // in, its update targets an element the rejection removed, materialization
    // fails, and the working model collapses to the accepted baseline -- so
    // declining a cascade would have blocked the entire draft, including the
    // unrelated change that has nothing to do with either group.
    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(fixture.accepted, 2);
    ASSERT_TRUE(result.success) << result.error << " (group " << result.failing_group_id << ")";
    EXPECT_EQ(FindElement(result.working_model, "G1")->content, "Clarified.")
        << "the unrelated change is still applied";
    EXPECT_EQ(FindElement(result.working_model, sub_id), nullptr) << "and the rejected creation is gone";
}

TEST(DraftWorkspace, AStrandedGroupIsKeptRatherThanCleanedUpAsEmpty) {
    Fixture fixture;
    const std::string creator = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(creator, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    const std::string sub_id = IdentityFor(fixture.store, creator, "$sub");

    const std::string editor = fixture.BeginGroup("Reword the sub-claim", "SCCG AI Review");
    fixture.Stage(editor, {UpdateTextOp(sub_id, "Identified hazards are mitigated to ALARP.")});

    std::string error;
    ASSERT_TRUE(fixture.store.RejectGroup(creator, error)) << error;
    ASSERT_TRUE(fixture.store.MarkGroupNeedsAttention(editor, error)) << error;

    // Nothing materializes now, so a workspace that decided emptiness by what
    // draws would delete itself here -- taking with it the one group the user
    // deliberately chose to keep. Reopening is where that would surface.
    ASSERT_TRUE(fixture.store.workspace()->has_active_groups());
    ASSERT_TRUE(fixture.store.Open(fixture.argument_file, fixture.accepted, error)) << error;
    ASSERT_NE(fixture.store.workspace(), nullptr) << "the stranded work survived a reopen";
    const core::drafts::DraftChangeGroup* restored = fixture.store.workspace()->FindGroup(editor);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->state, core::drafts::DraftGroupState::NeedsAttention);
    EXPECT_EQ(restored->operations.size(), 1u) << "with its operations intact, to be retargeted";
}

TEST(DraftWorkspace, RetargetingAStrandedGroupPutsItBackIntoTheDraft) {
    Fixture fixture;
    const std::string creator = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(creator, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    const std::string sub_id = IdentityFor(fixture.store, creator, "$sub");

    const std::string editor = fixture.BeginGroup("Reword the sub-claim", "SCCG AI Review");
    fixture.Stage(editor, {UpdateTextOp(sub_id, "Identified hazards are mitigated to ALARP.")});

    std::string error;
    ASSERT_TRUE(fixture.store.RejectGroup(creator, error)) << error;
    ASSERT_TRUE(fixture.store.MarkGroupNeedsAttention(editor, error)) << error;

    // Without a way out, "keep it for review" is a slower way of losing the work
    // than rejecting it, because nothing can ever be done with the result.
    ASSERT_TRUE(fixture.store.ReplaceOperations(
        editor, {UpdateTextOp("G1", "Reworded against the top goal.")}, fixture.accepted, error))
        << error;

    const core::drafts::DraftChangeGroup* recovered = fixture.store.workspace()->FindGroup(editor);
    ASSERT_NE(recovered, nullptr);
    EXPECT_EQ(recovered->state, core::drafts::DraftGroupState::Building)
        << "it applies again, so it is no longer stranded";

    const core::drafts::DraftMaterializationResult& result = fixture.store.Materialize(fixture.accepted, 3);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(FindElement(result.working_model, "G1")->content, "Reworded against the top goal.");
}

// --------------------------------------------------------------------------
// Pruning promotion snapshots.
//
// They accumulate one per promotion and are consumed only by an undo. The rule
// for deleting one is the audit undo boundary and nothing cheaper: every
// approximation (keep the last N, drop by age) can delete a snapshot that is
// still reachable, which destroys the only copy of unaccepted work at exactly
// the moment the user asked for it back.
// --------------------------------------------------------------------------

TEST(DraftWorkspace, SnapshotEnumerationIgnoresFilesItDidNotWrite) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(group, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);

    std::string error;
    ASSERT_TRUE(fixture.store.SavePromotionSnapshot(7, *fixture.store.workspace(), error)) << error;
    ASSERT_TRUE(fixture.store.SavePromotionSnapshot(12, *fixture.store.workspace(), error)) << error;

    // Something this code did not write. The one safe thing to do with a file
    // that may hold unaccepted work and whose name is not understood is to leave
    // it alone -- so it must not appear in the list a pruner deletes from.
    const std::filesystem::path directory = core::drafts::DraftPromotionSnapshotsDirectory(fixture.dir.path);
    {
        std::ofstream stray(directory / "notes.json", std::ios::binary | std::ios::trunc);
        stray << "{}";
    }
    {
        std::ofstream stray(directory / "17-backup.json", std::ios::binary | std::ios::trunc);
        stray << "{}";
    }

    const std::vector<std::uint64_t> listed = core::drafts::ListDraftPromotionSnapshots(fixture.dir.path);
    ASSERT_EQ(listed.size(), 2u);
    EXPECT_EQ(listed[0], 7u) << "ascending, so a caller comparing against a boundary can stop early";
    EXPECT_EQ(listed[1], 12u);
}

TEST(DraftWorkspace, PruningKeepsEverySnapshotAnUndoCouldStillReach) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(group, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);

    std::string error;
    for (const std::uint64_t sequence : {3u, 5u, 9u, 14u})
        ASSERT_TRUE(fixture.store.SavePromotionSnapshot(sequence, *fixture.store.workspace(), error)) << error;

    // A baseline at 9 is the undo boundary: `CanUndo` is a strict comparison, so
    // 9 itself and everything below it is unreachable permanently, and 14 is not.
    const std::uint64_t boundary = 9;
    std::vector<std::uint64_t> kept;
    for (const std::uint64_t sequence : core::drafts::ListDraftPromotionSnapshots(fixture.dir.path)) {
        if (core::audit::CanUndo(sequence, boundary)) {
            kept.push_back(sequence);
            continue;
        }
        ASSERT_TRUE(fixture.store.DeletePromotionSnapshot(sequence, error)) << error;
    }

    ASSERT_EQ(kept.size(), 1u);
    EXPECT_EQ(kept.front(), 14u);
    // The one that survives is the one the undo stack can still reach, and it is
    // still readable -- deleting it would be the failure this whole mechanism
    // exists to prevent.
    core::drafts::DraftWorkspace restored;
    EXPECT_TRUE(fixture.store.LoadPromotionSnapshot(14, restored, error)) << error;
    EXPECT_EQ(restored.groups.size(), 1u);
    EXPECT_FALSE(fixture.store.HasPromotionSnapshot(3));
    EXPECT_FALSE(fixture.store.HasPromotionSnapshot(9)) << "at the boundary is already past reach";
}

// --------------------------------------------------------------------------
// Attribution in the audit log.
//
// Accepting a draft consumes it, so whatever the log does not record about
// where the change came from is not recoverable from anywhere else. `author`
// names the approver and the contributing sources; the rest is the trace a
// person auditing the argument a year later needs.
// --------------------------------------------------------------------------

TEST(DraftWorkspace, APromotionTransactionCarriesItsProvenance) {
    core::audit::AuditTransaction tx;
    tx.transaction_sequence = 4;
    tx.transaction_id = "tx-4";
    tx.timestamp = "2026-08-05T10:00:00Z";
    tx.author = "Jesper (accepting work by Claude Code, SCCG AI Review)";
    tx.command_name = "ApplyProposal";
    tx.draft_promotion.group_ids = {"group-1", "group-2"};
    tx.draft_promotion.source_labels = {"Claude Code", "SCCG AI Review"};
    tx.draft_promotion.guideline_ids = {"AR-02"};
    tx.draft_promotion.review_item_ids = {"RI-11"};
    tx.draft_promotion.rationales = {"Add a misuse-hazard branch: the hazard analysis identifies foreseeable misuse."};

    const std::string line = core::audit::SerializeAuditTransactionLine(tx);
    core::audit::AuditTransaction parsed;
    std::string error;
    ASSERT_TRUE(core::audit::ParseAuditTransactionLine(line, parsed, error)) << error;

    EXPECT_EQ(parsed.draft_promotion.group_ids, tx.draft_promotion.group_ids);
    EXPECT_EQ(parsed.draft_promotion.source_labels, tx.draft_promotion.source_labels);
    EXPECT_EQ(parsed.draft_promotion.guideline_ids, tx.draft_promotion.guideline_ids);
    EXPECT_EQ(parsed.draft_promotion.review_item_ids, tx.draft_promotion.review_item_ids);
    EXPECT_EQ(parsed.draft_promotion.rationales, tx.draft_promotion.rationales);
}

TEST(DraftWorkspace, AnOrdinaryTransactionSerializesExactlyAsItDidBefore) {
    core::audit::AuditTransaction tx;
    tx.transaction_sequence = 1;
    tx.transaction_id = "tx-1";
    tx.timestamp = "2026-08-05T10:00:00Z";
    tx.author = "Jesper";
    tx.command_name = "CreateClaim";

    // The hash chain is over the bytes of each line. A field emitted
    // unconditionally -- even as an empty object -- would change the bytes of
    // every ordinary transaction, and a log written by this version would then
    // not verify against one written by the last.
    const std::string line = core::audit::SerializeAuditTransactionLine(tx);
    EXPECT_EQ(line.find("draft_promotion"), std::string::npos) << line;

    core::audit::AuditTransaction parsed;
    std::string error;
    ASSERT_TRUE(core::audit::ParseAuditTransactionLine(line, parsed, error)) << error;
    EXPECT_TRUE(parsed.draft_promotion.empty());
}

TEST(DraftWorkspace, OneGestureIsOneUndoEvenWhenItTouchesSeveralGroups) {
    Fixture fixture;
    const std::string creator = fixture.BeginGroup("Add a sub-claim");
    fixture.Stage(creator, {CreateClaimOp("$sub", "Hazards are mitigated."), SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    const std::string sub_id = IdentityFor(fixture.store, creator, "$sub");

    const std::string editor = fixture.BeginGroup("Reword the sub-claim", "SCCG AI Review");
    fixture.Stage(editor, {UpdateTextOp(sub_id, "Identified hazards are mitigated to ALARP.")});

    std::string error;
    {
        // What "reject this, keep what depended on it" does: two store mutations
        // from one click.
        const core::drafts::DraftWorkspaceStore::EditUndoScope scope(fixture.store, "Rejected Add a sub-claim");
        ASSERT_TRUE(fixture.store.RejectGroup(creator, error)) << error;
        ASSERT_TRUE(fixture.store.MarkGroupNeedsAttention(editor, error)) << error;
    }

    ASSERT_TRUE(fixture.store.CanUndoDraftEdit());
    EXPECT_EQ(fixture.store.NextDraftUndoLabel(), "Rejected Add a sub-claim");
    ASSERT_TRUE(fixture.store.UndoDraftEdit(error)) << error;

    // Both halves come back together. Undoing only the stranding would leave the
    // creation rejected and its dependent healthy -- an argument state the user
    // never chose, produced by the very control that exists to stop them
    // choosing one by accident.
    EXPECT_EQ(fixture.store.workspace()->FindGroup(creator)->state, core::drafts::DraftGroupState::Building);
    EXPECT_EQ(fixture.store.workspace()->FindGroup(editor)->state, core::drafts::DraftGroupState::Building);

    // Exactly one entry, so the *next* undo reaches the edit before the gesture
    // rather than a half-applied state inside it. Asserted by taking it: with
    // per-mutation entries left behind, this second undo lands on
    // "creation rejected, dependent healthy" -- which is the state the whole
    // mechanism exists to make unreachable.
    ASSERT_TRUE(fixture.store.UndoDraftEdit(error)) << error;
    EXPECT_EQ(fixture.store.workspace()->FindGroup(creator)->state, core::drafts::DraftGroupState::Building)
        << "the second undo went past the gesture, not into the middle of it";
    EXPECT_TRUE(fixture.store.workspace()->FindGroup(editor)->operations.empty())
        << "and reached the staging that preceded it";
}

TEST(DraftWorkspace, AGestureThatChangesNothingLeavesNoUndoEntry) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Reword the top goal");
    fixture.Stage(group, {UpdateTextOp("G1", "First wording.")});

    std::string error;
    {
        const core::drafts::DraftWorkspaceStore::EditUndoScope scope(fixture.store, "Rejected nothing at all");
        EXPECT_FALSE(fixture.store.RejectGroup("group-does-not-exist", error));
    }

    // An entry for a gesture that did nothing would make the next Ctrl+Z reverse
    // the user's last real change while appearing to reverse the refused one.
    EXPECT_EQ(fixture.store.NextDraftUndoLabel(), "Reword the top goal");
}

TEST(DraftWorkspace, UndoingADraftEditDoesNotRevivateAStaleDraft) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Reword the top goal");
    fixture.Stage(group, {UpdateTextOp("G1", "First wording.")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);
    fixture.Stage(group, {ClearUndevelopedOp("G1")});

    // The accepted argument changes while the draft is open. This is the path
    // `Open()` does not cover and that does not clear the undo stack, so the
    // entries above still describe an `Active` workspace.
    core::AssuranceCase moved = fixture.accepted;
    moved.elements.push_back(Claim("G2", "Something else was accepted in the meantime."));
    fixture.store.Materialize(moved, 2);
    ASSERT_EQ(fixture.store.workspace()->state, core::drafts::DraftWorkspaceState::NeedsRebase);

    std::string error;
    ASSERT_TRUE(fixture.store.UndoDraftEdit(error)) << error;

    // Restoring the state from the entry would put this back to `Active`.
    // `AppRuntime::PromoteDraftGroups` checks this flag before it checks
    // anything else, so that would reopen the path to promoting a draft against
    // a baseline it was never written for -- until the next materialization
    // happened to re-detect the drift.
    EXPECT_EQ(fixture.store.workspace()->state, core::drafts::DraftWorkspaceState::NeedsRebase)
        << "reversing a draft edit does not change the draft's relationship to the accepted argument";

    // And the edit itself really was reversed, so this is not passing by doing
    // nothing at all.
    EXPECT_EQ(fixture.store.workspace()->FindGroup(group)->operations.size(), 1u);
}

// --------------------------------------------------------------------------
// Bilingual contributions.
//
// A case maintained in English and Japanese is reviewed by people reading one
// or the other. A contribution that reaches the draft in one language only is
// invisible to half of them, so the language has to survive staging, the
// restart-recovery round trip, and materialization onto the canvas.
// --------------------------------------------------------------------------

namespace {

core::reviews::PatchOperation
CreateBilingualClaimOp(const std::string& create_ref, const std::string& english, const std::string& japanese) {
    core::reviews::PatchOperation operation = CreateClaimOp(create_ref, english);
    operation.translations["ja"] = japanese;
    return operation;
}

// An update that revises only the Japanese: no `new_value`, so the English the
// element already carries is left where it is.
core::reviews::PatchOperation TranslateTextOp(const std::string& element_id, const std::string& japanese) {
    core::reviews::PatchOperation operation = UpdateTextOp(element_id, "");
    operation.new_value.clear();
    operation.translations["ja"] = japanese;
    return operation;
}

} // namespace

TEST(DraftWorkspace, ABilingualClaimMaterializesInBothLanguages) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Add a bilingual sub-claim");
    fixture.Stage(group,
                  {CreateBilingualClaimOp(
                       "$sub", "Brake wear is monitored continuously.", "ブレーキの摩耗は継続的に監視される。"),
                   SupportOp("$sub", "G1")});

    const core::drafts::DraftMaterializationResult& materialized = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(materialized.success) << materialized.error;

    const core::SacmElement* claim = FindElement(materialized.working_model, IdentityFor(fixture.store, group, "$sub"));
    ASSERT_NE(claim, nullptr);
    EXPECT_EQ(claim->content, "Brake wear is monitored continuously.");
    // The canvas language toggle reads this map; without the entry it falls
    // back to English and the toggle silently shows the wrong language.
    EXPECT_EQ(claim->content_langs.at("ja"), "ブレーキの摩耗は継続的に監視される。");
}

TEST(DraftWorkspace, SerializationRoundTripsTranslations) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Add a bilingual sub-claim");
    fixture.Stage(group,
                  {CreateBilingualClaimOp(
                       "$sub", "Brake wear is monitored continuously.", "ブレーキの摩耗は継続的に監視される。"),
                   SupportOp("$sub", "G1")});

    core::drafts::DraftWorkspace restored;
    std::string error;
    ASSERT_TRUE(core::drafts::DeserializeDraftWorkspace(
        core::drafts::SerializeDraftWorkspace(*fixture.store.workspace()), restored, error))
        << error;

    // Recovery state is the only copy of unaccepted work. A translation dropped
    // here is lost at the next restart, with the English left behind to suggest
    // nothing went missing.
    ASSERT_EQ(restored.groups.size(), 1u);
    ASSERT_FALSE(restored.groups.front().operations.empty());
    EXPECT_EQ(restored.groups.front().operations.front().translations.at("ja"), "ブレーキの摩耗は継続的に監視される。");
}

TEST(DraftWorkspace, PromotionReportsMachineWrittenTranslationsForReview) {
    Fixture fixture;
    const std::string mcp_group = fixture.BeginGroup("Add a bilingual sub-claim");
    fixture.Stage(mcp_group,
                  {CreateBilingualClaimOp(
                       "$sub", "Brake wear is monitored continuously.", "ブレーキの摩耗は継続的に監視される。"),
                   SupportOp("$sub", "G1")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);

    const std::vector<std::string> flagged =
        core::drafts::MachineTranslatedElementIds(*fixture.store.workspace(), {mcp_group});

    // Promotion accepts the argument. It does not establish that the Japanese
    // says what the English says, and nobody has read it in Japanese yet.
    ASSERT_EQ(flagged.size(), 1u);
    EXPECT_EQ(flagged.front(), IdentityFor(fixture.store, mcp_group, "$sub"));
}

TEST(DraftWorkspace, RemovingATranslationIsNotFlaggedForReview) {
    Fixture fixture;
    const std::string mcp_group = fixture.BeginGroup("Drop the stale Japanese");

    // An empty value is a removal, not a translation: WriteLanguage erases that
    // language rather than storing a blank string.
    fixture.Stage(mcp_group, {TranslateTextOp("G1", "")});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);

    // The map is populated, but nothing was added for anyone to read. Asking a
    // human to review text that is no longer there teaches them the warning
    // means nothing, and the next real one gets waved through with it.
    EXPECT_TRUE(core::drafts::MachineTranslatedElementIds(*fixture.store.workspace(), {mcp_group}).empty());
}

TEST(DraftWorkspace, AddingOneTranslationWhileRemovingAnotherIsStillFlagged) {
    Fixture fixture;
    const std::string mcp_group = fixture.BeginGroup("Retranslate the top goal");

    core::reviews::PatchOperation operation = TranslateTextOp("G1", "制動サブシステムは所定の性能目標を満たす。");
    operation.translations["sv"] = "";
    fixture.Stage(mcp_group, {operation});
    ASSERT_TRUE(fixture.store.Materialize(fixture.accepted, 1).success);

    // A removal alongside an addition must not suppress the flag. Skipping the
    // whole operation because one entry is empty would hide a machine-written
    // sentence behind a tidy-up in the same edit.
    const std::vector<std::string> flagged =
        core::drafts::MachineTranslatedElementIds(*fixture.store.workspace(), {mcp_group});
    ASSERT_EQ(flagged.size(), 1u);
    EXPECT_EQ(flagged.front(), "G1");
}

TEST(DraftWorkspace, AHumanTypedTranslationIsNotFlaggedForReview) {
    Fixture fixture;
    core::drafts::DraftGroupRequest request;
    request.title = "My edits";
    request.source = core::drafts::DraftSource::Human;
    request.source_label = "Jesper";
    std::string error;
    const std::string human_group = fixture.store.BeginGroup(request, fixture.accepted, error);
    ASSERT_FALSE(human_group.empty()) << error;

    fixture.Stage(human_group, {TranslateTextOp("G1", "制動サブシステムは所定の性能目標を満たす。")});

    // A person who typed the translation has already reviewed it; telling them
    // to review their own sentence is noise that devalues the warning.
    EXPECT_TRUE(core::drafts::MachineTranslatedElementIds(*fixture.store.workspace(), {human_group}).empty());
}

TEST(DraftWorkspace, TranslatingAnExistingClaimLeavesItsEnglishAlone) {
    Fixture fixture;
    const std::string group = fixture.BeginGroup("Translate the top goal");
    fixture.Stage(group, {TranslateTextOp("G1", "制動サブシステムは所定の性能目標を満たす。")});

    const core::drafts::DraftMaterializationResult& materialized = fixture.store.Materialize(fixture.accepted, 1);
    ASSERT_TRUE(materialized.success) << materialized.error;

    const core::SacmElement* goal = FindElement(materialized.working_model, "G1");
    ASSERT_NE(goal, nullptr);
    // Translating a safety case must not edit the safety case.
    EXPECT_EQ(goal->content, "The braking subsystem meets its stated performance targets.");
    EXPECT_EQ(goal->content_langs.at("ja"), "制動サブシステムは所定の性能目標を満たす。");
}
