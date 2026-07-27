#include "core/changesets/change_set_store.h"

#include "agent/operations.h"
#include "core/app_state.h"
#include "parser/model_utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

// A change set is what an agent builds and a human accepts.
//
// The two properties that matter most here are what the previous design got
// wrong: staging must change nothing, and a change set must always be
// previewable so the canvas can draw it at any moment.

namespace {

struct TempDir {
    std::filesystem::path path;
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::filesystem::path UniqueTempPath(const std::string& stem) {
    static int                  counter = 0;
    const std::filesystem::path path    = std::filesystem::temp_directory_path() /
                                       ("af_change_sets_" + stem + "_" + std::to_string(++counter));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

bool OpenProjectWithArgument(core::AppState& state, const std::filesystem::path& workspace) {
    if (!state.create_empty_project("Project", workspace.string())) {
        ADD_FAILURE() << "could not create project: " << state.status_message;
        return false;
    }
    for (const core::ProjectFileEntry& entry : state.current_project->files) {
        if (entry.role == core::ProjectFileRole::SacmArgument) {
            return state.open_project_file(entry);
        }
    }
    ADD_FAILURE() << "seeded project holds no SACM argument";
    return false;
}

std::string FirstClaimId(const parser::AssuranceCase& model) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.type == "claim") {
            return element.id;
        }
    }
    ADD_FAILURE() << "seeded project holds no claim";
    return {};
}

std::vector<core::reviews::PatchOperation> AddSubGoalUnder(const std::string& parent_id,
                                                           const std::string& text) {
    const nlohmann::json operations = nlohmann::json::array(
        {nlohmann::json{{"type", "CreateClaim"}, {"create_ref", "$sub"}, {"text", text}},
         nlohmann::json{{"type", "AddSupportedBy"},
                        {"source", {{"id", parent_id}}},
                        {"target", {{"ref", "$sub"}}}}});

    std::vector<core::reviews::PatchOperation> parsed;
    std::string                                error;
    if (!agent::ParsePatchOperations(operations, parsed, error)) {
        ADD_FAILURE() << "could not parse operations: " << error;
    }
    return parsed;
}

struct Fixture {
    TempDir                          workspace;
    core::AppState                   state;
    core::changesets::ChangeSetStore store;
};

std::unique_ptr<Fixture> MakeFixture(const std::string& stem) {
    std::unique_ptr<Fixture> fixture(new Fixture{TempDir{UniqueTempPath(stem)}, {}, {}});
    if (!OpenProjectWithArgument(fixture->state, fixture->workspace.path)) {
        return nullptr;
    }
    return fixture;
}

} // namespace

TEST(ChangeSets, StagingChangesNothingInTheModel) {
    std::unique_ptr<Fixture> fixture = MakeFixture("nomutation");
    ASSERT_NE(fixture, nullptr);

    const parser::AssuranceCase before = fixture->state.loaded_case.value();
    const std::string          parent  = FirstClaimId(before);

    const std::string id =
        fixture->store.Begin(1, "Add a maintenance sub-goal", "", "", "claude-ai 0.1.0");

    std::string error;
    ASSERT_TRUE(fixture->store.Stage(id, AddSubGoalUnder(parent, "Maintenance is adequate"),
                                     fixture->state.loaded_case.value(), error))
        << error;

    // The whole point: the agent has staged real operations and the safety case
    // is untouched. It changes once, when a person accepts.
    EXPECT_EQ(fixture->state.loaded_case->elements.size(), before.elements.size());
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(fixture->state.loaded_case.value()),
              core::reviews::ComputeModelSemanticHash(before));
}

TEST(ChangeSets, ReportsWhatItWouldAddAndWhere) {
    std::unique_ptr<Fixture> fixture = MakeFixture("diff");
    ASSERT_NE(fixture, nullptr);
    const std::string parent = FirstClaimId(fixture->state.loaded_case.value());

    const std::string id = fixture->store.Begin(1, "Add a sub-goal", "", "", "claude-ai");
    std::string       error;
    ASSERT_TRUE(fixture->store.Stage(id, AddSubGoalUnder(parent, "Thermal runaway is mitigated"),
                                     fixture->state.loaded_case.value(), error))
        << error;

    const core::changesets::ChangeSetDiff diff = core::changesets::ComputeChangeSetDiff(
        *fixture->store.Find(id), fixture->state.loaded_case.value());

    ASSERT_TRUE(diff.success) << diff.error;
    // A claim and the relationship that attaches it.
    EXPECT_EQ(diff.added_count, 2);
    EXPECT_EQ(diff.removed_count, 0);
    EXPECT_GT(diff.preview_model.elements.size(),
              fixture->state.loaded_case->elements.size());

    // The preview is a whole assurance case, which is what lets the canvas draw
    // the proposal in place rather than as a list of operations beside it.
    bool found_new_claim = false;
    for (const std::pair<const std::string, core::changesets::ElementChange>& entry :
         diff.status_by_id) {
        if (entry.second != core::changesets::ElementChange::Added) {
            continue;
        }
        const parser::SacmElement* element =
            parser::FindElementById(diff.preview_model, entry.first);
        if (element != nullptr && element->content == "Thermal runaway is mitigated") {
            found_new_claim = true;
        }
    }
    EXPECT_TRUE(found_new_claim);
}

TEST(ChangeSets, NamesTheIdItGaveEachCreatedElement) {
    std::unique_ptr<Fixture> fixture = MakeFixture("ids");
    ASSERT_NE(fixture, nullptr);
    const std::string parent = FirstClaimId(fixture->state.loaded_case.value());

    const std::string id = fixture->store.Begin(1, "Add a sub-goal", "", "", "claude-ai");
    std::string       error;
    ASSERT_TRUE(fixture->store.Stage(id, AddSubGoalUnder(parent, "Sub-goal"),
                                     fixture->state.loaded_case.value(), error));

    const core::changesets::ChangeSetDiff diff = core::changesets::ComputeChangeSetDiff(
        *fixture->store.Find(id), fixture->state.loaded_case.value());

    // Without this an agent has to guess what "$sub" became before it can refer
    // to it in a later operation.
    ASSERT_TRUE(diff.generated_ids.count("$sub")) << "generated ids: " << diff.generated_ids.size();
    EXPECT_FALSE(diff.generated_ids.at("$sub").empty());
}

// A change set that cannot be previewed cannot be drawn, and the canvas has to
// be able to draw one at any moment. So operations that would not apply are
// refused at staging time rather than accepted and discovered later.
TEST(ChangeSets, RefusesOperationsThatWouldNotApply) {
    std::unique_ptr<Fixture> fixture = MakeFixture("badops");
    ASSERT_NE(fixture, nullptr);

    const std::string id = fixture->store.Begin(1, "Attach to nothing", "", "", "claude-ai");
    std::string       error;
    EXPECT_FALSE(fixture->store.Stage(id, AddSubGoalUnder("NO-SUCH-ELEMENT", "Orphan"),
                                      fixture->state.loaded_case.value(), error));
    EXPECT_FALSE(error.empty());

    // And the change set is unchanged, not half-staged.
    EXPECT_TRUE(fixture->store.Find(id)->proposal.operations.empty());
}

TEST(ChangeSets, LetsAnAgentTakeBackWhatItStaged) {
    std::unique_ptr<Fixture> fixture = MakeFixture("unstage");
    ASSERT_NE(fixture, nullptr);
    const std::string parent = FirstClaimId(fixture->state.loaded_case.value());

    const std::string id = fixture->store.Begin(1, "Reconsider", "", "", "claude-ai");
    std::string       error;
    ASSERT_TRUE(fixture->store.Stage(id, AddSubGoalUnder(parent, "First thought"),
                                     fixture->state.loaded_case.value(), error));
    ASSERT_EQ(fixture->store.Find(id)->proposal.operations.size(), 2u);

    // Responding to "not there" must not mean starting the conversation over.
    ASSERT_TRUE(fixture->store.Unstage(id, 2, error)) << error;
    EXPECT_TRUE(fixture->store.Find(id)->proposal.operations.empty());
}

TEST(ChangeSets, GivesEachConnectionItsOwnChangeSet) {
    std::unique_ptr<Fixture> fixture = MakeFixture("twoclients");
    ASSERT_NE(fixture, nullptr);

    const std::string first  = fixture->store.Begin(1, "First client", "", "", "claude-ai");
    const std::string second = fixture->store.Begin(2, "Second client", "", "", "cursor");

    ASSERT_NE(fixture->store.OpenFor(1), nullptr);
    ASSERT_NE(fixture->store.OpenFor(2), nullptr);
    EXPECT_EQ(fixture->store.OpenFor(1)->id, first);
    EXPECT_EQ(fixture->store.OpenFor(2)->id, second);
    EXPECT_EQ(fixture->store.Open().size(), 2u);
}

// A second `begin` on one connection means the agent has moved on. Keeping the
// first would leave a change set on the user's canvas that nobody is going to
// finish.
TEST(ChangeSets, ReplacesAConnectionsPreviousChangeSet) {
    std::unique_ptr<Fixture> fixture = MakeFixture("replace");
    ASSERT_NE(fixture, nullptr);

    const std::string first  = fixture->store.Begin(1, "First idea", "", "", "claude-ai");
    const std::string second = fixture->store.Begin(1, "Better idea", "", "", "claude-ai");

    EXPECT_EQ(fixture->store.Find(first)->state, core::changesets::ChangeSetState::Discarded);
    EXPECT_EQ(fixture->store.Open().size(), 1u);
    EXPECT_EQ(fixture->store.OpenFor(1)->id, second);
}

TEST(ChangeSets, RefusesToSubmitAnEmptyChangeSet) {
    std::unique_ptr<Fixture> fixture = MakeFixture("empty");
    ASSERT_NE(fixture, nullptr);

    const std::string id = fixture->store.Begin(1, "Nothing yet", "", "", "claude-ai");
    std::string       error;
    EXPECT_FALSE(fixture->store.MarkReady(id, error));
    EXPECT_FALSE(error.empty());
}

TEST(ChangeSets, PreviewsAnEmptyChangeSetAsTheUnchangedCase) {
    std::unique_ptr<Fixture> fixture = MakeFixture("emptydiff");
    ASSERT_NE(fixture, nullptr);

    const std::string id = fixture->store.Begin(1, "Just started", "", "", "claude-ai");

    // The canvas asks for a diff every frame, including in the moment between
    // `begin_change_set` and the first staged operation.
    const core::changesets::ChangeSetDiff diff = core::changesets::ComputeChangeSetDiff(
        *fixture->store.Find(id), fixture->state.loaded_case.value());

    EXPECT_TRUE(diff.success);
    EXPECT_FALSE(diff.touches_anything());
    EXPECT_EQ(diff.preview_model.elements.size(), fixture->state.loaded_case->elements.size());
}

// Staleness detection hangs off the anchor. A change set that touches an
// existing element must name one, or the check at acceptance has nothing to
// compare against.
TEST(ChangeSets, AnchorsItselfToAnElementItTouches) {
    std::unique_ptr<Fixture> fixture = MakeFixture("anchor");
    ASSERT_NE(fixture, nullptr);
    const std::string parent = FirstClaimId(fixture->state.loaded_case.value());

    const std::string id = fixture->store.Begin(1, "Add a sub-goal", "", "", "claude-ai");
    std::string       error;
    ASSERT_TRUE(fixture->store.Stage(id, AddSubGoalUnder(parent, "Sub-goal"),
                                     fixture->state.loaded_case.value(), error));

    const core::reviews::ReviewProposal& proposal = fixture->store.Find(id)->proposal;
    EXPECT_EQ(proposal.anchor_element_id, parent);
    EXPECT_TRUE(proposal.base_element_hashes.count(parent));
    EXPECT_FALSE(proposal.base_model_hash.empty());
}

// The reviewer needs to know who is asking, and the audit trail needs it after
// the fact.
TEST(ChangeSets, AttributesItselfToTheConnectedClient) {
    std::unique_ptr<Fixture> fixture = MakeFixture("attribution");
    ASSERT_NE(fixture, nullptr);

    const std::string id = fixture->store.Begin(1, "A change", "", "", "claude-ai 0.1.0");

    EXPECT_EQ(fixture->store.Find(id)->client_label, "claude-ai 0.1.0");
    EXPECT_EQ(fixture->store.Find(id)->proposal.author_name, "MCP: claude-ai 0.1.0");
}
