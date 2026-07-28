#include "app/areas/workbench_area.h"

#include "agent/operations.h"
#include "app/areas/review_panel_area.h"
#include "core/app_state.h"
#include "core/argument_package_projection.h"
#include "core/changesets/change_set_store.h"
#include "parser/model_utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// What the user sees on the GSN canvas while an agent builds a change set.
//
// This is the seam the live application got wrong, and it got it wrong while
// every unit test around it passed. `RefreshAgentChangePreview` built a correct
// preview. `BuildArgumentPackageProjection` correctly projected a package. The
// Argument Navigator rendered the preview and showed all eighty staged
// elements. And the canvas -- the thing the user actually looks at -- projected
// the committed package and showed none of them, because nobody had joined the
// two up. Each half worked; the seam between them did not exist.
//
// So the assertion here is deliberately end-to-end through the real pieces: a
// real project on disk, a real change set staged through the real agent
// operations, and the case the canvas would draw.

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
                                       ("af_agent_canvas_" + stem + "_" + std::to_string(++counter));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

struct Fixture {
    TempDir                          workspace;
    core::AppState                   state;
    core::changesets::ChangeSetStore store;
};

std::unique_ptr<Fixture> MakeFixture(const std::string& stem) {
    std::unique_ptr<Fixture> fixture(new Fixture{TempDir{UniqueTempPath(stem)}, {}, {}});
    if (!fixture->state.create_empty_project("Project", fixture->workspace.path.string())) {
        ADD_FAILURE() << "could not create project: " << fixture->state.status_message;
        return nullptr;
    }
    for (const core::ProjectFileEntry& entry : fixture->state.current_project->files) {
        if (entry.role == core::ProjectFileRole::SacmArgument) {
            if (!fixture->state.open_project_file(entry)) {
                ADD_FAILURE() << "could not open the seeded argument";
                return nullptr;
            }
            return fixture;
        }
    }
    ADD_FAILURE() << "seeded project holds no SACM argument";
    return nullptr;
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

// A restructure of the shape an agent actually stages: a strategy under the top
// goal, and sub-goals under the strategy.
std::vector<core::reviews::PatchOperation> Restructure(const std::string& top_goal_id, int goals) {
    nlohmann::json operations = nlohmann::json::array(
        {nlohmann::json{{"type", "CreateStrategy"},
                        {"create_ref", "$strategy"},
                        {"text", "Argue over identified hazards"}},
         nlohmann::json{{"type", "AddSupportedBy"},
                        {"source", {{"ref", "$strategy"}}},
                        {"target", {{"id", top_goal_id}}}}});
    for (int index = 0; index < goals; ++index) {
        const std::string ref = "$goal" + std::to_string(index);
        operations.push_back(nlohmann::json{
            {"type", "CreateClaim"},
            {"create_ref", ref},
            {"text", "Hazard " + std::to_string(index) + " is mitigated"}});
        operations.push_back(nlohmann::json{{"type", "AddSupportedBy"},
                                            {"source", {{"ref", ref}}},
                                            {"target", {{"ref", "$strategy"}}}});
    }

    std::vector<core::reviews::PatchOperation> parsed;
    std::string                                error;
    if (!agent::ParsePatchOperations(operations, parsed, error)) {
        ADD_FAILURE() << "could not parse operations: " << error;
    }
    return parsed;
}

// What `AppRuntime::RefreshAgentChangePreview` hands the canvas each frame.
struct Preview {
    parser::AssuranceCase    model;
    std::vector<std::string> added_ids;
};

Preview ComputePreview(const core::changesets::ChangeSet& change_set,
                       const parser::AssuranceCase&       committed) {
    const core::changesets::ChangeSetDiff diff =
        core::changesets::ComputeChangeSetDiff(change_set, committed);
    EXPECT_TRUE(diff.success) << diff.error;

    Preview preview;
    preview.model = diff.preview_model;
    for (const std::pair<const std::string, core::changesets::ElementChange>& entry :
         diff.status_by_id) {
        if (entry.second == core::changesets::ElementChange::Added) {
            preview.added_ids.push_back(entry.first);
        }
    }
    return preview;
}

} // namespace

TEST(AgentChangeCanvas, DrawsWhatTheAgentHasStagedRatherThanTheCommittedArgument) {
    std::unique_ptr<Fixture> fixture = MakeFixture("draws");
    ASSERT_NE(fixture, nullptr);
    ASSERT_TRUE(fixture->state.has_projected_package());
    const std::string top_goal = FirstClaimId(fixture->state.loaded_case.value());

    const std::string id = fixture->store.Begin(1, "Restructure by hazard", "", "", "claude-ai",
                                                fixture->state.loaded_file_path);
    std::string       error;
    ASSERT_TRUE(fixture->store.Stage(id, Restructure(top_goal, 4),
                                     fixture->state.loaded_case.value(), error))
        << error;

    const Preview preview =
        ComputePreview(*fixture->store.Find(id), fixture->state.loaded_case.value());
    ASSERT_FALSE(preview.added_ids.empty());

    const sacm::AssuranceCasePackage& package = fixture->state.projected_package();
    ASSERT_FALSE(package.argumentPackages.empty());
    const parser::AssuranceCase drawn = app::areas::BuildArgumentPackageCanvasCase(
        fixture->state.loaded_case.value(), preview.model, preview.added_ids, package,
        package.argumentPackages.front(), "Argument");

    for (const std::string& added : preview.added_ids) {
        EXPECT_NE(parser::FindElementById(drawn, added), nullptr)
            << added << " was staged and the canvas would not draw it";
    }
    // And the argument it was staged onto is still there, so this is the
    // argument with the proposal in it rather than a diagram of the proposal.
    EXPECT_NE(parser::FindElementById(drawn, top_goal), nullptr);

    // The claim text has to survive too. A node drawn with nothing written on it
    // tells a reviewer no more than an empty canvas did.
    bool found_text = false;
    for (const std::string& added : preview.added_ids) {
        const parser::SacmElement* element = parser::FindElementById(drawn, added);
        if (element != nullptr && element->content == "Hazard 0 is mitigated") {
            found_text = true;
        }
    }
    EXPECT_TRUE(found_text) << "the staged claim reached the canvas with no text on it";
}

// With nothing staged the canvas is what it always was. The preview path must
// not become the only path.
TEST(AgentChangeCanvas, DrawsTheCommittedArgumentWhenNoAgentIsWorking) {
    std::unique_ptr<Fixture> fixture = MakeFixture("committed");
    ASSERT_NE(fixture, nullptr);
    ASSERT_TRUE(fixture->state.has_projected_package());
    const std::string top_goal = FirstClaimId(fixture->state.loaded_case.value());

    const sacm::AssuranceCasePackage& package = fixture->state.projected_package();
    ASSERT_FALSE(package.argumentPackages.empty());
    const parser::AssuranceCase drawn = app::areas::BuildArgumentPackageCanvasCase(
        fixture->state.loaded_case.value(), std::nullopt, {}, package,
        package.argumentPackages.front(), "Argument");

    EXPECT_NE(parser::FindElementById(drawn, top_goal), nullptr);
    EXPECT_EQ(drawn.elements.size(), core::BuildArgumentPackageProjection(
                                         fixture->state.loaded_case.value(),
                                         package.argumentPackages.front(), "Argument")
                                         .elements.size());
}

// The reviewer sees what the agent sees.
//
// SCCG findings have been returned to the agent in every staging result since
// the checks were written, and the Review panel has had a section to render them
// on the change set. Nothing ever filled it: the row's `sccg_findings` was
// default-constructed and the section could not appear. The checks were tested,
// the panel was written, and no test crossed between them -- so the person being
// asked to accept a change to a safety argument was told less about it than the
// agent proposing it was.
TEST(AgentChangeCanvas, ShowsTheReviewerTheSameSccgFindingsTheAgentGot) {
    std::unique_ptr<Fixture> fixture = MakeFixture("findings");
    ASSERT_NE(fixture, nullptr);
    const std::string top_goal = FirstClaimId(fixture->state.loaded_case.value());

    // A claim with no support and no undeveloped marker: EV.1, and the one an
    // agent is most likely to leave behind mid-draft.
    const nlohmann::json operations = nlohmann::json::array(
        {nlohmann::json{{"type", "CreateStrategy"},
                        {"create_ref", "$strategy"},
                        {"text", "Argue over misuse"}},
         nlohmann::json{{"type", "AddSupportedBy"},
                        {"source", {{"ref", "$strategy"}}},
                        {"target", {{"id", top_goal}}}}});
    std::vector<core::reviews::PatchOperation> parsed;
    std::string                                error;
    ASSERT_TRUE(agent::ParsePatchOperations(operations, parsed, error)) << error;

    const std::string id = fixture->store.Begin(1, "Leave a strategy hanging", "", "", "claude-ai",
                                                fixture->state.loaded_file_path);
    ASSERT_TRUE(fixture->store.Stage(id, parsed, fixture->state.loaded_case.value(), error))
        << error;

    const core::changesets::ChangeSetDiff diff = core::changesets::ComputeChangeSetDiff(
        *fixture->store.Find(id), fixture->state.loaded_case.value());
    ASSERT_TRUE(diff.success) << diff.error;

    const std::vector<std::string> described = app::areas::DescribeStagedSccgFindings(diff);
    ASSERT_FALSE(described.empty()) << "the reviewer would see no findings at all";

    // Each line names the guideline, so a reviewer can look the rule up rather
    // than take the tool's word for it.
    bool names_a_guideline = false;
    for (const std::string& line : described) {
        if (line.find("AR.2") != std::string::npos || line.find("EV.1") != std::string::npos) {
            names_a_guideline = true;
        }
    }
    EXPECT_TRUE(names_a_guideline) << described.front();
}
