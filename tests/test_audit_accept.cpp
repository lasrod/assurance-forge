#include "core/audit/audit_accept.h"

#include "core/app_state.h"
#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_snapshot.h"
#include "core/audit/audit_store.h"
#include "core/audit/event_replayer.h"
#include "core/audit/event_store.h"
#include "core/audit/history_reconstruction.h"
#include "core/audit/replay_verifier.h"
#include "core/audit/undo_boundary.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/element_factory.h"
#include "core/project_model.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

// Recording a whole-document accept in the audit log (ADR 0016, #409).
//
// Reported from a real project: everything an AI client contributed was in the
// `.sacm` and absent from the log, so the next open raised "Audit log divergence
// detected" over work a human had approved, and the only remedy offered threw
// the history away. The properties under test are that a recorded accept is not
// a divergence, that the log can reproduce the accepted document without the
// draft it consumed, and that work after the accept continues the same chain.

namespace {

constexpr const char* kSampleSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

// What an accepted draft leaves on disk: the same argument plus the glossary an
// AI client wrote. No command produced this; the file was replaced.
constexpr const char* kAcceptedSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <terminologyPackage id="TP1" name="Terminology">
    <term id="T1" value="hazard" description="A system state that, with environmental conditions, could lead to harm."/>
  </terminologyPackage>
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

void WriteFile(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

struct AcceptFixture {
    std::filesystem::path root;
    std::filesystem::path sacm_relative = "argument.sacm";
    core::AssuranceProject project;
    std::unique_ptr<core::commands::CommandBus> bus;

    std::filesystem::path sacm_absolute() const {
        return root / sacm_relative;
    }

    ~AcceptFixture() {
        bus.reset();
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

// A project with an initialized audit store and an open command bus -- the
// state the application is in when the user presses Accept.
std::unique_ptr<AcceptFixture> OpenFixture(const std::string& tag) {
    auto fixture = std::make_unique<AcceptFixture>();
    fixture->root =
        std::filesystem::temp_directory_path() /
        ("af_audit_accept_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(fixture->root);
    std::filesystem::create_directories(fixture->root);
    WriteFile(fixture->sacm_absolute(), kSampleSacm);

    fixture->project.id = "p";
    fixture->project.name = "Project";
    fixture->project.rootPath = fixture->root;
    core::ProjectFileEntry entry;
    entry.id = "f1";
    entry.relativePath = fixture->sacm_relative;
    entry.role = core::ProjectFileRole::SacmArgument;
    fixture->project.files.push_back(entry);

    core::audit::EnsureAuditStoreResult ensure;
    std::string error;
    EXPECT_TRUE(core::audit::EnsureAuditStore(fixture->project, fixture->sacm_relative, ensure, error)) << error;

    fixture->bus = core::commands::CommandBus::Open(fixture->project, fixture->sacm_absolute(), error);
    EXPECT_NE(fixture->bus, nullptr) << error;
    return fixture;
}

core::audit::DraftPromotionRecord Provenance() {
    core::audit::DraftPromotionRecord provenance;
    provenance.group_ids = {"group-1"};
    provenance.source_labels = {"MCP: claude-ai"};
    provenance.guideline_ids = {"CL.5"};
    provenance.rationales = {"Bound the safety vocabulary: the claims relied on undefined terms."};
    return provenance;
}

// The accept as the application performs it -- the file replaced wholesale --
// followed by the recording under test.
core::audit::RecordAcceptedDocumentResult AcceptAndRecord(AcceptFixture& fixture) {
    WriteFile(fixture.sacm_absolute(), kAcceptedSacm);
    core::audit::RecordAcceptedDocumentResult result;
    std::string error;
    EXPECT_TRUE(fixture.bus->RecordAcceptedDocument("alice", Provenance(), result, error)) << error;
    EXPECT_TRUE(result.warning.empty()) << result.warning;
    return result;
}

std::string FirstDiagnostic(const core::audit::ReplayVerificationResult& verification) {
    return verification.diagnostics.empty() ? std::string() : verification.diagnostics.front();
}

} // namespace

// The defect, then the fix, in one test: the file an accept writes is a
// divergence until the accept is recorded, and is not one afterwards. The first
// half is what proves the second half is doing the work.
TEST(AuditAccept, AnUnrecordedAcceptIsADivergenceAndARecordedOneIsNot) {
    std::unique_ptr<AcceptFixture> fixture = OpenFixture("divergence");
    WriteFile(fixture->sacm_absolute(), kAcceptedSacm);

    const core::audit::ReplayVerificationResult before = core::audit::VerifyProject(fixture->project);
    ASSERT_TRUE(before.ran);
    ASSERT_FALSE(before.success) << "an accepted draft the log knows nothing about must read as a divergence, or "
                                    "this test cannot tell whether recording it did anything";
    EXPECT_EQ(before.cause, core::audit::DivergenceCause::ReplayDoesNotMatchOnDisk);

    core::audit::RecordAcceptedDocumentResult result;
    std::string error;
    ASSERT_TRUE(fixture->bus->RecordAcceptedDocument("alice", Provenance(), result, error)) << error;
    EXPECT_TRUE(result.warning.empty()) << result.warning;

    const core::audit::ReplayVerificationResult after = core::audit::VerifyProject(fixture->project);
    EXPECT_TRUE(after.success) << FirstDiagnostic(after);
    EXPECT_EQ(after.cause, core::audit::DivergenceCause::None);
}

TEST(AuditAccept, TheAcceptBecomesTheTrustedReplayRoot) {
    std::unique_ptr<AcceptFixture> fixture = OpenFixture("root");
    const core::audit::RecordAcceptedDocumentResult result = AcceptAndRecord(*fixture);

    EXPECT_EQ(result.transaction_sequence, 1u);
    EXPECT_EQ(result.snapshot_id, "snapshot_000001");
    EXPECT_FALSE(result.canonical_model_hash.empty());

    core::audit::AuditManifest manifest;
    std::string error;
    ASSERT_TRUE(core::audit::ReadAuditManifest(fixture->root, manifest, error)) << error;
    EXPECT_EQ(manifest.replay_root_snapshot_id, result.snapshot_id);
    EXPECT_EQ(manifest.latest_transaction_sequence, 1u);
    EXPECT_EQ(manifest.last_known_canonical_model_hash, result.canonical_model_hash);
    EXPECT_EQ(manifest.last_known_raw_file_hash, result.raw_file_hash);

    core::audit::SnapshotMetadata snapshot;
    ASSERT_TRUE(core::audit::ReadSnapshotMetadata(fixture->root, result.snapshot_id, snapshot, error)) << error;
    EXPECT_EQ(snapshot.transaction_sequence, 1u);
    EXPECT_EQ(snapshot.raw_file_hash, result.raw_file_hash);
    EXPECT_TRUE(std::filesystem::exists(core::audit::SnapshotSacmPath(fixture->root, result.snapshot_id)));

    // The root is what the verifier replays from, so it has to resolve to the
    // accept's own sequence rather than fall back to snapshot 0.
    const core::audit::ReplayRoot root =
        core::audit::ResolveReplayRoot(fixture->root, manifest.replay_root_snapshot_id, manifest.initial_snapshot_id);
    EXPECT_EQ(root.snapshot_id, result.snapshot_id);
    EXPECT_EQ(root.from_transaction_sequence, 1u);
}

// What an assessor reads a year later: which command, who approved it, which
// sources contributed and why -- and the document itself, because the draft
// that held the individual changes was consumed by the accept.
TEST(AuditAccept, TheTransactionNamesTheAcceptAndCarriesTheDocumentAndProvenance) {
    std::unique_ptr<AcceptFixture> fixture = OpenFixture("transaction");
    AcceptAndRecord(*fixture);

    const std::vector<core::audit::AuditTransaction>& transactions = fixture->bus->Store().Transactions();
    ASSERT_EQ(transactions.size(), 1u);
    const core::audit::AuditTransaction& tx = transactions.front();
    EXPECT_EQ(tx.command_name, core::audit::kAcceptWorkingDraftCommandName);
    EXPECT_EQ(tx.author, "alice");
    ASSERT_EQ(tx.events.size(), 1u);
    EXPECT_EQ(tx.events.front().event_type, core::audit::kWorkingDraftAcceptedEventType);
    EXPECT_EQ(core::audit::AcceptedDocumentFromEvent(tx.events.front()), kAcceptedSacm);
    EXPECT_EQ(tx.events.front().payload.value("sacm_path", std::string()), "argument.sacm");

    const core::audit::DraftPromotionRecord expected = Provenance();
    EXPECT_EQ(tx.draft_promotion.group_ids, expected.group_ids);
    EXPECT_EQ(tx.draft_promotion.source_labels, expected.source_labels);
    EXPECT_EQ(tx.draft_promotion.guideline_ids, expected.guideline_ids);
    EXPECT_EQ(tx.draft_promotion.rationales, expected.rationales);
}

// The history slider replays from snapshot 0 across every transaction, so the
// accept has to be replayable in its own right: before it there is no glossary,
// after it there is the one the accepted document holds.
TEST(AuditAccept, HistoryReconstructsTheStateOnEitherSideOfTheAccept) {
    std::unique_ptr<AcceptFixture> fixture = OpenFixture("history");
    const core::audit::RecordAcceptedDocumentResult result = AcceptAndRecord(*fixture);

    std::expected<core::audit::ReconstructedState, std::string> before =
        core::audit::ReconstructAtSequence(fixture->project, 0);
    ASSERT_TRUE(before.has_value()) << before.error();
    EXPECT_TRUE(before->views.package.terminologyPackages.empty());

    std::expected<core::audit::ReconstructedState, std::string> after =
        core::audit::ReconstructAtSequence(fixture->project, result.transaction_sequence);
    ASSERT_TRUE(after.has_value()) << after.error();
    ASSERT_EQ(after->views.package.terminologyPackages.size(), 1u);
    ASSERT_EQ(after->views.package.terminologyPackages.front().terms.size(), 1u);
    EXPECT_EQ(after->views.package.terminologyPackages.front().terms.front().value, "hazard");
}

// The accept is one transaction in a chain that goes on. An audited edit made
// afterwards has to append to the same chain, replay on top of the accepted
// document, and leave the project verifiable -- which is exactly the state the
// reporting user's project could not reach.
TEST(AuditAccept, LaterCommandsContinueTheChainFromTheAcceptedDocument) {
    std::unique_ptr<AcceptFixture> fixture = OpenFixture("chain");
    AcceptAndRecord(*fixture);

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture->sacm_absolute().string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);
    core::commands::CommandContext ctx{
        state.loaded_case.value(), state.sacm_package.value(), state.library_document.get()};
    core::commands::UpdateElementTextCommand edit(
        "G1", core::ElementTextField::Content, "en", "The system is acceptably safe.");
    const core::commands::CommandResult edited = fixture->bus->Execute(edit, ctx, "alice");
    ASSERT_TRUE(edited.success) << edited.error;
    EXPECT_EQ(edited.transaction_sequence, 2u);

    // Reopening verifies the hash chain from the first line; a stale previous
    // hash on the edit would fail here.
    std::string error;
    std::unique_ptr<core::audit::EventStore> reopened = core::audit::EventStore::Open(fixture->root, error);
    ASSERT_NE(reopened, nullptr) << error;
    EXPECT_EQ(reopened->Transactions().size(), 2u);

    const core::audit::ReplayVerificationResult verification = core::audit::VerifyProject(fixture->project);
    EXPECT_TRUE(verification.success) << FirstDiagnostic(verification);

    std::expected<core::audit::ReconstructedState, std::string> latest =
        core::audit::ReconstructAtSequence(fixture->project, 2);
    ASSERT_TRUE(latest.has_value()) << latest.error();
    ASSERT_EQ(latest->views.package.terminologyPackages.size(), 1u) << "the edit replayed on top of the accept";
}

// The draft the accept consumed is gone, so there is nothing an undo of the
// accept could put back. The accept's snapshot makes it the boundary Ctrl+Z
// stops at, the way a user snapshot or a baseline already does.
TEST(AuditAccept, TheAcceptIsAnUndoBoundary) {
    std::unique_ptr<AcceptFixture> fixture = OpenFixture("undo");
    const core::audit::RecordAcceptedDocumentResult result = AcceptAndRecord(*fixture);

    std::vector<core::audit::SnapshotMetadata> snapshots(2);
    std::string error;
    ASSERT_TRUE(core::audit::ReadSnapshotMetadata(fixture->root, core::audit::kInitialSnapshotId, snapshots[0], error))
        << error;
    ASSERT_TRUE(core::audit::ReadSnapshotMetadata(fixture->root, result.snapshot_id, snapshots[1], error)) << error;

    const std::uint64_t boundary = core::audit::FindUndoBoundary(snapshots, {}, result.transaction_sequence);
    EXPECT_EQ(boundary, result.transaction_sequence);
    EXPECT_FALSE(core::audit::CanUndo(result.transaction_sequence, boundary));
}

// A malformed accept event must stop the replay with a location, not apply as
// a no-op and let every later event build on a document that never existed.
TEST(AuditAccept, AReplayedAcceptWithoutADocumentIsRefused) {
    std::unique_ptr<AcceptFixture> fixture = OpenFixture("malformed");
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(fixture->sacm_absolute());
    ASSERT_TRUE(loaded.ok);

    core::audit::AuditEvent event;
    event.event_type = core::audit::kWorkingDraftAcceptedEventType;
    event.event_sequence = 4;
    std::string error;
    EXPECT_FALSE(core::audit::ApplyEventToLibrary(*loaded.document, 7, event, error));
    EXPECT_NE(error.find("no document"), std::string::npos) << error;
    EXPECT_NE(error.find("transaction 7"), std::string::npos) << error;
}
