// Where a piece of evidence is.
//
// An ArtifactReference (a GSN Solution) cites its evidence by id, and the
// location -- the path or URL -- lives on the cited Resource (SACM clause
// 12.12). These tests hold the whole path together: the adapter seam that
// writes it, the projection the register reads it from, the audited command
// and its replay, and the draft operation an agent or the register stages
// while a working draft is active.

#include "core/audit/audit_store.h"
#include "core/audit/replay_verifier.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/derived_views.h"
#include "core/drafts/draft_operation_apply.h"
#include "core/project_model.h"
#include "core/reviews/review_proposal.h"
#include "parser/model_utils.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/document_edit.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace {

// One claim resting on one piece of evidence, and no ArtifactPackage at all:
// the state of every argument drawn before anyone recorded where the evidence
// was.
constexpr const char* kSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
    <artifactReference id="Sn1" name="Test report"/>
    <assertedEvidence id="R1"><source ref="Sn1"/><target ref="G1"/></assertedEvidence>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

std::filesystem::path MakeTempRoot(const std::string& tag) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("af_evidence_location_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void WriteFile(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::unique_ptr<sacm_adapter::LibraryDocument> LoadSample(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "argument.sacm";
    WriteFile(path, kSacm);
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    EXPECT_TRUE(loaded.ok);
    return std::move(loaded.document);
}

const core::SacmElement* Find(const core::AssuranceCase& model, const std::string& id) {
    return parser::FindElementById(model, id);
}

int CountElementsOfType(const core::AssuranceCase& model, const std::string& type) {
    int count = 0;
    for (const core::SacmElement& element : model.elements) {
        if (element.type == type)
            ++count;
    }
    return count;
}

TEST(EvidenceLocation, SeamCreatesTheResourceTheFirstTimeAndReusesItAfter) {
    const std::filesystem::path root = MakeTempRoot("seam");
    std::unique_ptr<sacm_adapter::LibraryDocument> document = LoadSample(root);
    ASSERT_NE(document, nullptr);

    const sacm_adapter::EditOutcome first =
        sacm_adapter::apply_set_evidence_location(*document, "Sn1", "  https://example.org/report.pdf ");
    ASSERT_TRUE(first.supported);
    ASSERT_TRUE(first.applied) << (first.diagnostics.empty() ? "" : first.diagnostics.front().message);

    core::AssuranceCase projected = sacm_adapter::project_case(*document);
    const core::SacmElement* evidence = Find(projected, "Sn1");
    ASSERT_NE(evidence, nullptr);
    EXPECT_EQ(evidence->artifact_location, "https://example.org/report.pdf") << "the location was not trimmed";
    EXPECT_FALSE(evidence->referenced_artifact_id.empty()) << "the reference cites nothing";
    EXPECT_EQ(CountElementsOfType(projected, "resource"), 1);

    // A second location updates the same Resource rather than creating another.
    ASSERT_TRUE(sacm_adapter::apply_set_evidence_location(*document, "Sn1", "evidence/report-v2.pdf").applied);
    projected = sacm_adapter::project_case(*document);
    EXPECT_EQ(Find(projected, "Sn1")->artifact_location, "evidence/report-v2.pdf");
    EXPECT_EQ(CountElementsOfType(projected, "resource"), 1) << "a rewrite created a second Resource";

    // The location survives a save, which is what makes it a record rather than
    // a display.
    const sacm_adapter::SaveOutcome saved = sacm_adapter::save_document(*document, /*tolerant=*/false);
    ASSERT_TRUE(saved.ok) << (saved.diagnostics.empty() ? "" : saved.diagnostics.front().message);
    EXPECT_NE(saved.xml.find("evidence/report-v2.pdf"), std::string::npos) << saved.xml;
    WriteFile(root / "saved.sacm", saved.xml);
    sacm_adapter::LoadOutcome reloaded = sacm_adapter::load_document(root / "saved.sacm");
    ASSERT_TRUE(reloaded.ok);
    EXPECT_EQ(Find(sacm_adapter::project_case(*reloaded.document), "Sn1")->artifact_location, "evidence/report-v2.pdf");

    // Clearing empties the location and keeps the Resource; nothing is lost
    // that a later location cannot fill back in.
    ASSERT_TRUE(sacm_adapter::apply_set_evidence_location(*document, "Sn1", "").applied);
    projected = sacm_adapter::project_case(*document);
    EXPECT_TRUE(Find(projected, "Sn1")->artifact_location.empty());
    EXPECT_EQ(CountElementsOfType(projected, "resource"), 1);

    std::filesystem::remove_all(root);
}

TEST(EvidenceLocation, SeamRefusesAnythingThatIsNotAnArtifactReference) {
    const std::filesystem::path root = MakeTempRoot("refuse");
    std::unique_ptr<sacm_adapter::LibraryDocument> document = LoadSample(root);
    ASSERT_NE(document, nullptr);

    const sacm_adapter::EditOutcome on_claim =
        sacm_adapter::apply_set_evidence_location(*document, "G1", "https://example.org");
    EXPECT_TRUE(on_claim.supported);
    EXPECT_FALSE(on_claim.applied);
    ASSERT_FALSE(on_claim.diagnostics.empty());
    EXPECT_EQ(on_claim.diagnostics.front().code, "SACM-CMD-002");

    const sacm_adapter::EditOutcome on_nothing =
        sacm_adapter::apply_set_evidence_location(*document, "missing", "https://example.org");
    EXPECT_FALSE(on_nothing.applied);

    // A clear on a reference that cites nothing is a no-op, not a creation.
    ASSERT_TRUE(sacm_adapter::apply_set_evidence_location(*document, "Sn1", "   ").applied);
    const core::AssuranceCase projected = sacm_adapter::project_case(*document);
    EXPECT_EQ(CountElementsOfType(projected, "resource"), 0) << "clearing nothing created a Resource";
    EXPECT_TRUE(Find(projected, "Sn1")->referenced_artifact_id.empty());

    std::filesystem::remove_all(root);
}

// One audited project, the way the application opens one, so the command runs
// through the real bus and the audit log can be replayed against the result.
struct BusFixture {
    core::AssuranceProject project;
    std::filesystem::path sacm_abs;
    sacm::AssuranceCasePackage package;
    parser::AssuranceCase model;
    std::unique_ptr<sacm_adapter::LibraryDocument> document;
    std::unique_ptr<core::commands::CommandBus> bus;
};

std::unique_ptr<BusFixture> MakeBusFixture(const std::string& tag, bool library_backed) {
    auto fixture = std::make_unique<BusFixture>();
    const std::filesystem::path root = MakeTempRoot(tag);
    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, kSacm);

    fixture->project.id = "p";
    fixture->project.name = "Project";
    fixture->project.rootPath = root;
    core::ProjectFileEntry entry;
    entry.id = "f1";
    entry.relativePath = sacm_rel;
    entry.role = core::ProjectFileRole::SacmArgument;
    fixture->project.files.push_back(entry);

    core::audit::EnsureAuditStoreResult ensure;
    std::string error;
    EXPECT_TRUE(core::audit::EnsureAuditStore(fixture->project, sacm_rel, ensure, error)) << error;
    fixture->sacm_abs = fixture->project.rootPath / sacm_rel;

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(fixture->sacm_abs);
    EXPECT_TRUE(loaded.ok);
    if (loaded.document == nullptr)
        return fixture;
    core::RebuildDerivedViewsFromLibrary(*loaded.document, fixture->model, fixture->package);
    if (library_backed)
        fixture->document = std::move(loaded.document);

    fixture->bus = core::commands::CommandBus::Open(fixture->project, fixture->sacm_abs, error);
    EXPECT_TRUE(fixture->bus) << error;
    return fixture;
}

core::commands::CommandResult RunCommand(BusFixture& fixture, core::commands::ICommand& command) {
    core::commands::CommandContext ctx{fixture.model, fixture.package, fixture.document.get()};
    core::commands::CommandResult result = fixture.bus->Execute(command, ctx, "tester");
    if (ctx.library_primary && fixture.document != nullptr)
        core::RebuildDerivedViewsFromLibrary(*fixture.document, fixture.model, fixture.package);
    return result;
}

TEST(EvidenceLocation, CommandRecordsTheLocationAndTheAuditLogReplaysIt) {
    std::unique_ptr<BusFixture> fixture = MakeBusFixture("bus", /*library_backed=*/true);
    ASSERT_NE(fixture->document, nullptr);
    ASSERT_NE(fixture->bus, nullptr);

    core::commands::SetEvidenceLocationCommand first("Sn1", "https://example.org/report.pdf");
    const core::commands::CommandResult first_result = RunCommand(*fixture, first);
    ASSERT_TRUE(first_result.success) << first_result.error;
    EXPECT_TRUE(first_result.sacm_written);
    EXPECT_TRUE(first.OldLocation().empty());
    EXPECT_FALSE(first.WasNoOp());
    ASSERT_NE(Find(fixture->model, "Sn1"), nullptr);
    EXPECT_EQ(Find(fixture->model, "Sn1")->artifact_location, "https://example.org/report.pdf");

    // A second edit, so the replay has to reproduce a create followed by an
    // update on the Resource it created.
    core::commands::SetEvidenceLocationCommand second("Sn1", "evidence/report-v2.pdf");
    ASSERT_TRUE(RunCommand(*fixture, second).success);
    EXPECT_EQ(second.OldLocation(), "https://example.org/report.pdf");
    EXPECT_EQ(Find(fixture->model, "Sn1")->artifact_location, "evidence/report-v2.pdf");

    // Recording the same location again is a no-op transaction, not a refusal.
    core::commands::SetEvidenceLocationCommand same("Sn1", "evidence/report-v2.pdf");
    ASSERT_TRUE(RunCommand(*fixture, same).success);
    EXPECT_TRUE(same.WasNoOp());
    // ...and so is the same location with whitespace around it, since the seam
    // trims before it writes and nothing on disk would change.
    core::commands::SetEvidenceLocationCommand padded("Sn1", "  evidence/report-v2.pdf ");
    ASSERT_TRUE(RunCommand(*fixture, padded).success);
    EXPECT_TRUE(padded.WasNoOp());

    const core::audit::ReplayVerificationResult verification = core::audit::VerifyProject(fixture->project);
    EXPECT_TRUE(verification.ran);
    EXPECT_TRUE(verification.success) << "snapshot " << verification.snapshot_canonical_hash << " replayed "
                                      << verification.replayed_canonical_hash << " on disk "
                                      << verification.on_disk_canonical_hash;

    std::filesystem::remove_all(fixture->project.rootPath);
}

TEST(EvidenceLocation, CommandRefusesWithoutALibraryDocumentAndOnANonEvidenceElement) {
    std::unique_ptr<BusFixture> legacy = MakeBusFixture("legacy", /*library_backed=*/false);
    ASSERT_NE(legacy->bus, nullptr);
    core::commands::SetEvidenceLocationCommand without_document("Sn1", "https://example.org");
    const core::commands::CommandResult refused = RunCommand(*legacy, without_document);
    EXPECT_FALSE(refused.success);
    EXPECT_NE(refused.error.find("library"), std::string::npos) << refused.error;
    std::filesystem::remove_all(legacy->project.rootPath);

    std::unique_ptr<BusFixture> backed = MakeBusFixture("claim", /*library_backed=*/true);
    ASSERT_NE(backed->document, nullptr);
    core::commands::SetEvidenceLocationCommand on_claim("G1", "https://example.org");
    const core::commands::CommandResult on_claim_result = RunCommand(*backed, on_claim);
    EXPECT_FALSE(on_claim_result.success);
    EXPECT_NE(on_claim_result.error.find("ArtifactReference"), std::string::npos) << on_claim_result.error;
    EXPECT_EQ(CountElementsOfType(backed->model, "resource"), 0);
    std::filesystem::remove_all(backed->project.rootPath);
}

core::reviews::PatchOperation SetLocationOp(const std::string& element_id, const std::string& location) {
    core::reviews::PatchOperation operation;
    operation.type = core::reviews::PatchOperationType::SetEvidenceLocation;
    core::reviews::ElementRef element;
    element.existing_id = element_id;
    operation.element = element;
    operation.new_value = location;
    return operation;
}

TEST(EvidenceLocation, DraftOperationRecordsTheLocationOrIsRefusedInTheCallThatMadeIt) {
    const std::filesystem::path root = MakeTempRoot("draft");
    std::unique_ptr<sacm_adapter::LibraryDocument> document = LoadSample(root);
    ASSERT_NE(document, nullptr);

    const core::drafts::DraftOperationResult applied =
        core::drafts::ApplyOperationsToDraftDocument(*document, {SetLocationOp("Sn1", "https://example.org/r.pdf")});
    ASSERT_TRUE(applied.applied) << applied.error;
    EXPECT_EQ(Find(sacm_adapter::project_case(*document), "Sn1")->artifact_location, "https://example.org/r.pdf");

    const core::drafts::DraftOperationResult refused =
        core::drafts::ApplyOperationsToDraftDocument(*document, {SetLocationOp("G1", "https://example.org")});
    EXPECT_FALSE(refused.applied);
    EXPECT_EQ(refused.failed_operation, 1u);
    EXPECT_NE(refused.error.find("ArtifactReference"), std::string::npos) << refused.error;

    // The operation survives the proposal wire format, so a client can send it
    // and a stored draft can carry it.
    EXPECT_STREQ(core::reviews::PatchOperationTypeToString(core::reviews::PatchOperationType::SetEvidenceLocation),
                 "SetEvidenceLocation");
    core::reviews::PatchOperationType parsed = core::reviews::PatchOperationType::CreateClaim;
    ASSERT_TRUE(core::reviews::PatchOperationTypeFromString("SetEvidenceLocation", parsed));
    EXPECT_EQ(parsed, core::reviews::PatchOperationType::SetEvidenceLocation);

    std::filesystem::remove_all(root);
}

} // namespace
