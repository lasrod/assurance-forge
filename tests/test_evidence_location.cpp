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
#include "core/evidence_attributes.h"
#include "core/drafts/draft_operation_apply.h"
#include "core/project_model.h"
#include "core/registers/register_model.h"
#include "core/reviews/review_proposal.h"
#include "parser/model_utils.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/document_edit.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <algorithm>
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

// ---------------------------------------------------------------------------
// The register's other columns, recorded on the Artifact the reference cites.

TEST(EvidenceRecord, AttributeTokensRoundTrip) {
    for (const core::EvidenceAttribute attribute : core::kAllEvidenceAttributes) {
        core::EvidenceAttribute parsed = core::EvidenceAttribute::Notes;
        ASSERT_TRUE(core::ParseEvidenceAttribute(core::EvidenceAttributeToken(attribute), parsed))
            << core::EvidenceAttributeToken(attribute);
        EXPECT_EQ(parsed, attribute);
    }
    core::EvidenceAttribute unused = core::EvidenceAttribute::Owner;
    EXPECT_FALSE(core::ParseEvidenceAttribute("recency", unused)) << "the project-file field name is not a column";
}

TEST(EvidenceRecord, SeamCreatesTheArtifactOnceAndRecordsEachColumn) {
    const std::filesystem::path root = MakeTempRoot("record");
    std::unique_ptr<sacm_adapter::LibraryDocument> document = LoadSample(root);
    ASSERT_NE(document, nullptr);

    // The first write creates the Artifact, named after the reference.
    const sacm_adapter::EditOutcome first = sacm_adapter::apply_set_evidence_attribute(
        *document, "Sn1", core::EvidenceAttribute::Owner, "  Safety Engineering ");
    ASSERT_TRUE(first.supported);
    ASSERT_TRUE(first.applied) << (first.diagnostics.empty() ? "" : first.diagnostics.front().message);
    core::AssuranceCase projected = sacm_adapter::project_case(*document);
    const core::SacmElement* evidence = Find(projected, "Sn1");
    ASSERT_NE(evidence, nullptr);
    EXPECT_EQ(evidence->evidence.owner, "Safety Engineering") << "the value was not trimmed";
    EXPECT_FALSE(evidence->evidence.artifact_id.empty());
    EXPECT_EQ(CountElementsOfType(projected, "artifact"), 1);

    // Every other column lands on the same Artifact.
    ASSERT_TRUE(
        sacm_adapter::apply_set_evidence_attribute(*document, "Sn1", core::EvidenceAttribute::Type, "Test report")
            .applied);
    ASSERT_TRUE(sacm_adapter::apply_set_evidence_attribute(*document, "Sn1", core::EvidenceAttribute::Version, "rev B")
                    .applied);
    ASSERT_TRUE(
        sacm_adapter::apply_set_evidence_attribute(*document, "Sn1", core::EvidenceAttribute::Date, "2026-06-01")
            .applied);
    ASSERT_TRUE(
        sacm_adapter::apply_set_evidence_attribute(*document, "Sn1", core::EvidenceAttribute::Maturity, "Approved")
            .applied);
    ASSERT_TRUE(sacm_adapter::apply_set_evidence_attribute(
                    *document, "Sn1", core::EvidenceAttribute::ControlledEnvironment, "Yes")
                    .applied);
    ASSERT_TRUE(sacm_adapter::apply_set_evidence_attribute(
                    *document, "Sn1", core::EvidenceAttribute::Notes, "Superseded by RA-002 for H2.")
                    .applied);
    projected = sacm_adapter::project_case(*document);
    const core::EvidenceRecord& record = Find(projected, "Sn1")->evidence;
    EXPECT_EQ(record.type, "Test report");
    EXPECT_EQ(record.version, "rev B");
    EXPECT_EQ(record.date, "2026-06-01");
    EXPECT_EQ(record.maturity, "Approved");
    EXPECT_EQ(record.controlled_environment, "Yes");
    EXPECT_EQ(record.notes, "Superseded by RA-002 for H2.");
    EXPECT_EQ(CountElementsOfType(projected, "artifact"), 1) << "a column created a second Artifact";

    // Setting the date keeps the version, and vice versa: provenance is one
    // record even though it is written a column at a time.
    ASSERT_TRUE(
        sacm_adapter::apply_set_evidence_attribute(*document, "Sn1", core::EvidenceAttribute::Date, "2026-07-01")
            .applied);
    projected = sacm_adapter::project_case(*document);
    EXPECT_EQ(Find(projected, "Sn1")->evidence.version, "rev B");
    EXPECT_EQ(Find(projected, "Sn1")->evidence.date, "2026-07-01");

    // A location recorded afterwards cites its Resource beside the Artifact.
    ASSERT_TRUE(sacm_adapter::apply_set_evidence_location(*document, "Sn1", "https://example.org/ra-001.pdf").applied);
    projected = sacm_adapter::project_case(*document);
    EXPECT_EQ(Find(projected, "Sn1")->artifact_location, "https://example.org/ra-001.pdf");
    EXPECT_EQ(Find(projected, "Sn1")->evidence.owner, "Safety Engineering");

    // The record survives a strict save.
    const sacm_adapter::SaveOutcome saved = sacm_adapter::save_document(*document, /*tolerant=*/false);
    ASSERT_TRUE(saved.ok) << (saved.diagnostics.empty() ? "" : saved.diagnostics.front().message);
    WriteFile(root / "saved.sacm", saved.xml);
    sacm_adapter::LoadOutcome reloaded = sacm_adapter::load_document(root / "saved.sacm");
    ASSERT_TRUE(reloaded.ok);
    // Held in a local: a reference into the projection temporary would dangle.
    const core::AssuranceCase reloaded_case = sacm_adapter::project_case(*reloaded.document);
    const core::EvidenceRecord& after = Find(reloaded_case, "Sn1")->evidence;
    EXPECT_EQ(after.owner, "Safety Engineering");
    EXPECT_EQ(after.version, "rev B");
    EXPECT_EQ(after.notes, "Superseded by RA-002 for H2.");

    // Clearing a tagged column removes the tag; clearing provenance empties it.
    ASSERT_TRUE(
        sacm_adapter::apply_set_evidence_attribute(*document, "Sn1", core::EvidenceAttribute::Owner, "").applied);
    ASSERT_TRUE(
        sacm_adapter::apply_set_evidence_attribute(*document, "Sn1", core::EvidenceAttribute::Version, "  ").applied);
    projected = sacm_adapter::project_case(*document);
    EXPECT_TRUE(Find(projected, "Sn1")->evidence.owner.empty());
    EXPECT_TRUE(Find(projected, "Sn1")->evidence.version.empty());
    EXPECT_EQ(Find(projected, "Sn1")->evidence.date, "2026-07-01");

    std::filesystem::remove_all(root);
}

TEST(EvidenceRecord, SeamRefusesNonEvidenceAndCreatesNothingForAClear) {
    const std::filesystem::path root = MakeTempRoot("record_refuse");
    std::unique_ptr<sacm_adapter::LibraryDocument> document = LoadSample(root);
    ASSERT_NE(document, nullptr);

    const sacm_adapter::EditOutcome on_claim =
        sacm_adapter::apply_set_evidence_attribute(*document, "G1", core::EvidenceAttribute::Owner, "x");
    EXPECT_FALSE(on_claim.applied);
    ASSERT_FALSE(on_claim.diagnostics.empty());
    EXPECT_EQ(on_claim.diagnostics.front().code, "SACM-CMD-002");

    ASSERT_TRUE(
        sacm_adapter::apply_set_evidence_attribute(*document, "Sn1", core::EvidenceAttribute::Notes, "").applied);
    const core::AssuranceCase projected = sacm_adapter::project_case(*document);
    EXPECT_EQ(CountElementsOfType(projected, "artifact"), 0) << "clearing nothing created an Artifact";
    EXPECT_TRUE(Find(projected, "Sn1")->evidence.artifact_id.empty());
    std::filesystem::remove_all(root);
}

TEST(EvidenceRecord, CommandsRecordColumnsAndTheAuditLogReplaysThem) {
    std::unique_ptr<BusFixture> fixture = MakeBusFixture("record_bus", /*library_backed=*/true);
    ASSERT_NE(fixture->document, nullptr);
    ASSERT_NE(fixture->bus, nullptr);

    core::commands::SetEvidenceAttributeCommand owner("Sn1", core::EvidenceAttribute::Owner, "Safety Engineering");
    const core::commands::CommandResult owner_result = RunCommand(*fixture, owner);
    ASSERT_TRUE(owner_result.success) << owner_result.error;
    EXPECT_TRUE(owner.OldValue().empty());
    EXPECT_EQ(Find(fixture->model, "Sn1")->evidence.owner, "Safety Engineering");

    // The same value again is a no-op transaction, whitespace included.
    core::commands::SetEvidenceAttributeCommand same("Sn1", core::EvidenceAttribute::Owner, " Safety Engineering ");
    ASSERT_TRUE(RunCommand(*fixture, same).success);
    EXPECT_TRUE(same.WasNoOp());

    // An import lands several columns as one transaction...
    core::commands::ImportEvidenceAssessmentsCommand import({
        {"Sn1", core::EvidenceAttribute::Type, "Test report"},
        {"Sn1", core::EvidenceAttribute::Date, "2026-06-01"},
        {"Sn1", core::EvidenceAttribute::Notes, "Imported from the project file."},
    });
    ASSERT_TRUE(RunCommand(*fixture, import).success);
    EXPECT_EQ(import.AppliedCount(), 3u);
    EXPECT_EQ(Find(fixture->model, "Sn1")->evidence.type, "Test report");
    EXPECT_EQ(Find(fixture->model, "Sn1")->evidence.date, "2026-06-01");

    // ...and refuses as a whole when one write names something that is not
    // evidence, before anything is written.
    core::commands::ImportEvidenceAssessmentsCommand mixed({
        {"Sn1", core::EvidenceAttribute::Maturity, "Approved"},
        {"G1", core::EvidenceAttribute::Owner, "nobody"},
    });
    const core::commands::CommandResult mixed_result = RunCommand(*fixture, mixed);
    EXPECT_FALSE(mixed_result.success);
    EXPECT_EQ(mixed.AppliedCount(), 0u);
    EXPECT_TRUE(Find(fixture->model, "Sn1")->evidence.maturity.empty()) << "a refused import wrote half of itself";

    core::commands::SetEvidenceAttributeCommand on_claim("G1", core::EvidenceAttribute::Owner, "x");
    EXPECT_FALSE(RunCommand(*fixture, on_claim).success);

    const core::audit::ReplayVerificationResult verification = core::audit::VerifyProject(fixture->project);
    EXPECT_TRUE(verification.ran);
    EXPECT_TRUE(verification.success) << "snapshot " << verification.snapshot_canonical_hash << " replayed "
                                      << verification.replayed_canonical_hash << " on disk "
                                      << verification.on_disk_canonical_hash;

    std::filesystem::remove_all(fixture->project.rootPath);
}

core::reviews::PatchOperation
SetAttributeOp(const std::string& element_id, const std::string& column, const std::string& value) {
    core::reviews::PatchOperation operation;
    operation.type = core::reviews::PatchOperationType::SetEvidenceAttribute;
    core::reviews::ElementRef element;
    element.existing_id = element_id;
    operation.element = element;
    operation.field = column;
    operation.new_value = value;
    return operation;
}

TEST(EvidenceRecord, DraftOperationRecordsAColumnOrRefusesAnUnknownOne) {
    const std::filesystem::path root = MakeTempRoot("record_draft");
    std::unique_ptr<sacm_adapter::LibraryDocument> document = LoadSample(root);
    ASSERT_NE(document, nullptr);

    const core::drafts::DraftOperationResult applied = core::drafts::ApplyOperationsToDraftDocument(
        *document, {SetAttributeOp("Sn1", "owner", "Safety Engineering"), SetAttributeOp("Sn1", "version", "rev B")});
    ASSERT_TRUE(applied.applied) << applied.error;
    const core::AssuranceCase drafted = sacm_adapter::project_case(*document);
    const core::EvidenceRecord& record = Find(drafted, "Sn1")->evidence;
    EXPECT_EQ(record.owner, "Safety Engineering");
    EXPECT_EQ(record.version, "rev B");

    const core::drafts::DraftOperationResult unknown =
        core::drafts::ApplyOperationsToDraftDocument(*document, {SetAttributeOp("Sn1", "recency", "2026")});
    EXPECT_FALSE(unknown.applied);
    EXPECT_NE(unknown.error.find("recency"), std::string::npos) << unknown.error;
    EXPECT_NE(unknown.error.find("controlled_environment"), std::string::npos)
        << "the refusal does not list the columns that exist: " << unknown.error;

    EXPECT_STREQ(core::reviews::PatchOperationTypeToString(core::reviews::PatchOperationType::SetEvidenceAttribute),
                 "SetEvidenceAttribute");
    std::filesystem::remove_all(root);
}

TEST(EvidenceRecord, APickedFileInsideTheProjectIsRecordedRelativeToIt) {
    const std::filesystem::path root = MakeTempRoot("picked");
    std::filesystem::create_directories(root / "evidence" / "reports");
    WriteFile(root / "evidence" / "reports" / "ra-001.pdf", "pdf");
    EXPECT_EQ(core::EvidenceLocationForPickedFile(root, root / "evidence" / "reports" / "ra-001.pdf"),
              "evidence/reports/ra-001.pdf");

    // Outside the project, the absolute path is kept: a relative one that
    // climbs out would break as soon as the project moved.
    const std::filesystem::path elsewhere = MakeTempRoot("picked_elsewhere");
    WriteFile(elsewhere / "shared.pdf", "pdf");
    const std::string outside = core::EvidenceLocationForPickedFile(root, elsewhere / "shared.pdf");
    EXPECT_EQ(outside.rfind("..", 0), std::string::npos) << outside;
    EXPECT_NE(outside.find("shared.pdf"), std::string::npos) << outside;
    EXPECT_TRUE(std::filesystem::path(outside).is_absolute()) << outside;

    // No project: absolute.
    EXPECT_TRUE(std::filesystem::path(core::EvidenceLocationForPickedFile({}, elsewhere / "shared.pdf")).is_absolute());
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(elsewhere);
}

// ---------------------------------------------------------------------------
// Authoring from the register: create evidence (under a claim, or bare), link
// existing evidence to a claim, and have the audit log replay all of it.

const core::SacmElement*
SupportLink(const core::AssuranceCase& model, const std::string& claim_id, const std::string& evidence_id) {
    for (const core::SacmElement& element : model.elements) {
        if (element.type != "assertedevidence")
            continue;
        const bool from =
            std::find(element.source_refs.begin(), element.source_refs.end(), evidence_id) != element.source_refs.end();
        const bool to =
            std::find(element.target_refs.begin(), element.target_refs.end(), claim_id) != element.target_refs.end();
        if (from && to)
            return &element;
    }
    return nullptr;
}

TEST(EvidenceAuthoring, CreateAndLinkCommandsAuthorEvidenceAndReplay) {
    std::unique_ptr<BusFixture> fixture = MakeBusFixture("author", /*library_backed=*/true);
    ASSERT_NE(fixture->document, nullptr);
    ASSERT_NE(fixture->bus, nullptr);

    // Under a claim: the element, its statement and the AssertedEvidence.
    core::commands::CreateEvidenceCommand under("G1", "  Vibration test report, run 4. ");
    const core::commands::CommandResult under_result = RunCommand(*fixture, under);
    ASSERT_TRUE(under_result.success) << under_result.error;
    ASSERT_FALSE(under.GeneratedId().empty());
    ASSERT_FALSE(under.GeneratedRelationshipId().empty());
    const core::SacmElement* created = Find(fixture->model, under.GeneratedId());
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created->type, "artifactreference");
    EXPECT_EQ(created->description, "Vibration test report, run 4.") << "the statement was not recorded or trimmed";
    const core::SacmElement* link = SupportLink(fixture->model, "G1", under.GeneratedId());
    ASSERT_NE(link, nullptr) << "the new evidence does not support the claim";
    EXPECT_EQ(link->id, under.GeneratedRelationshipId());

    // Bare: registered before anything rests on it, so no relationship.
    core::commands::CreateEvidenceCommand bare("", "Supplier audit record.");
    ASSERT_TRUE(RunCommand(*fixture, bare).success);
    ASSERT_FALSE(bare.GeneratedId().empty());
    EXPECT_TRUE(bare.GeneratedRelationshipId().empty());
    EXPECT_NE(bare.GeneratedId(), under.GeneratedId());
    EXPECT_TRUE(core::registers::DeriveEvidenceCitations(fixture->model, bare.GeneratedId()).empty());
    EXPECT_EQ(Find(fixture->model, bare.GeneratedId())->description, "Supplier audit record.");

    // Linking the bare evidence to the claim creates the relationship...
    core::commands::LinkEvidenceCommand linked("G1", bare.GeneratedId());
    ASSERT_TRUE(RunCommand(*fixture, linked).success);
    ASSERT_FALSE(linked.GeneratedRelationshipId().empty());
    EXPECT_NE(SupportLink(fixture->model, "G1", bare.GeneratedId()), nullptr);

    // ...and doing it again is refused rather than doubled.
    core::commands::LinkEvidenceCommand again("G1", bare.GeneratedId());
    const core::commands::CommandResult again_result = RunCommand(*fixture, again);
    EXPECT_FALSE(again_result.success);
    EXPECT_NE(again_result.error.find("already"), std::string::npos) << again_result.error;

    // Linking a claim, or to something that is not a claim, is refused.
    core::commands::LinkEvidenceCommand not_evidence("G1", "G1");
    EXPECT_FALSE(RunCommand(*fixture, not_evidence).success);
    core::commands::LinkEvidenceCommand not_a_claim("Sn1", bare.GeneratedId());
    EXPECT_FALSE(RunCommand(*fixture, not_a_claim).success);

    const core::audit::ReplayVerificationResult verification = core::audit::VerifyProject(fixture->project);
    EXPECT_TRUE(verification.ran);
    EXPECT_TRUE(verification.success) << "snapshot " << verification.snapshot_canonical_hash << " replayed "
                                      << verification.replayed_canonical_hash << " on disk "
                                      << verification.on_disk_canonical_hash;

    std::filesystem::remove_all(fixture->project.rootPath);
}

TEST(EvidenceAuthoring, CommandsRefuseWithoutALibraryDocument) {
    std::unique_ptr<BusFixture> legacy = MakeBusFixture("author_legacy", /*library_backed=*/false);
    ASSERT_NE(legacy->bus, nullptr);
    core::commands::CreateEvidenceCommand create("G1", "x");
    EXPECT_FALSE(RunCommand(*legacy, create).success);
    core::commands::LinkEvidenceCommand link("G1", "Sn1");
    EXPECT_FALSE(RunCommand(*legacy, link).success);
    std::filesystem::remove_all(legacy->project.rootPath);
}

core::reviews::PatchOperation
SupportOp(core::reviews::PatchOperationType type, const std::string& evidence_id, const std::string& claim_id) {
    core::reviews::PatchOperation operation;
    operation.type = type;
    core::reviews::ElementRef source;
    source.existing_id = evidence_id;
    core::reviews::ElementRef target;
    target.existing_id = claim_id;
    operation.source = source;
    operation.target = target;
    return operation;
}

// A draft withdraws a solution's support the way it withdraws a claim's: the
// register's unlink stages RemoveSupportedBy, which used to look only for an
// AssertedInference and so could never find the AssertedEvidence a solution
// attaches by.
TEST(EvidenceAuthoring, DraftRemoveSupportedByWithdrawsEvidenceAndAddSupportedByRestoresIt) {
    const std::filesystem::path root = MakeTempRoot("author_draft");
    std::unique_ptr<sacm_adapter::LibraryDocument> document = LoadSample(root);
    ASSERT_NE(document, nullptr);
    ASSERT_NE(SupportLink(sacm_adapter::project_case(*document), "G1", "Sn1"), nullptr);

    const core::drafts::DraftOperationResult withdrawn = core::drafts::ApplyOperationsToDraftDocument(
        *document, {SupportOp(core::reviews::PatchOperationType::RemoveSupportedBy, "Sn1", "G1")});
    ASSERT_TRUE(withdrawn.applied) << withdrawn.error;
    core::AssuranceCase projected = sacm_adapter::project_case(*document);
    EXPECT_EQ(SupportLink(projected, "G1", "Sn1"), nullptr) << "the AssertedEvidence survived the withdrawal";
    ASSERT_NE(Find(projected, "Sn1"), nullptr) << "the evidence itself must stay";

    const core::drafts::DraftOperationResult restored = core::drafts::ApplyOperationsToDraftDocument(
        *document, {SupportOp(core::reviews::PatchOperationType::AddSupportedBy, "Sn1", "G1")});
    ASSERT_TRUE(restored.applied) << restored.error;
    projected = sacm_adapter::project_case(*document);
    const core::SacmElement* link = SupportLink(projected, "G1", "Sn1");
    ASSERT_NE(link, nullptr);
    EXPECT_EQ(link->type, "assertedevidence") << "a solution attaches by AssertedEvidence, not inference";

    std::filesystem::remove_all(root);
}

} // namespace
