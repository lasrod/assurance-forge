#include "core/changesets/change_set_store.h"

#include "agent/operations.h"
#include "core/app_state.h"
#include "core/assurance_tree.h"
#include "core/project_service.h"
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
    static int counter = 0;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("af_change_sets_" + stem + "_" + std::to_string(++counter));
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

// Support runs upwards: the SOURCE supports the TARGET, so the new sub-goal is
// the source and the goal it develops is the target. This helper had it the
// other way round while calling itself `...Under`, which made every change-set
// test build an argument with the case's top goal hanging beneath a claim the
// test had just invented. Nothing noticed, because the assertions counted
// elements and never looked at the shape.
std::vector<core::reviews::PatchOperation> AddSubGoalUnder(const std::string& parent_id, const std::string& text) {
    const nlohmann::json operations = nlohmann::json::array(
        {nlohmann::json{{"type", "CreateClaim"}, {"create_ref", "$sub"}, {"text", text}},
         nlohmann::json{{"type", "AddSupportedBy"}, {"source", {{"ref", "$sub"}}}, {"target", {{"id", parent_id}}}}});

    std::vector<core::reviews::PatchOperation> parsed;
    std::string error;
    if (!agent::ParsePatchOperations(operations, parsed, error)) {
        ADD_FAILURE() << "could not parse operations: " << error;
    }
    return parsed;
}

struct Fixture {
    TempDir workspace;
    core::AppState state;
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
    const std::string parent = FirstClaimId(before);

    const std::string id = fixture->store.Begin(1, "Add a maintenance sub-goal", "", "", "claude-ai 0.1.0");

    std::string error;
    ASSERT_TRUE(fixture->store.Stage(
        id, AddSubGoalUnder(parent, "Maintenance is adequate"), fixture->state.loaded_case.value(), error))
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
    std::string error;
    ASSERT_TRUE(fixture->store.Stage(
        id, AddSubGoalUnder(parent, "Thermal runaway is mitigated"), fixture->state.loaded_case.value(), error))
        << error;

    const core::changesets::ChangeSetDiff diff =
        core::changesets::ComputeChangeSetDiff(*fixture->store.Find(id), fixture->state.loaded_case.value());

    ASSERT_TRUE(diff.success) << diff.error;
    // A claim and the relationship that attaches it.
    EXPECT_EQ(diff.added_count, 2);
    EXPECT_EQ(diff.removed_count, 0);
    EXPECT_GT(diff.preview_model.elements.size(), fixture->state.loaded_case->elements.size());

    // The preview is a whole assurance case, which is what lets the canvas draw
    // the proposal in place rather than as a list of operations beside it.
    bool found_new_claim = false;
    for (const std::pair<const std::string, core::changesets::ElementChange>& entry : diff.status_by_id) {
        if (entry.second != core::changesets::ElementChange::Added) {
            continue;
        }
        const parser::SacmElement* element = parser::FindElementById(diff.preview_model, entry.first);
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
    std::string error;
    ASSERT_TRUE(
        fixture->store.Stage(id, AddSubGoalUnder(parent, "Sub-goal"), fixture->state.loaded_case.value(), error));

    const core::changesets::ChangeSetDiff diff =
        core::changesets::ComputeChangeSetDiff(*fixture->store.Find(id), fixture->state.loaded_case.value());

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
    std::string error;
    EXPECT_FALSE(fixture->store.Stage(
        id, AddSubGoalUnder("NO-SUCH-ELEMENT", "Orphan"), fixture->state.loaded_case.value(), error));
    EXPECT_FALSE(error.empty());

    // And the change set is unchanged, not half-staged.
    EXPECT_TRUE(fixture->store.Find(id)->proposal.operations.empty());
}

TEST(ChangeSets, LetsAnAgentTakeBackWhatItStaged) {
    std::unique_ptr<Fixture> fixture = MakeFixture("unstage");
    ASSERT_NE(fixture, nullptr);
    const std::string parent = FirstClaimId(fixture->state.loaded_case.value());

    const std::string id = fixture->store.Begin(1, "Reconsider", "", "", "claude-ai");
    std::string error;
    ASSERT_TRUE(
        fixture->store.Stage(id, AddSubGoalUnder(parent, "First thought"), fixture->state.loaded_case.value(), error));
    ASSERT_EQ(fixture->store.Find(id)->proposal.operations.size(), 2u);

    // Responding to "not there" must not mean starting the conversation over.
    ASSERT_TRUE(fixture->store.Unstage(id, 2, error)) << error;
    EXPECT_TRUE(fixture->store.Find(id)->proposal.operations.empty());
}

TEST(ChangeSets, GivesEachConnectionItsOwnChangeSet) {
    std::unique_ptr<Fixture> fixture = MakeFixture("twoclients");
    ASSERT_NE(fixture, nullptr);

    const std::string first = fixture->store.Begin(1, "First client", "", "", "claude-ai");
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

    const std::string first = fixture->store.Begin(1, "First idea", "", "", "claude-ai");
    const std::string second = fixture->store.Begin(1, "Better idea", "", "", "claude-ai");

    EXPECT_EQ(fixture->store.Find(first)->state, core::changesets::ChangeSetState::Discarded);
    EXPECT_EQ(fixture->store.Open().size(), 1u);
    EXPECT_EQ(fixture->store.OpenFor(1)->id, second);
}

// Discarding says what happened rather than "no open change set has that id",
// which reads as the store having lost track of something `list_change_sets`
// had just reported. It had not; the change set had been closed in between.
TEST(ChangeSets, AnswersHonestlyWhenDiscardingSomethingAlreadyClosed) {
    std::unique_ptr<Fixture> fixture = MakeFixture("discardclosed");
    ASSERT_NE(fixture, nullptr);

    const std::string first = fixture->store.Begin(1, "First idea", "", "", "claude-ai");
    // A second `begin` on one connection closes the first. That is the sequence
    // behind the report, and it is the client's own call that causes it.
    const std::string second = fixture->store.Begin(1, "Better idea", "", "", "claude-ai");
    ASSERT_NE(first, second);

    std::string error;
    // Withdrawing something already withdrawn is what the caller wanted.
    EXPECT_TRUE(fixture->store.Discard(first, error)) << error;
    EXPECT_TRUE(error.empty());

    // An id the store has never held is a different thing and still an error.
    EXPECT_FALSE(fixture->store.Discard("proposal-never-existed", error));
    EXPECT_NE(error.find("proposal-never-existed"), std::string::npos) << error;

    // An accepted change is not withdrawable, and the refusal names the remedy.
    ASSERT_TRUE(fixture->store.MarkApplied(second));
    EXPECT_FALSE(fixture->store.Discard(second, error));
    EXPECT_NE(error.find("undo"), std::string::npos) << error;
}

// The agent is told what its own `begin_change_set` threw away, at the moment it
// happens, rather than discovering it later through a refusal.
TEST(ChangeSets, ReportsTheChangeSetANewOneReplaced) {
    std::unique_ptr<Fixture> fixture = MakeFixture("reportsreplaced");
    ASSERT_NE(fixture, nullptr);

    const agent::ChangeContext context{fixture->state, fixture->store, 1, "claude-ai"};
    const agent::Result first = agent::BeginChangeSet(context, nlohmann::json{{"title", "First idea"}});
    ASSERT_FALSE(first.is_error) << first.payload.dump();
    EXPECT_FALSE(first.payload.contains("replaced_change_set"));

    const agent::Result second = agent::BeginChangeSet(context, nlohmann::json{{"title", "Better idea"}});
    ASSERT_FALSE(second.is_error) << second.payload.dump();
    ASSERT_TRUE(second.payload.contains("replaced_change_set")) << second.payload.dump();
    EXPECT_EQ(second.payload["replaced_change_set"]["change_set_id"], first.payload["change_set_id"]);
    EXPECT_EQ(second.payload["replaced_change_set"]["title"], "First idea");
}

TEST(ChangeSets, RefusesToSubmitAnEmptyChangeSet) {
    std::unique_ptr<Fixture> fixture = MakeFixture("empty");
    ASSERT_NE(fixture, nullptr);

    const std::string id = fixture->store.Begin(1, "Nothing yet", "", "", "claude-ai");
    std::string error;
    EXPECT_FALSE(fixture->store.MarkReady(id, error));
    EXPECT_FALSE(error.empty());
}

TEST(ChangeSets, PreviewsAnEmptyChangeSetAsTheUnchangedCase) {
    std::unique_ptr<Fixture> fixture = MakeFixture("emptydiff");
    ASSERT_NE(fixture, nullptr);

    const std::string id = fixture->store.Begin(1, "Just started", "", "", "claude-ai");

    // The canvas asks for a diff every frame, including in the moment between
    // `begin_change_set` and the first staged operation.
    const core::changesets::ChangeSetDiff diff =
        core::changesets::ComputeChangeSetDiff(*fixture->store.Find(id), fixture->state.loaded_case.value());

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
    std::string error;
    ASSERT_TRUE(
        fixture->store.Stage(id, AddSubGoalUnder(parent, "Sub-goal"), fixture->state.loaded_case.value(), error));

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

// Staging is deliberately not a model mutation, so nothing it does marks the
// application's derived views dirty. Something still has to tell the canvas that
// the picture changed, and this revision is it.
//
// Reported: an agent staged operations and the app showed nothing at all. The
// preview only appeared after clicking to another argument file and back --
// which is to say, only when something unrelated happened to rebuild the tree.
TEST(ChangeSets, AdvancesARevisionSoTheCanvasKnowsToRedraw) {
    std::unique_ptr<Fixture> fixture = MakeFixture("revision");
    ASSERT_NE(fixture, nullptr);
    const std::string parent = FirstClaimId(fixture->state.loaded_case.value());

    const std::uint64_t at_start = fixture->store.revision();

    const std::string id = fixture->store.Begin(1, "Watch this", "", "", "claude-ai");
    const std::uint64_t after_begin = fixture->store.revision();
    EXPECT_NE(after_begin, at_start) << "beginning a change set must repaint";

    std::string error;
    ASSERT_TRUE(
        fixture->store.Stage(id, AddSubGoalUnder(parent, "A sub-goal"), fixture->state.loaded_case.value(), error))
        << error;
    const std::uint64_t after_stage = fixture->store.revision();
    EXPECT_NE(after_stage, after_begin) << "staging must repaint";

    ASSERT_TRUE(fixture->store.Unstage(id, 1, error)) << error;
    const std::uint64_t after_unstage = fixture->store.revision();
    EXPECT_NE(after_unstage, after_stage) << "unstaging must repaint";

    ASSERT_TRUE(fixture->store.Discard(id, error)) << error;
    EXPECT_NE(fixture->store.revision(), after_unstage) << "discarding must repaint";
}

// Which way support runs.
//
// `AddSupportedBy` names a source and a target, and the published schema said
// only "Relationship source" and "Relationship target". Driving the real server
// against a real case, the direction was guessed wrong on the first attempt and
// the result was the case's top goal drawn as a sub-claim of the strategy that
// was meant to develop it -- an inverted safety argument, from one undocumented
// word. The direction is now stated in the schema and pinned here.
TEST(ChangeSets, PutsTheNewSubGoalUnderTheGoalItDevelops) {
    std::unique_ptr<Fixture> fixture = MakeFixture("direction");
    ASSERT_NE(fixture, nullptr);
    const std::string parent = FirstClaimId(fixture->state.loaded_case.value());

    const std::string id =
        fixture->store.Begin(1, "Develop the top goal", "", "", "claude-ai", fixture->state.loaded_file_path);
    std::string error;
    ASSERT_TRUE(fixture->store.Stage(
        id, AddSubGoalUnder(parent, "Cleaning is safe"), fixture->state.loaded_case.value(), error))
        << error;

    const core::changesets::ChangeSetDiff diff =
        core::changesets::ComputeChangeSetDiff(*fixture->store.Find(id), fixture->state.loaded_case.value());
    ASSERT_TRUE(diff.success) << diff.error;

    const core::AssuranceTree tree = core::AssuranceTree::Build(diff.preview_model, "ja");
    ASSERT_NE(tree.root, nullptr);
    // The argument still has the same top goal. Getting this backwards does not
    // fail to apply -- it applies perfectly, and re-roots the safety case.
    EXPECT_EQ(tree.root->id, parent) << "the change set re-rooted the argument";

    const std::string sub_goal = diff.generated_ids.at("$sub");
    const core::TreeNode* node = core::FindTreeNode(tree, sub_goal);
    ASSERT_NE(node, nullptr);
    ASSERT_NE(node->parent, nullptr);
    EXPECT_EQ(node->parent->id, parent);
}

// An agent refers to what it has already created, by the id it was given.
//
// `stage_operations` answers with `created_element_ids` -- "$topGoal became G3"
// -- precisely so a later call can name it. Reported from live use: a change set
// of eighty operations staged cleanly, drew on the canvas, counted correctly in
// the Review panel, and then refused to accept with "The proposal refers to G3,
// but that element no longer exists."
//
// It never existed in the committed model and was never meant to: the change set
// creates it. Staging checks feasibility by applying the operations in order to a
// scratch copy, where G3 exists by the time it is named; acceptance checked each
// reference against the committed model alone. So the gate was stricter than the
// thing it guards, and the work an agent had done was unacceptable by
// construction the moment it spanned two staging calls.
TEST(ChangeSets, AcceptsAChangeSetThatNamesItsOwnEarlierCreationsById) {
    std::unique_ptr<Fixture> fixture = MakeFixture("selfreference");
    ASSERT_NE(fixture, nullptr);
    const std::string parent = FirstClaimId(fixture->state.loaded_case.value());

    const std::string id =
        fixture->store.Begin(1, "Build it in two passes", "", "", "claude-ai", fixture->state.loaded_file_path);
    std::string error;
    ASSERT_TRUE(fixture->store.Stage(
        id, AddSubGoalUnder(parent, "Hazards are mitigated"), fixture->state.loaded_case.value(), error))
        << error;

    // What the agent is told the first call produced.
    const core::changesets::ChangeSetDiff first =
        core::changesets::ComputeChangeSetDiff(*fixture->store.Find(id), fixture->state.loaded_case.value());
    ASSERT_TRUE(first.success) << first.error;
    ASSERT_TRUE(first.generated_ids.count("$sub"));
    const std::string created = first.generated_ids.at("$sub");

    // A second call that develops it, naming it the way the reply named it.
    const nlohmann::json more = nlohmann::json::array(
        {nlohmann::json{{"type", "CreateSolution"}, {"create_ref", "$evidence"}, {"text", "Hazard analysis report"}},
         nlohmann::json{
             {"type", "AddSupportedBy"}, {"source", {{"ref", "$evidence"}}}, {"target", {{"id", created}}}}});
    std::vector<core::reviews::PatchOperation> parsed;
    ASSERT_TRUE(agent::ParsePatchOperations(more, parsed, error)) << error;
    ASSERT_TRUE(fixture->store.Stage(id, parsed, fixture->state.loaded_case.value(), error)) << error;

    // Staging accepted it, so the canvas is showing it and the reviewer has been
    // offered a decision. Acceptance has to be able to honour that.
    const core::changesets::ChangeSetAcceptability acceptability = core::changesets::EvaluateChangeSetAcceptability(
        *fixture->store.Find(id), fixture->state.loaded_file_path, fixture->state.loaded_case.value());
    EXPECT_TRUE(acceptability.can_accept) << acceptability.reason;
}

// Element ids repeat across a project's arguments -- each is seeded from the
// same template, so each starts with the same top goal. A change set that does
// not record which document it was written against is therefore ambiguous the
// moment a project holds two, and the ambiguity is not academic: reported from
// live use, a change set built against `main2.sacm` decorated `main.sacm`'s G1,
// and Accept did nothing, because the check acceptance runs compared the staged
// element hashes against the wrong document and called it staleness.
TEST(ChangeSets, BelongsToTheArgumentItWasWrittenAgainst) {
    std::unique_ptr<Fixture> fixture = MakeFixture("twoarguments");
    ASSERT_NE(fixture, nullptr);
    const std::filesystem::path first_file = fixture->state.loaded_file_path;
    const parser::AssuranceCase first_case = fixture->state.loaded_case.value();
    const std::string shared_id = FirstClaimId(first_case);

    core::ProjectFileEntry second;
    std::string error;
    ASSERT_TRUE(core::ProjectService::AddSacmFile(fixture->state.current_project.value(), "second", second, error))
        << error;
    ASSERT_TRUE(fixture->state.open_project_file(second));
    const std::filesystem::path second_file = fixture->state.loaded_file_path;
    ASSERT_NE(first_file, second_file);

    // The premise. Without colliding ids the operations would simply fail to
    // resolve and the defect could not happen.
    ASSERT_NE(parser::FindElementById(fixture->state.loaded_case.value(), shared_id), nullptr)
        << "the two arguments do not share an id, so this fixture proves nothing";

    const std::string id = fixture->store.Begin(1, "Argue about the second file", "", "", "claude-ai", second_file);
    ASSERT_TRUE(fixture->store.Stage(
        id, AddSubGoalUnder(shared_id, "Only true of the second file"), fixture->state.loaded_case.value(), error))
        << error;

    EXPECT_TRUE(core::changesets::ChangeSetTargetsArgumentFile(*fixture->store.Find(id), second_file));
    EXPECT_FALSE(core::changesets::ChangeSetTargetsArgumentFile(*fixture->store.Find(id), first_file));

    // Against the argument it was written for, it accepts.
    const core::changesets::ChangeSetAcceptability here = core::changesets::EvaluateChangeSetAcceptability(
        *fixture->store.Find(id), second_file, fixture->state.loaded_case.value());
    EXPECT_TRUE(here.can_accept) << here.reason;

    // Against the other one it does not -- and says so as "a different argument
    // is open", not as staleness. The remedies differ: open that argument,
    // versus ask the agent to rebuild against an argument that has moved.
    const core::changesets::ChangeSetAcceptability there =
        core::changesets::EvaluateChangeSetAcceptability(*fixture->store.Find(id), first_file, first_case);
    EXPECT_FALSE(there.can_accept);
    EXPECT_TRUE(there.wrong_argument_file);
    EXPECT_NE(there.reason.find(second_file.filename().generic_string()), std::string::npos) << there.reason;
}

// The agent's half of the same rule. An agent that stages against whichever
// argument happens to be open would build a change set out of two documents.
TEST(ChangeSets, RefusesToStageAgainstADifferentArgumentThanItStarted) {
    std::unique_ptr<Fixture> fixture = MakeFixture("stagewrongfile");
    ASSERT_NE(fixture, nullptr);
    const std::string shared_id = FirstClaimId(fixture->state.loaded_case.value());

    core::ProjectFileEntry second;
    std::string error;
    ASSERT_TRUE(core::ProjectService::AddSacmFile(fixture->state.current_project.value(), "second", second, error))
        << error;
    ASSERT_TRUE(fixture->state.open_project_file(second));

    const agent::ChangeContext context{fixture->state, fixture->store, 1, "claude-ai"};
    const agent::Result begun =
        agent::BeginChangeSet(context, nlohmann::json{{"title", "Argue about the second file"}});
    ASSERT_FALSE(begun.is_error) << begun.payload.dump();
    // Which argument it belongs to is reported, so the refusal below is not a
    // surprise to the agent that gets it.
    EXPECT_EQ(begun.payload["argument_file"], second.relativePath.filename().generic_string());

    // The user clicks back to the first argument while the agent is working.
    for (const core::ProjectFileEntry& entry : fixture->state.current_project->files) {
        if (entry.role == core::ProjectFileRole::SacmArgument && entry.relativePath != second.relativePath) {
            ASSERT_TRUE(fixture->state.open_project_file(entry));
        }
    }

    const nlohmann::json operations = nlohmann::json::array(
        {nlohmann::json{{"type", "CreateClaim"}, {"create_ref", "$sub"}, {"text", "Wrong file"}},
         nlohmann::json{{"type", "AddSupportedBy"}, {"source", {{"id", shared_id}}}, {"target", {{"ref", "$sub"}}}}});
    const agent::Result staged = agent::StageOperations(context, nlohmann::json{{"operations", operations}});

    // The ids resolve in this document too, so without the check the operations
    // would apply cleanly to the wrong argument.
    EXPECT_TRUE(staged.is_error) << staged.payload.dump();
    EXPECT_NE(staged.payload["error"].get<std::string>().find("open_case_file"), std::string::npos)
        << staged.payload.dump();
    EXPECT_TRUE(fixture->store.Find(begun.payload["change_set_id"].get<std::string>())->proposal.operations.empty());
}

// Reading must not advance it, or the canvas rebuilds every frame a panel
// happens to look at the store -- which for a large case is a visible stall.
TEST(ChangeSets, DoesNotAdvanceTheRevisionOnReads) {
    std::unique_ptr<Fixture> fixture = MakeFixture("revisionreads");
    ASSERT_NE(fixture, nullptr);

    const std::string id = fixture->store.Begin(1, "Something", "", "", "claude-ai");
    const std::uint64_t settled = fixture->store.revision();

    (void)fixture->store.Open();
    (void)fixture->store.Find(id);
    (void)fixture->store.OpenFor(1);
    (void)fixture->store.has_open();

    EXPECT_EQ(fixture->store.revision(), settled);
}
