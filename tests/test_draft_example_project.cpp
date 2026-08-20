#include "core/app_state.h"
#include "core/drafts/draft_persistence.h"
#include "core/drafts/draft_workspace_store.h"
#include "core/drafts/draft_promotion_service.h"
#include "core/project_file_io.h"
#include "core/project_service.h"
#include "core/reviews/review_proposal.h"
#include "core/reviews/review_proposal_patch_service.h"
#include "core/sacm_argument_sync.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

// The example project that carries a working draft.
//
// It exists so the draft workspace can be looked at in the running application
// before MCP and SCCG review write draft groups themselves. Nothing else creates
// one yet, so without this there is nothing to open.
//
// **A fixture that quietly stopped working would be worse than no fixture.** If
// someone edits `arguments/main.sacm`, the stored base hash no longer matches
// and the draft opens in `NeedsRebase` -- correct behaviour, and exactly what a
// demo must not silently do. So this asserts it opens active, materializes, and
// still marks what it claims to mark.
//
// **The fixture lives here, not in the `examples` submodule.** That submodule is
// a place people open projects and try things; opening one writes an autosave, a
// refreshed file hash, a review item. Tests that read it therefore failed
// whenever somebody had used the examples for what they are for -- reporting a
// broken baseline that was really just a colleague's afternoon. Tests own their
// inputs, and `examples/` goes back to being examples.
//
// Each test works on a **copy in a temp directory**, because opening a project
// writes to it. A fixture a test can mutate is a fixture that passes once.

namespace {

std::filesystem::path FixtureProjectsSource() {
    // Walk up from the test binary until the repository's test data appears, so
    // this works from the build tree and from a source checkout.
    std::filesystem::path directory = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        const std::filesystem::path candidate = directory / "tests" / "data" / "projects";
        if (std::filesystem::exists(candidate / "kitchen-blender" / "af.proj"))
            return candidate;
        if (!directory.has_parent_path() || directory.parent_path() == directory)
            break;
        directory = directory.parent_path();
    }
    return {};
}

// A throwaway copy of the fixture projects. Opening a project writes to it --
// an autosave, a refreshed file hash, a review item -- so a test that opened
// the checked-in copy would dirty the working tree and make the next run
// disagree with the last.
//
// The directory is claimed by creating it rather than by picking a name and
// clearing it. CTest runs each case in its own process, so a name derived only
// from the fixture source is the same name in all of them, and clearing it would
// delete the copy another process is in the middle of reading -- reintroducing,
// under `ctest -j`, exactly the shared-state interference this fixture removes.
struct FixtureProjects {
    std::filesystem::path path;
    // Why there is no copy. Absent fixtures are a reason to skip; fixtures that
    // are present and could not be copied are a broken test environment, and
    // skipping on that would report "not found" about files that are right there.
    bool source_found = false;
    std::string error;

    FixtureProjects() {
        const std::filesystem::path source = FixtureProjectsSource();
        if (source.empty())
            return;
        source_found = true;
        const std::string stem = "af_fixture_projects_" + std::to_string(std::filesystem::hash_value(source)) + "_";
        for (int attempt = 0; attempt < 64; ++attempt) {
            const std::filesystem::path candidate =
                std::filesystem::temp_directory_path() / (stem + std::to_string(attempt));
            std::error_code ec;
            if (!std::filesystem::create_directory(candidate, ec) || ec)
                continue; // Taken by another test, or by a run that crashed before cleaning up.
            std::filesystem::copy(source, candidate, std::filesystem::copy_options::recursive, ec);
            if (ec) {
                error = "the fixture projects in " + source.string() + " could not be copied to " + candidate.string() +
                        ": " + ec.message();
                std::filesystem::remove_all(candidate, ec);
                return;
            }
            path = candidate;
            return;
        }
        error = "no free temp directory for a copy of " + source.string() + " after 64 attempts";
    }

    ~FixtureProjects() {
        if (path.empty())
            return;
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    bool empty() const {
        return path.empty();
    }
    bool source_missing() const {
        return !source_found;
    }
    std::filesystem::path project(const std::string& name) const {
        return path / name;
    }
};

} // namespace

TEST(DraftExampleProject, OpensActiveAndMaterializesWhatItAdvertises) {
    const FixtureProjects fixture;
    if (fixture.source_missing()) {
        GTEST_SKIP() << "tests/data/projects not found from " << std::filesystem::current_path();
    }
    // Present but uncopyable is a broken environment, not a reason to skip: a
    // skip here would say "not found" about files that are right there.
    ASSERT_FALSE(fixture.empty()) << fixture.error;
    const std::filesystem::path root = fixture.project("kitchen-blender-draft");

    core::AppState state;
    const std::filesystem::path argument = root / "arguments" / "main.sacm";
    ASSERT_TRUE(state.load_file(argument.string())) << state.status_message;
    ASSERT_TRUE(state.loaded_case.has_value());

    core::drafts::DraftWorkspaceStore store;
    store.SetProjectRoot(root);
    std::string error;
    ASSERT_TRUE(store.Open(argument, state.loaded_case.value(), error)) << error;
    ASSERT_TRUE(store.has_workspace()) << "the example ships a draft under .af/drafts";

    const core::drafts::DraftWorkspace& workspace = *store.workspace();
    // If this fails, `arguments/main.sacm` was edited without the stored base
    // hash being updated. The draft is not wrong -- it is correctly refusing to
    // replay against an argument it was not written for. Recompute the hash with
    // `ComputeModelSemanticHash` and update `workspace.json`.
    EXPECT_EQ(workspace.state, core::drafts::DraftWorkspaceState::Active)
        << "stored base hash: " << workspace.base_model_hash
        << "\ncurrent model hash: " << core::reviews::ComputeModelSemanticHash(state.loaded_case.value());

    EXPECT_EQ(workspace.staged_group_count(), 3u);

    const core::drafts::DraftMaterializationResult& result = store.Materialize(state.loaded_case.value(), 1);
    ASSERT_TRUE(result.success) << result.error << " (group " << result.failing_group_id << ")";

    // What the README tells the reader they will see. Each of these is a
    // different badge on the canvas, and between them they cover every marker
    // the draft renderer draws.
    const core::drafts::DraftElementEntry* added = result.change_index.Find("G7");
    ASSERT_NE(added, nullptr) << "the MCP group's new claim";
    EXPECT_EQ(added->change, core::drafts::DraftElementChange::Added);

    const core::drafts::DraftElementEntry* multiple = result.change_index.Find("G1");
    ASSERT_NE(multiple, nullptr) << "the top goal, reworded by two different sources";
    EXPECT_EQ(multiple->change, core::drafts::DraftElementChange::Modified);
    EXPECT_EQ(result.change_index.ContributingGroupIds("G1").size(), 2u) << "MULTIPLE CHANGES badge";

    const core::drafts::DraftElementEntry* human = result.change_index.Find("C1");
    ASSERT_NE(human, nullptr) << "the human group's edit";
    EXPECT_EQ(human->change, core::drafts::DraftElementChange::Modified);

    // The new support relationship, which is drawn on the edge rather than
    // inferred from the nodes at either end.
    bool has_added_relationship = false;
    for (const std::string& id : result.change_index.ChangedElementIds()) {
        for (const core::SacmElement& element : result.working_model.elements) {
            if (element.id != id || element.type != "assertedinference")
                continue;
            const core::drafts::DraftElementEntry* entry = result.change_index.Find(id);
            if (entry != nullptr && entry->change == core::drafts::DraftElementChange::Added &&
                !element.target_refs.empty() && element.target_refs.front() == "G5") {
                has_added_relationship = true;
            }
        }
    }
    EXPECT_TRUE(has_added_relationship) << "NEW SUPPORT edge from the MCP group's claim to G5";
}

TEST(DraftExampleProject, EveryScenarioKeepsTheAcceptedBlenderBaselineByteIdentical) {
    const FixtureProjects fixture;
    if (fixture.source_missing()) {
        GTEST_SKIP() << "tests/data/projects not found from " << std::filesystem::current_path();
    }
    // Present but uncopyable is a broken environment, not a reason to skip: a
    // skip here would say "not found" about files that are right there.
    ASSERT_FALSE(fixture.empty()) << fixture.error;
    const std::filesystem::path projects = fixture.path;

    const std::expected<std::string, std::string> baseline =
        core::ReadTextFile(projects / "kitchen-blender" / "arguments" / "main.sacm");
    ASSERT_TRUE(baseline.has_value()) << baseline.error();

    for (const std::string scenario : {"kitchen-blender-draft", "kitchen-blender-deletion-draft"}) {
        const std::expected<std::string, std::string> accepted =
            core::ReadTextFile(projects / scenario / "arguments" / "main.sacm");
        ASSERT_TRUE(accepted.has_value()) << scenario << ": " << accepted.error();
        EXPECT_EQ(accepted.value(), baseline.value())
            << scenario << " must carry proposals only in .af/drafts, never in accepted SACM";
    }
}

TEST(DraftExampleProject, EveryBlenderManifestOpensWithoutProjectHealthWarnings) {
    const FixtureProjects fixture;
    if (fixture.source_missing()) {
        GTEST_SKIP() << "tests/data/projects not found from " << std::filesystem::current_path();
    }
    // Present but uncopyable is a broken environment, not a reason to skip: a
    // skip here would say "not found" about files that are right there.
    ASSERT_FALSE(fixture.empty()) << fixture.error;
    const std::filesystem::path projects = fixture.path;

    for (const std::string name : {"kitchen-blender", "kitchen-blender-draft", "kitchen-blender-deletion-draft"}) {
        core::AssuranceProject project;
        core::ProjectLoadReport report;
        std::string error;
        ASSERT_TRUE(core::ProjectService::OpenProject(projects / name / "af.proj", project, report, error))
            << name << ": " << error;
        EXPECT_FALSE(report.has_failures()) << name;
        EXPECT_TRUE(report.warnings.empty()) << name << " carries a stale file hash in af.proj";
        for (const core::ProjectFileEntry& file : project.files)
            EXPECT_EQ(file.state, core::ProjectFileState::Clean) << name << ": " << file.relativePath;
    }
}

TEST(DraftExampleProject, DeletionScenarioMarksTheElementWithoutChangingTheAcceptedBaseline) {
    const FixtureProjects fixture;
    if (fixture.source_missing()) {
        GTEST_SKIP() << "tests/data/projects not found from " << std::filesystem::current_path();
    }
    // Present but uncopyable is a broken environment, not a reason to skip: a
    // skip here would say "not found" about files that are right there.
    ASSERT_FALSE(fixture.empty()) << fixture.error;
    const std::filesystem::path root = fixture.project("kitchen-blender-deletion-draft");

    core::AppState state;
    const std::filesystem::path argument = root / "arguments" / "main.sacm";
    ASSERT_TRUE(state.load_file(argument.string())) << state.status_message;
    ASSERT_TRUE(state.loaded_case.has_value());
    const std::string accepted_hash = core::reviews::ComputeModelSemanticHash(state.loaded_case.value());

    core::drafts::DraftWorkspaceStore store;
    store.SetProjectRoot(root);
    std::string error;
    ASSERT_TRUE(store.Open(argument, state.loaded_case.value(), error)) << error;
    ASSERT_TRUE(store.has_workspace());
    ASSERT_EQ(store.workspace()->state, core::drafts::DraftWorkspaceState::Active);

    const core::drafts::DraftMaterializationResult& result = store.Materialize(state.loaded_case.value(), 1);
    ASSERT_TRUE(result.success) << result.error;
    const core::drafts::DraftElementEntry* deleted = result.change_index.Find("G10");
    ASSERT_NE(deleted, nullptr);
    EXPECT_EQ(deleted->change, core::drafts::DraftElementChange::Removed);

    const auto contains_g10 = [](const core::AssuranceCase& model) {
        return std::any_of(model.elements.begin(), model.elements.end(), [](const core::SacmElement& element) {
            return element.id == "G10";
        });
    };
    EXPECT_TRUE(contains_g10(state.loaded_case.value()));
    EXPECT_FALSE(contains_g10(result.working_model));
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(state.loaded_case.value()), accepted_hash);
}

TEST(DraftExampleProject, LeavesTheAcceptedArgumentUntouched) {
    const FixtureProjects fixture;
    if (fixture.source_missing()) {
        GTEST_SKIP() << "tests/data/projects not found from " << std::filesystem::current_path();
    }
    // Present but uncopyable is a broken environment, not a reason to skip: a
    // skip here would say "not found" about files that are right there.
    ASSERT_FALSE(fixture.empty()) << fixture.error;
    const std::filesystem::path root = fixture.project("kitchen-blender-draft");
    const std::filesystem::path argument = root / "arguments" / "main.sacm";

    core::AppState state;
    ASSERT_TRUE(state.load_file(argument.string())) << state.status_message;
    const std::string before = core::reviews::ComputeModelSemanticHash(state.loaded_case.value());

    core::drafts::DraftWorkspaceStore store;
    store.SetProjectRoot(root);
    std::string error;
    ASSERT_TRUE(store.Open(argument, state.loaded_case.value(), error)) << error;
    ASSERT_TRUE(store.Materialize(state.loaded_case.value(), 1).success);

    // Opening and materializing a draft is not an edit. The example's accepted
    // argument is the same argument afterwards, which is the property the whole
    // design rests on.
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(state.loaded_case.value()), before);
}

// Accepting the whole draft must leave a safety case, not take it away.
//
// Reported from the running application: pressing "Accept all" returned the app
// to the welcome screen. The `.sacm` on disk was byte-identical afterwards, and
// the draft recovery data was gone -- the signature of the promotion reporting
// success and then something downstream throwing, because `RenderFrame` resets
// `loaded_case` when the derived-view rebuild raises. Losing the argument from
// the screen is the worst failure this feature can have, so it is pinned here.
TEST(DraftExampleProject, PromotingTheWholeDraftKeepsTheArgument) {
    const FixtureProjects fixture;
    if (fixture.source_missing()) {
        GTEST_SKIP() << "tests/data/projects not found from " << std::filesystem::current_path();
    }
    // Present but uncopyable is a broken environment, not a reason to skip: a
    // skip here would say "not found" about files that are right there.
    ASSERT_FALSE(fixture.empty()) << fixture.error;
    const std::filesystem::path root = fixture.project("kitchen-blender-draft");
    const std::filesystem::path argument = root / "arguments" / "main.sacm";

    core::AppState state;
    ASSERT_TRUE(state.load_file(argument.string())) << state.status_message;
    ASSERT_TRUE(state.loaded_case.has_value());
    ASSERT_TRUE(state.sacm_package.has_value());
    const std::size_t accepted_elements = state.loaded_case->elements.size();

    core::drafts::DraftWorkspaceStore store;
    store.SetProjectRoot(root);
    std::string error;
    ASSERT_TRUE(store.Open(argument, state.loaded_case.value(), error)) << error;
    ASSERT_TRUE(store.has_workspace());
    ASSERT_TRUE(store.Materialize(state.loaded_case.value(), 1).success);

    const core::drafts::CompiledDraftPromotion compiled =
        core::drafts::CompileWorkspacePromotion(*store.workspace(), "Working draft");
    ASSERT_TRUE(compiled.success) << compiled.error;

    // Exactly what `ApplyProposalCommand` does: apply with the draft's pinned
    // identities, then mirror the mutated parser model into the SACM package the
    // serializer and the canvas read from.
    core::AssuranceCase promoted = state.loaded_case.value();
    const core::reviews::ReviewProposalPatchService service;
    const core::reviews::ApplyProposalResult applied =
        service.ApplyProposalWithIds(compiled.proposal, promoted, compiled.identities);
    ASSERT_TRUE(applied.success) << applied.error;
    EXPECT_GT(promoted.elements.size(), accepted_elements) << "the draft adds a claim and a relationship";

    sacm::AssuranceCasePackage package = state.sacm_package.value();
    core::RebuildSacmArgumentPackageFromParser(promoted, package);

    // The elements the draft proposed have to survive the round trip into the
    // package, or the promoted argument is missing exactly what was accepted.
    std::size_t claims = 0;
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages)
        claims += argument_package.claims.size();
    EXPECT_GT(claims, 0u) << "the package still holds the argument after promotion";
}
