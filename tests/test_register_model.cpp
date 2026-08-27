#include "core/registers/register_model.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using core::registers::CseLink;
using core::registers::RegisterStore;

parser::SacmElement Element(std::string id, std::string type, std::string name = {}) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = std::move(type);
    element.name = std::move(name);
    return element;
}

parser::SacmElement EvidenceLink(std::string id, std::vector<std::string> sources, std::vector<std::string> targets) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = "assertedevidence";
    element.source_refs = std::move(sources);
    element.target_refs = std::move(targets);
    return element;
}

parser::AssuranceCase TwoClaimsSharingEvidence() {
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Top goal"),
                      Element("G2", "claim", "Sub goal"),
                      Element("Sn1", "artifactreference", "Test report"),
                      EvidenceLink("R1", {"Sn1"}, {"G1"}),
                      EvidenceLink("R2", {"Sn1"}, {"G2"})};
    return model;
}

} // namespace

TEST(RegisterModelTest, DerivesOneLinkPerClaimEvidencePairing) {
    const std::vector<CseLink> links = core::registers::DeriveCseLinks(TwoClaimsSharingEvidence());

    ASSERT_EQ(links.size(), 2u);
    EXPECT_EQ(links[0].claim_id, "G1");
    EXPECT_EQ(links[0].evidence_id, "Sn1");
    EXPECT_EQ(links[1].claim_id, "G2");
    EXPECT_EQ(links[1].evidence_id, "Sn1");
}

TEST(RegisterModelTest, LinkOrderDoesNotFollowDocumentOrder) {
    parser::AssuranceCase forward = TwoClaimsSharingEvidence();
    parser::AssuranceCase reversed = forward;
    std::reverse(reversed.elements.begin(), reversed.elements.end());

    EXPECT_EQ(core::registers::DeriveCseLinks(forward), core::registers::DeriveCseLinks(reversed));
}

TEST(RegisterModelTest, DuplicateRelationshipsProduceOneRow) {
    parser::AssuranceCase model = TwoClaimsSharingEvidence();
    model.elements.push_back(EvidenceLink("R3", {"Sn1"}, {"G1"})); // same pairing again

    EXPECT_EQ(core::registers::DeriveCseLinks(model).size(), 2u);
}

TEST(RegisterModelTest, EvidenceNobodyUsesIsStillListed) {
    // Unused evidence is a finding, not something to hide from the register.
    parser::AssuranceCase model = TwoClaimsSharingEvidence();
    model.elements.push_back(Element("Sn2", "artifactreference", "Unused report"));

    const std::vector<std::string> ids = core::registers::DeriveEvidenceIds(model);

    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[1], "Sn2");
    EXPECT_EQ(core::registers::CountCseUses(core::registers::DeriveCseLinks(model), "Sn2"), 0);
}

TEST(RegisterModelTest, CountsHowManyClaimsRestOnEachEvidence) {
    const std::vector<CseLink> links = core::registers::DeriveCseLinks(TwoClaimsSharingEvidence());
    EXPECT_EQ(core::registers::CountCseUses(links, "Sn1"), 2);
    EXPECT_EQ(core::registers::CountCseUses(links, "missing"), 0);
}

TEST(RegisterModelTest, DanglingEndpointsAreIgnored) {
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Top goal"), EvidenceLink("R1", {"MISSING"}, {"G1"})};

    EXPECT_TRUE(core::registers::DeriveCseLinks(model).empty());
}

// The CSE id is the key every stored assessment hangs on. Changing its format
// orphans all of them, so the format is pinned here on purpose.
TEST(RegisterModelTest, CseIdFormatIsStable) {
    EXPECT_EQ(core::registers::MakeCseId("G1", "Sn1"), "CSE:G1->Sn1");
}

TEST(RegisterModelTest, DisplayTextFallsBackThroughNameContentDescriptionId) {
    parser::SacmElement named = Element("G1", "claim", "The name");
    named.content = "The content";
    EXPECT_EQ(core::registers::DisplayTextFor(&named), "The name");

    parser::SacmElement content_only = Element("G2", "claim");
    content_only.content = "The content";
    EXPECT_EQ(core::registers::DisplayTextFor(&content_only), "The content");

    parser::SacmElement description_only = Element("G3", "claim");
    description_only.description = "The description";
    EXPECT_EQ(core::registers::DisplayTextFor(&description_only), "The description");

    const parser::SacmElement bare = Element("G4", "claim");
    EXPECT_EQ(core::registers::DisplayTextFor(&bare), "G4");
    EXPECT_EQ(core::registers::DisplayTextFor(nullptr), "");
}

// ===== Assessments the user typed must survive a save and reload =====

TEST(RegisterModelTest, AssessmentsRoundTripThroughJson) {
    RegisterStore store;
    core::registers::CseMetadata cse;
    cse.claim_owner = "A. Engineer";
    cse.evidence_owner = "B. Tester";
    cse.safety_case_owner = "C. Manager";
    cse.claim_criteria = "Must hold under all operating modes";
    cse.evidence_criteria = "Coverage ≥ 95%";
    cse.assessment_status = "Accepted";
    cse.notes = "Reviewed 2026-07-26";
    store.cse[core::registers::MakeCseId("G1", "Sn1")] = cse;

    core::registers::EvidenceMetadata evidence;
    evidence.evidence_owner = "B. Tester";
    evidence.type = "Test report";
    evidence.recency = "2026-06";
    evidence.maturity = "Qualified";
    evidence.controlled_environment = "Yes";
    evidence.notes = "Rig 4";
    store.evidence["Sn1"] = evidence;

    RegisterStore reloaded;
    std::string error;
    ASSERT_TRUE(
        core::registers::DeserializeRegisterStore(core::registers::SerializeRegisterStore(store), reloaded, error))
        << error;

    ASSERT_EQ(reloaded.cse.size(), 1u);
    const core::registers::CseMetadata& round_tripped = reloaded.cse.at(core::registers::MakeCseId("G1", "Sn1"));
    EXPECT_EQ(round_tripped.claim_owner, cse.claim_owner);
    EXPECT_EQ(round_tripped.evidence_criteria, cse.evidence_criteria);
    EXPECT_EQ(round_tripped.assessment_status, "Accepted");
    EXPECT_EQ(round_tripped.notes, cse.notes);

    ASSERT_EQ(reloaded.evidence.size(), 1u);
    EXPECT_EQ(reloaded.evidence.at("Sn1").maturity, "Qualified");
    EXPECT_EQ(reloaded.evidence.at("Sn1").controlled_environment, "Yes");
}

TEST(RegisterModelTest, SerializationIsByteStable) {
    RegisterStore store;
    store.cse[core::registers::MakeCseId("G2", "Sn1")].notes = "second";
    store.cse[core::registers::MakeCseId("G1", "Sn1")].notes = "first";
    store.evidence["Sn2"].type = "b";
    store.evidence["Sn1"].type = "a";

    EXPECT_EQ(core::registers::SerializeRegisterStore(store), core::registers::SerializeRegisterStore(store));
    // Insertion order must not leak into the file, or every session produces a
    // spurious diff.
    RegisterStore other;
    other.cse[core::registers::MakeCseId("G1", "Sn1")].notes = "first";
    other.cse[core::registers::MakeCseId("G2", "Sn1")].notes = "second";
    other.evidence["Sn1"].type = "a";
    other.evidence["Sn2"].type = "b";
    EXPECT_EQ(core::registers::SerializeRegisterStore(store), core::registers::SerializeRegisterStore(other));
}

TEST(RegisterModelTest, DefaultAssessmentStatusSurvivesAnAbsentField) {
    RegisterStore store;
    std::string error;
    ASSERT_TRUE(core::registers::DeserializeRegisterStore(
        R"({"format":"assurance-forge-register-assessments","cseAssessments":[{"cseId":"CSE:G1->Sn1"}]})",
        store,
        error))
        << error;

    ASSERT_EQ(store.cse.size(), 1u);
    EXPECT_EQ(store.cse.at("CSE:G1->Sn1").assessment_status, "Not Assessed");
}

TEST(RegisterModelTest, MalformedJsonIsRefusedRatherThanPartiallyLoaded) {
    RegisterStore store;
    std::string error;
    EXPECT_FALSE(core::registers::DeserializeRegisterStore("{not json", store, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(store.cse.empty());
}

TEST(RegisterModelTest, AForeignFormatIsRefusedRatherThanGuessedAt) {
    RegisterStore store;
    std::string error;
    EXPECT_FALSE(
        core::registers::DeserializeRegisterStore(R"({"format":"something-else","cseAssessments":[]})", store, error));
    EXPECT_FALSE(error.empty());
}

// ===== Orphans are reported, never silently dropped =====

TEST(RegisterModelTest, ReportsAssessmentsWhoseSubjectIsGone) {
    RegisterStore store;
    store.cse[core::registers::MakeCseId("G1", "Sn1")].notes = "still here";
    store.cse[core::registers::MakeCseId("GONE", "Sn1")].notes = "orphan";
    store.evidence["Sn1"].type = "still here";
    store.evidence["SnGone"].type = "orphan";

    const parser::AssuranceCase model = TwoClaimsSharingEvidence();
    const core::registers::OrphanedMetadata orphans = core::registers::FindOrphanedMetadata(
        store, core::registers::DeriveCseLinks(model), core::registers::DeriveEvidenceIds(model));

    ASSERT_EQ(orphans.cse_ids.size(), 1u);
    EXPECT_EQ(orphans.cse_ids.front(), "CSE:GONE->Sn1");
    ASSERT_EQ(orphans.evidence_ids.size(), 1u);
    EXPECT_EQ(orphans.evidence_ids.front(), "SnGone");
}

TEST(RegisterModelTest, FindingOrphansDoesNotRemoveThem) {
    // A renamed element must not cost the reviewer their assessment; only a
    // human may decide to discard it.
    RegisterStore store;
    store.cse[core::registers::MakeCseId("GONE", "Sn1")].notes = "hours of review";

    const parser::AssuranceCase model = TwoClaimsSharingEvidence();
    core::registers::FindOrphanedMetadata(
        store, core::registers::DeriveCseLinks(model), core::registers::DeriveEvidenceIds(model));

    EXPECT_EQ(store.cse.size(), 1u);
    EXPECT_EQ(store.cse.at("CSE:GONE->Sn1").notes, "hours of review");
}

// SACM has one class for a GSN Solution and a GSN Context; only the
// relationship tells them apart. A reference reached by an AssertedContext is
// context, and the register used to list every context in the case as evidence
// nothing cites.
TEST(RegisterModelTest, AnArtifactReferenceAttachedAsContextIsNotEvidence) {
    parser::AssuranceCase model = TwoClaimsSharingEvidence();
    model.elements.push_back(Element("C1", "artifactreference", "Operating environment"));
    parser::SacmElement context;
    context.id = "R3";
    context.type = "assertedcontext";
    context.source_refs = {"C1"};
    context.target_refs = {"G1"};
    model.elements.push_back(context);
    // A reference nobody attaches at all is still evidence, and still unlinked.
    model.elements.push_back(Element("Sn9", "artifactreference", "Orphaned report"));

    const std::vector<std::string> ids = core::registers::DeriveEvidenceIds(model);
    EXPECT_EQ(ids, (std::vector<std::string>{"Sn1", "Sn9"}));
}

// The Artifact an ArtifactReference cites carries the register's columns for
// that evidence. Listing it too showed every recorded piece of evidence
// twice: once as the reference, once as its own record.
TEST(RegisterModelTest, AnArtifactCitedByAReferenceIsItsRecordNotMoreEvidence) {
    parser::AssuranceCase model = TwoClaimsSharingEvidence();
    parser::SacmElement& reference = model.elements[2];
    ASSERT_EQ(reference.id, "Sn1");
    reference.referenced_artifact_id = "resource_1";
    reference.evidence.artifact_id = "artifact_1";
    model.elements.push_back(Element("artifact_1", "artifact", "Sn1's record"));
    model.elements.push_back(Element("resource_1", "resource", "where Sn1 is"));
    // An Artifact nothing cites is still evidence in its own right.
    model.elements.push_back(Element("artifact_9", "artifact", "Uncited record"));

    const std::vector<std::string> ids = core::registers::DeriveEvidenceIds(model);
    EXPECT_EQ(ids, (std::vector<std::string>{"Sn1", "artifact_9"}));
}
