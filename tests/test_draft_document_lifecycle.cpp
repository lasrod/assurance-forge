#include "app/app_runtime_state.h"

#include "core/app_state.h"
#include "core/drafts/draft_document_store.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

// When a draft document exists at all, and what happens to the accepted
// argument while it does (ADR 0016).
//
// The comparison against the accepted document is only meaningful if the draft
// descends from the argument it is being compared with. A draft cloned for every
// argument anyone opens does not: the moment the user edits the accepted
// argument, every element they add is in the accepted document and absent from
// the draft, which the comparison reports -- correctly, and uselessly -- as the
// draft removing them.

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
        std::filesystem::temp_directory_path() / ("af_draft_lifecycle_" + stem + "_" + std::to_string(++counter));
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
        if (entry.role == core::ProjectFileRole::SacmArgument)
            return state.open_project_file(entry);
    }
    ADD_FAILURE() << "seeded project holds no SACM argument";
    return false;
}

core::SacmElement Strategy(const std::string& id) {
    core::SacmElement element;
    element.id = id;
    element.type = "strategy";
    element.content = "Argue over the identified hazards";
    return element;
}

} // namespace

// Opening an argument is not drafting against it. A draft is unaccepted work,
// and there is none until somebody makes some.
TEST(DraftDocumentLifecycle, OpeningAnArgumentNobodyHasDraftedAgainstCreatesNoDraft) {
    TempDir workspace{UniqueTempPath("no-draft")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));
    ASSERT_NE(state.library_document, nullptr);

    core::drafts::DraftDocumentStore store;
    std::string error;
    ASSERT_TRUE(store.Open(workspace.path, state.loaded_file_path, *state.library_document, error)) << error;

    EXPECT_FALSE(store.active()) << "a draft that has changed nothing is indistinguishable from no draft at all, "
                                    "and one cloned per argument opened goes stale the first time the user edits";
}

// The reported defect, at the level that produced it: the user right-clicked a
// goal and added a strategy, and the banner said the draft removed two things.
TEST(DraftDocumentLifecycle, AnElementTheUserJustAddedIsNotReportedAsADraftRemoval) {
    TempDir workspace{UniqueTempPath("added-not-removed")};
    app::AppRuntimeState state;
    ASSERT_TRUE(OpenProjectWithArgument(state.app_state, workspace.path));
    ASSERT_NE(state.app_state.library_document, nullptr);

    std::string error;
    ASSERT_TRUE(state.draft_document.Open(
        workspace.path, state.app_state.loaded_file_path, *state.app_state.library_document, error))
        << error;

    // Adding a strategy with no draft open goes to the accepted argument, which
    // is what the command bus does today.
    state.app_state.loaded_case->elements.push_back(Strategy("S1"));
    state.app_state.bump_case_revision();

    const core::drafts::DraftDocumentDiff& diff = state.DraftDocumentChanges();
    EXPECT_EQ(diff.removed_count, 0) << "an element the user has just added must never be reported as removed";
    EXPECT_FALSE(diff.touches_anything()) << "editing the accepted argument with no draft open must not raise a "
                                             "working-draft banner over the user's own accepted edit";
}

// A draft that really exists still survives being reopened, and is still not
// re-derived from the accepted argument -- that would silently destroy the
// unaccepted work the file exists to protect.
TEST(DraftDocumentLifecycle, AnExistingDraftIsStillOpenedAndStillNotRederived) {
    TempDir workspace{UniqueTempPath("existing")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));
    ASSERT_NE(state.library_document, nullptr);

    core::drafts::DraftDocumentStore store;
    std::string error;
    ASSERT_TRUE(store.Open(workspace.path, state.loaded_file_path, *state.library_document, error)) << error;
    ASSERT_TRUE(store.EnsureDraft(*state.library_document, error)) << error;
    ASSERT_TRUE(store.active());
    ASSERT_TRUE(store.Save(error)) << error;
    const std::filesystem::path draft_path = store.path();
    ASSERT_TRUE(std::filesystem::exists(draft_path));

    core::drafts::DraftDocumentStore reopened;
    ASSERT_TRUE(reopened.Open(workspace.path, state.loaded_file_path, *state.library_document, error)) << error;
    EXPECT_TRUE(reopened.active()) << "a draft on disk is unaccepted work and must be opened as it was left";
}

// Creating one is idempotent: a second contributor arriving must join the draft
// that is already there rather than replace it with a fresh copy of the
// accepted argument.
TEST(DraftDocumentLifecycle, CreatingADraftThatAlreadyExistsLeavesItAlone) {
    TempDir workspace{UniqueTempPath("idempotent")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));
    ASSERT_NE(state.library_document, nullptr);

    core::drafts::DraftDocumentStore store;
    std::string error;
    ASSERT_TRUE(store.Open(workspace.path, state.loaded_file_path, *state.library_document, error)) << error;
    ASSERT_TRUE(store.EnsureDraft(*state.library_document, error)) << error;
    const std::uint64_t after_create = store.revision();

    ASSERT_TRUE(store.EnsureDraft(*state.library_document, error)) << error;
    EXPECT_EQ(store.revision(), after_create) << "a second EnsureDraft must not replace the draft in progress";
}
