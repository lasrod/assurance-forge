#include "core/app_state.h"
#include "core/drafts/draft_persistence.h"
#include "core/drafts/draft_workspace_store.h"
#include "core/reviews/review_proposal.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

// The shipped example project that carries a working draft.
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

namespace {

std::filesystem::path ExampleProjectRoot() {
    // Walk up from the test binary until the repository's examples directory
    // appears, so this works from the build tree and from a source checkout.
    std::filesystem::path directory = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        const std::filesystem::path candidate = directory / "examples" / "projects" / "kitchen-blender-draft";
        if (std::filesystem::exists(candidate / "af.proj"))
            return candidate;
        if (!directory.has_parent_path() || directory.parent_path() == directory)
            break;
        directory = directory.parent_path();
    }
    return {};
}

} // namespace

TEST(DraftExampleProject, OpensActiveAndMaterializesWhatItAdvertises) {
    const std::filesystem::path root = ExampleProjectRoot();
    if (root.empty()) {
        GTEST_SKIP() << "examples/projects/kitchen-blender-draft not found from " << std::filesystem::current_path();
    }

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

TEST(DraftExampleProject, LeavesTheAcceptedArgumentUntouched) {
    const std::filesystem::path root = ExampleProjectRoot();
    if (root.empty()) {
        GTEST_SKIP() << "examples/projects/kitchen-blender-draft not found";
    }
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
