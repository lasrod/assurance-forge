#include "core/drafts/draft_document_store.h"

#include "core/derived_views.h"
#include "core/drafts/draft_document_diff.h"
#include "core/project_file_io.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/document_edit.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <system_error>

// The draft's lifetime as a real SACM document (ADR 0016).
//
// The properties under test are the ones the operation-based design could not
// hold: the accepted file does not move until a human accepts; accept is one
// write that cannot half-apply; discard is available in every state; and an
// existing draft is never silently re-derived from the accepted argument, which
// would destroy unaccepted work.

namespace {

struct TempDir {
    std::filesystem::path path;
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

TempDir MakeTempDir(const std::string& stem) {
    static int counter = 0;
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("af_draft_doc_" + stem + "_" + std::to_string(++counter));
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path);
    return TempDir{std::move(path)};
}

// A real document produced by the real writer, rather than a hand-written XMI
// literal: the draft is a copy of whatever the application actually holds, so a
// fixture the application would never produce proves nothing about the copy.
std::unique_ptr<sacm_adapter::LibraryDocument> NewAcceptedDocument(const std::filesystem::path& path,
                                                                   const std::string& case_name) {
    const sacm_adapter::SaveOutcome seed = sacm_adapter::new_case_document_xmi(case_name);
    if (!seed.ok)
        return nullptr;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (!core::WriteTextFileAtomic(path, seed.xml).has_value())
        return nullptr;
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    if (!loaded.ok)
        return nullptr;
    return std::move(loaded.document);
}

// The id of the first claim in a projection, which is the element the seed
// document is built around and the one these tests edit.
std::string FirstClaimId(const core::AssuranceCase& model) {
    for (const core::SacmElement& element : model.elements) {
        if (element.type == "claim")
            return element.id;
    }
    return {};
}

std::string ReadFile(const std::filesystem::path& path) {
    const std::expected<std::string, std::string> content = core::ReadTextFile(path);
    return content.has_value() ? content.value() : std::string{};
}

} // namespace

TEST(DraftDocumentStoreTest, ANewDraftIsACopyOfTheAcceptedArgumentAndChangesNothing) {
    const TempDir root = MakeTempDir("fresh");
    const std::filesystem::path argument = root.path / "arguments" / "main.sacm";
    const std::unique_ptr<sacm_adapter::LibraryDocument> accepted = NewAcceptedDocument(argument, "Kettle");
    ASSERT_NE(accepted, nullptr);

    core::drafts::DraftDocumentStore store;
    std::string error;
    ASSERT_TRUE(store.Open(root.path, argument, *accepted, error)) << error;
    ASSERT_TRUE(store.EnsureDraft(*accepted, error)) << error;
    ASSERT_TRUE(store.active());

    const core::drafts::DraftDocumentDiff diff =
        core::drafts::DiffAcceptedAgainstDraft(sacm_adapter::project_case(*accepted), store.Projection());
    EXPECT_FALSE(diff.touches_anything()) << "a draft nobody has edited proposes nothing";
}

// Opening a draft must not write one. A draft file is unaccepted safety-argument
// text sitting in the project directory; creating one for every argument a user
// merely opened would put it there without anyone proposing anything.
TEST(DraftDocumentStoreTest, OpeningADraftDoesNotWriteAFileUntilItIsSaved) {
    const TempDir root = MakeTempDir("nofile");
    const std::filesystem::path argument = root.path / "arguments" / "main.sacm";
    const std::unique_ptr<sacm_adapter::LibraryDocument> accepted = NewAcceptedDocument(argument, "Kettle");
    ASSERT_NE(accepted, nullptr);

    core::drafts::DraftDocumentStore store;
    std::string error;
    ASSERT_TRUE(store.Open(root.path, argument, *accepted, error)) << error;
    ASSERT_TRUE(store.EnsureDraft(*accepted, error)) << error;

    EXPECT_FALSE(std::filesystem::exists(store.path()));
    ASSERT_TRUE(store.Save(error)) << error;
    EXPECT_TRUE(std::filesystem::exists(store.path()));
}

TEST(DraftDocumentStoreTest, AnEditToTheDraftLeavesTheAcceptedFileByteIdentical) {
    const TempDir root = MakeTempDir("bytestable");
    const std::filesystem::path argument = root.path / "arguments" / "main.sacm";
    const std::unique_ptr<sacm_adapter::LibraryDocument> accepted = NewAcceptedDocument(argument, "Kettle");
    ASSERT_NE(accepted, nullptr);
    const std::string before = ReadFile(argument);
    ASSERT_FALSE(before.empty());

    core::drafts::DraftDocumentStore store;
    std::string error;
    ASSERT_TRUE(store.Open(root.path, argument, *accepted, error)) << error;
    ASSERT_TRUE(store.EnsureDraft(*accepted, error)) << error;

    const std::string claim_id = FirstClaimId(store.Projection());
    ASSERT_FALSE(claim_id.empty());
    const sacm_adapter::EditOutcome edited = sacm_adapter::apply_text_edit(
        *store.document(), claim_id, sacm_adapter::TextField::Content, "en", "The kettle is acceptably safe.");
    ASSERT_TRUE(edited.supported && edited.applied);
    ASSERT_TRUE(store.Save(error)) << error;

    EXPECT_EQ(ReadFile(argument), before) << "drafting must not move the accepted argument by a single byte";
}

TEST(DraftDocumentStoreTest, AnEditShowsUpAsExactlyOneModifiedElement) {
    const TempDir root = MakeTempDir("diff");
    const std::filesystem::path argument = root.path / "arguments" / "main.sacm";
    const std::unique_ptr<sacm_adapter::LibraryDocument> accepted = NewAcceptedDocument(argument, "Kettle");
    ASSERT_NE(accepted, nullptr);

    core::drafts::DraftDocumentStore store;
    std::string error;
    ASSERT_TRUE(store.Open(root.path, argument, *accepted, error)) << error;
    ASSERT_TRUE(store.EnsureDraft(*accepted, error)) << error;

    const std::string claim_id = FirstClaimId(store.Projection());
    ASSERT_FALSE(claim_id.empty());
    ASSERT_TRUE(sacm_adapter::apply_text_edit(
                    *store.document(), claim_id, sacm_adapter::TextField::Content, "en", "Reworded by a contributor.")
                    .applied);

    const core::drafts::DraftDocumentDiff diff =
        core::drafts::DiffAcceptedAgainstDraft(sacm_adapter::project_case(*accepted), store.Projection());
    EXPECT_EQ(diff.modified_count, 1);
    const core::drafts::DraftDocumentChange* change = diff.Find(claim_id);
    ASSERT_NE(change, nullptr);
    EXPECT_EQ(change->change, core::drafts::DraftElementChange::Modified);
}

// Reopening must load the draft as it was left. Re-deriving it from the accepted
// document would be simpler and would silently destroy every unaccepted change,
// which is the one thing a recovery file exists to prevent.
TEST(DraftDocumentStoreTest, ReopeningRestoresTheDraftRatherThanRederivingIt) {
    const TempDir root = MakeTempDir("reopen");
    const std::filesystem::path argument = root.path / "arguments" / "main.sacm";
    const std::unique_ptr<sacm_adapter::LibraryDocument> accepted = NewAcceptedDocument(argument, "Kettle");
    ASSERT_NE(accepted, nullptr);

    std::string claim_id;
    {
        core::drafts::DraftDocumentStore store;
        std::string error;
        ASSERT_TRUE(store.Open(root.path, argument, *accepted, error)) << error;
        ASSERT_TRUE(store.EnsureDraft(*accepted, error)) << error;
        claim_id = FirstClaimId(store.Projection());
        ASSERT_FALSE(claim_id.empty());
        ASSERT_TRUE(sacm_adapter::apply_text_edit(
                        *store.document(), claim_id, sacm_adapter::TextField::Content, "en", "Survives a restart.")
                        .applied);
        ASSERT_TRUE(store.Save(error)) << error;
    }

    core::drafts::DraftDocumentStore reopened;
    std::string error;
    ASSERT_TRUE(reopened.Open(root.path, argument, *accepted, error)) << error;

    const core::AssuranceCase projection = reopened.Projection();
    const core::drafts::DraftDocumentDiff diff =
        core::drafts::DiffAcceptedAgainstDraft(sacm_adapter::project_case(*accepted), projection);
    EXPECT_EQ(diff.modified_count, 1) << "the unaccepted edit must survive a reopen";
    for (const core::SacmElement& element : projection.elements) {
        // Braced: a gtest assertion expands to an if/else, so a braceless `if`
        // around one is a dangling else that GCC rejects under -Werror.
        if (element.id == claim_id) {
            EXPECT_EQ(element.content, "Survives a restart.");
        }
    }
}

TEST(DraftDocumentStoreTest, AcceptReplacesTheAcceptedArgumentAndClearsTheDraft) {
    const TempDir root = MakeTempDir("accept");
    const std::filesystem::path argument = root.path / "arguments" / "main.sacm";
    const std::unique_ptr<sacm_adapter::LibraryDocument> accepted = NewAcceptedDocument(argument, "Kettle");
    ASSERT_NE(accepted, nullptr);

    core::drafts::DraftDocumentStore store;
    std::string error;
    ASSERT_TRUE(store.Open(root.path, argument, *accepted, error)) << error;
    ASSERT_TRUE(store.EnsureDraft(*accepted, error)) << error;
    const std::string claim_id = FirstClaimId(store.Projection());
    ASSERT_FALSE(claim_id.empty());
    ASSERT_TRUE(sacm_adapter::apply_text_edit(
                    *store.document(), claim_id, sacm_adapter::TextField::Content, "en", "Accepted wording.")
                    .applied);
    ASSERT_TRUE(store.Save(error)) << error;
    const std::filesystem::path draft_path = store.path();

    ASSERT_TRUE(store.AcceptInto(argument, error)) << error;

    EXPECT_FALSE(store.active()) << "an accepted draft is finished";
    EXPECT_FALSE(std::filesystem::exists(draft_path)) << "and its file is gone";

    sacm_adapter::LoadOutcome reloaded = sacm_adapter::load_document(argument);
    ASSERT_TRUE(reloaded.ok);
    const core::AssuranceCase model = sacm_adapter::project_case(*reloaded.document);
    bool found = false;
    for (const core::SacmElement& element : model.elements) {
        if (element.id == claim_id) {
            EXPECT_EQ(element.content, "Accepted wording.");
            found = true;
        }
    }
    EXPECT_TRUE(found) << "the accepted file must hold what the draft proposed";
}

// Provenance records who proposed a change while it is unaccepted. The accepted
// safety case carries the argument, not the record of who was still proposing
// it, so accept strips every draft tag on the way out.
TEST(DraftDocumentStoreTest, AcceptStripsDraftProvenanceFromTheAcceptedFile) {
    const TempDir root = MakeTempDir("provenance");
    const std::filesystem::path argument = root.path / "arguments" / "main.sacm";
    const std::unique_ptr<sacm_adapter::LibraryDocument> accepted = NewAcceptedDocument(argument, "Kettle");
    ASSERT_NE(accepted, nullptr);

    core::drafts::DraftDocumentStore store;
    std::string error;
    ASSERT_TRUE(store.Open(root.path, argument, *accepted, error)) << error;
    ASSERT_TRUE(store.EnsureDraft(*accepted, error)) << error;
    const std::string claim_id = FirstClaimId(store.Projection());
    ASSERT_FALSE(claim_id.empty());

    const std::string tag_key = std::string(core::drafts::kDraftProvenanceTagPrefix) + "source";
    ASSERT_TRUE(sacm_adapter::apply_set_tagged_value(*store.document(), claim_id, tag_key, "claude-ai 0.1.0").applied);
    ASSERT_TRUE(sacm_adapter::apply_text_edit(
                    *store.document(), claim_id, sacm_adapter::TextField::Content, "en", "Proposed by an agent.")
                    .applied);

    ASSERT_TRUE(store.AcceptInto(argument, error)) << error;

    const std::string written = ReadFile(argument);
    ASSERT_FALSE(written.empty());
    EXPECT_EQ(written.find(core::drafts::kDraftProvenanceTagPrefix), std::string::npos)
        << "no draft provenance may reach the accepted safety case";
    EXPECT_NE(written.find("Proposed by an agent."), std::string::npos) << "the argument itself must survive the strip";
}

// ADR 0016 requires this without exception. The design it replaces could reach a
// state offering neither accept, nor edit, nor discard, whose only exit was
// hand-editing a file.
TEST(DraftDocumentStoreTest, DiscardIsAvailableAndLeavesTheAcceptedArgumentUntouched) {
    const TempDir root = MakeTempDir("discard");
    const std::filesystem::path argument = root.path / "arguments" / "main.sacm";
    const std::unique_ptr<sacm_adapter::LibraryDocument> accepted = NewAcceptedDocument(argument, "Kettle");
    ASSERT_NE(accepted, nullptr);
    const std::string before = ReadFile(argument);

    core::drafts::DraftDocumentStore store;
    std::string error;
    ASSERT_TRUE(store.Open(root.path, argument, *accepted, error)) << error;
    ASSERT_TRUE(store.EnsureDraft(*accepted, error)) << error;
    const std::string claim_id = FirstClaimId(store.Projection());
    ASSERT_TRUE(sacm_adapter::apply_text_edit(
                    *store.document(), claim_id, sacm_adapter::TextField::Content, "en", "Discarded wording.")
                    .applied);
    ASSERT_TRUE(store.Save(error)) << error;
    const std::filesystem::path draft_path = store.path();

    store.Discard(error);
    EXPECT_TRUE(error.empty()) << error;
    EXPECT_FALSE(store.active());
    EXPECT_FALSE(std::filesystem::exists(draft_path));
    EXPECT_EQ(ReadFile(argument), before) << "a discarded draft leaves the accepted file byte-identical";
}

TEST(DraftDocumentStoreTest, DiscardingWhenThereIsNothingToDiscardSucceeds) {
    const TempDir root = MakeTempDir("discardnothing");
    const std::filesystem::path argument = root.path / "arguments" / "main.sacm";
    const std::unique_ptr<sacm_adapter::LibraryDocument> accepted = NewAcceptedDocument(argument, "Kettle");
    ASSERT_NE(accepted, nullptr);

    core::drafts::DraftDocumentStore store;
    std::string error;
    ASSERT_TRUE(store.Open(root.path, argument, *accepted, error)) << error;
    ASSERT_TRUE(store.EnsureDraft(*accepted, error)) << error;

    // Never saved, so there is no file behind it.
    store.Discard(error);
    EXPECT_TRUE(error.empty()) << error;
    EXPECT_FALSE(store.active());
}

// Discard reports no outcome to test, which is the guarantee: there is no `bool`
// for a caller to read as "the draft is still there". A leftover file comes back
// as a note, and the draft is gone from the session regardless.
TEST(DraftDocumentStoreTest, DiscardDropsTheDraftEvenWhenTheFileCannotBeDeleted) {
    const TempDir root = MakeTempDir("discardlocked");
    const std::filesystem::path argument = root.path / "arguments" / "main.sacm";
    const std::unique_ptr<sacm_adapter::LibraryDocument> accepted = NewAcceptedDocument(argument, "Kettle");
    ASSERT_NE(accepted, nullptr);

    core::drafts::DraftDocumentStore store;
    std::string error;
    ASSERT_TRUE(store.Open(root.path, argument, *accepted, error)) << error;
    ASSERT_TRUE(store.EnsureDraft(*accepted, error)) << error;
    ASSERT_TRUE(store.Save(error)) << error;
    const std::filesystem::path draft_path = store.path();

    // Standing in for a file that will not delete: a directory where the draft
    // file was. `remove` refuses a non-empty directory on every platform, which
    // is the failure this asserts we survive rather than a contrived one.
    std::filesystem::remove(draft_path);
    std::filesystem::create_directories(draft_path / "occupied");

    store.Discard(error);
    EXPECT_FALSE(error.empty()) << "the leftover has to be reported to somebody";
    EXPECT_FALSE(store.active()) << "but the draft is gone from this session either way";
}

// Two arguments in one project legitimately reuse ids such as `G1`. Their drafts
// must not be able to reach each other.
TEST(DraftDocumentStoreTest, EachArgumentGetsItsOwnDraft) {
    const TempDir root = MakeTempDir("perargument");
    const std::filesystem::path first = root.path / "arguments" / "main.sacm";
    const std::filesystem::path second = root.path / "arguments" / "subsystem.sacm";

    EXPECT_NE(core::drafts::DraftDocumentPath(root.path, first), core::drafts::DraftDocumentPath(root.path, second));
}

TEST(DraftDocumentStoreTest, AcceptingADraftThatChangedNothingIsHarmless) {
    const TempDir root = MakeTempDir("noop");
    const std::filesystem::path argument = root.path / "arguments" / "main.sacm";
    const std::unique_ptr<sacm_adapter::LibraryDocument> accepted = NewAcceptedDocument(argument, "Kettle");
    ASSERT_NE(accepted, nullptr);

    core::drafts::DraftDocumentStore store;
    std::string error;
    ASSERT_TRUE(store.Open(root.path, argument, *accepted, error)) << error;
    ASSERT_TRUE(store.EnsureDraft(*accepted, error)) << error;
    ASSERT_TRUE(store.AcceptInto(argument, error)) << error;

    sacm_adapter::LoadOutcome reloaded = sacm_adapter::load_document(argument);
    EXPECT_TRUE(reloaded.ok) << "the accepted argument must still load after an empty accept";
    EXPECT_FALSE(store.active());
}

// The draft is drawn on the same canvas as the accepted argument, by the same
// code, so it has to be projected the same way.
//
// It was not: `Projection()` returned a bare `project_case` while the accepted
// view went through `RebuildDerivedViewsFromLibrary`. The render passes that
// turn a term into an inline chip carrying its definition never ran on the
// draft, so a term the user had just asked an agent to define showed on their
// canvas as though it had none -- and stayed that way until a restart re-ran
// the passes through `load_file`.
TEST(DraftDocumentStoreTest, TheDraftIsProjectedTheSameWayTheAcceptedArgumentIs) {
    const TempDir root = MakeTempDir("render_passes");
    const std::filesystem::path argument = root.path / "arguments" / "main.sacm";
    const std::unique_ptr<sacm_adapter::LibraryDocument> accepted = NewAcceptedDocument(argument, "Kettle");
    ASSERT_NE(accepted, nullptr);

    // A term with a definition: the case that showed the defect.
    const sacm_adapter::TerminologyCreateOutcome package =
        sacm_adapter::apply_create_terminology_package(*accepted, "Terminology", "");
    ASSERT_TRUE(package.supported && package.applied) << package.diagnostics.size();
    sacm_adapter::TerminologyTermFields fields;
    fields.value = "safe";
    fields.name = "safe";
    fields.description = "No unreasonable risk of harm within the stated ODD.";
    const sacm_adapter::TerminologyCreateOutcome term =
        sacm_adapter::apply_create_terminology_term(*accepted, package.element_id, fields, {}, {});
    ASSERT_TRUE(term.supported && term.applied);

    // Associated with a claim, because that is what creates the ArtifactReference
    // and AssertedContext the render passes act on. A term defined but attached
    // to nothing is drawn nowhere, so it cannot show the defect.
    const std::string claim_id = FirstClaimId(sacm_adapter::project_case(*accepted));
    ASSERT_FALSE(claim_id.empty());
    const sacm_adapter::TerminologyContextOutcome association =
        sacm_adapter::apply_associate_terminology_term(*accepted, claim_id, term.element_id);
    ASSERT_TRUE(association.supported && association.applied);

    core::drafts::DraftDocumentStore store;
    std::string error;
    ASSERT_TRUE(store.Open(root.path, argument, *accepted, error)) << error;
    // Opening an argument no longer creates a draft, so this makes the one the
    // comparison is about. Unedited, it is a copy of the accepted argument.
    ASSERT_TRUE(store.EnsureDraft(*accepted, error)) << error;

    // What the accepted view renders, built the way the application builds it.
    core::AssuranceCase accepted_view;
    sacm::AssuranceCasePackage accepted_package;
    core::RebuildDerivedViewsFromLibrary(*accepted, accepted_view, accepted_package);

    const core::AssuranceCase draft_view = store.Projection();

    // An unedited draft is a copy of the accepted argument, so the two views
    // must agree element for element -- including which terminology references
    // are hidden and what the visible ones display.
    ASSERT_EQ(draft_view.elements.size(), accepted_view.elements.size())
        << "the draft and accepted views disagree about which elements are drawn";
    for (const core::SacmElement& expected : accepted_view.elements) {
        const core::SacmElement* actual = nullptr;
        for (const core::SacmElement& candidate : draft_view.elements) {
            if (candidate.id == expected.id)
                actual = &candidate;
        }
        ASSERT_NE(actual, nullptr) << expected.id << " is drawn in the accepted view and missing from the draft";
        EXPECT_EQ(actual->name, expected.name) << expected.id;
        EXPECT_EQ(actual->content, expected.content) << expected.id;
        EXPECT_EQ(actual->description, expected.description) << expected.id;
    }
}
