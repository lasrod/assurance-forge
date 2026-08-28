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

// The "Used by" popup lists the claims resting on a piece of evidence with the
// relationship carrying each, so an unlink can name exactly what it withdraws.
// A relationship carrying several sources is flagged: withdrawing one end of it
// would withdraw more than the one link.
TEST(RegisterModelTest, DerivesTheClaimsRestingOnEvidenceWithTheirRelationships) {
    parser::AssuranceCase model = TwoClaimsSharingEvidence();
    model.elements.push_back(Element("G3", "claim", "Third goal"));
    model.elements.push_back(Element("Sn2", "artifactreference", "Shared report"));
    model.elements.push_back(EvidenceLink("R3", {"Sn1", "Sn2"}, {"G3"}));

    const std::vector<core::registers::EvidenceCitation> citations =
        core::registers::DeriveEvidenceCitations(model, "Sn1");
    ASSERT_EQ(citations.size(), 3u);
    EXPECT_EQ(citations[0].claim_id, "G1");
    EXPECT_EQ(citations[0].relationship_id, "R1");
    EXPECT_FALSE(citations[0].shared);
    EXPECT_EQ(citations[1].claim_id, "G2");
    EXPECT_EQ(citations[1].relationship_id, "R2");
    EXPECT_EQ(citations[2].claim_id, "G3");
    EXPECT_EQ(citations[2].relationship_id, "R3");
    EXPECT_TRUE(citations[2].shared) << "R3 also carries Sn2";

    EXPECT_TRUE(core::registers::DeriveEvidenceCitations(model, "Sn9").empty());
    // Evidence attaches under a Goal or a Strategy. An Assumption and a
    // Justification are SACM Claims but GSN leaves, so the picker never offers
    // them -- attaching under one is refused by core::CanAddChildElement.
    parser::SacmElement assumption = Element("A1", "claim", "An assumption");
    assumption.assertion_declaration = "assumed";
    model.elements.push_back(assumption);
    parser::SacmElement justification = Element("J1", "claim", "A justification");
    justification.assertion_declaration = "justification";
    model.elements.push_back(justification);
    model.elements.push_back(Element("S1", "argumentreasoning", "A strategy"));
    EXPECT_EQ(core::registers::DeriveEvidenceSupportTargets(model), (std::vector<std::string>{"G1", "G2", "G3", "S1"}));
}

// Which end of an AssertedEvidence carries the claim varies with the dialect a
// file came from, which is why DeriveCseLinks reads both. Reading only
// `source` lost every citation in a document written the other way round --
// and with it the register's Used-by list and its unlink.
TEST(RegisterModelTest, CitationsAreFoundWhicheverEndCarriesTheEvidence) {
    parser::AssuranceCase model;
    model.elements.push_back(Element("G1", "claim", "Top goal"));
    model.elements.push_back(Element("Sn1", "artifactreference", "Test report"));
    // Reversed: the claim is the source and the evidence the target.
    model.elements.push_back(EvidenceLink("R1", {"G1"}, {"Sn1"}));

    const std::vector<core::registers::EvidenceCitation> citations =
        core::registers::DeriveEvidenceCitations(model, "Sn1");
    ASSERT_EQ(citations.size(), 1u) << "the citation was missed because it was written the other way round";
    EXPECT_EQ(citations[0].claim_id, "G1");
    EXPECT_EQ(citations[0].relationship_id, "R1");
    EXPECT_FALSE(citations[0].shared);

    // A relationship carrying two claims withdraws both if it is deleted, so
    // neither link may be unlinked from the register on its own.
    parser::AssuranceCase shared_model;
    shared_model.elements.push_back(Element("G1", "claim", "First goal"));
    shared_model.elements.push_back(Element("G2", "claim", "Second goal"));
    shared_model.elements.push_back(Element("Sn1", "artifactreference", "Test report"));
    shared_model.elements.push_back(EvidenceLink("R1", {"Sn1"}, {"G1", "G2"}));
    const std::vector<core::registers::EvidenceCitation> shared =
        core::registers::DeriveEvidenceCitations(shared_model, "Sn1");
    ASSERT_EQ(shared.size(), 2u);
    EXPECT_TRUE(shared[0].shared);
    EXPECT_TRUE(shared[1].shared);
}

// A CSE assessment lives on the AssertedEvidence carrying the support, so each
// link has to say which relationship that is -- and whether that relationship
// carries other pairings, whose assessment it would then share.
TEST(RegisterModelTest, LinksCarryTheRelationshipThatSupportsThem) {
    const std::vector<core::registers::CseLink> links = core::registers::DeriveCseLinks(TwoClaimsSharingEvidence());
    ASSERT_EQ(links.size(), 2u);
    EXPECT_EQ(links[0].relationship_id, "R1");
    EXPECT_FALSE(links[0].shares_relationship);
    EXPECT_EQ(links[1].relationship_id, "R2");
    EXPECT_FALSE(links[1].shares_relationship);

    // One relationship carrying two claims: both rows share its assessment.
    parser::AssuranceCase model;
    model.elements.push_back(Element("G1", "claim", "First goal"));
    model.elements.push_back(Element("G2", "claim", "Second goal"));
    model.elements.push_back(Element("Sn1", "artifactreference", "Test report"));
    model.elements.push_back(EvidenceLink("R1", {"Sn1"}, {"G1", "G2"}));
    const std::vector<core::registers::CseLink> shared = core::registers::DeriveCseLinks(model);
    ASSERT_EQ(shared.size(), 2u);
    for (const core::registers::CseLink& link : shared) {
        EXPECT_EQ(link.relationship_id, "R1");
        EXPECT_TRUE(link.shares_relationship);
    }
}

// Two relationships carrying the same pairing is a malformed-but-loadable
// document. The row that results stores its assessment ON the relationship it
// names, so that name must not depend on which one the file happened to list
// first -- a save, a reload or another dialect can reorder them, and the
// assessment would then be read from, and written to, the other one.
TEST(RegisterModelTest, ADuplicatedPairingAlwaysPicksTheSameRelationship) {
    const auto links_for = [](bool reversed) {
        parser::AssuranceCase model;
        model.elements.push_back(Element("G1", "claim", "Top goal"));
        model.elements.push_back(Element("Sn1", "artifactreference", "Test report"));
        if (reversed) {
            model.elements.push_back(EvidenceLink("R2", {"Sn1"}, {"G1"}));
            model.elements.push_back(EvidenceLink("R1", {"Sn1"}, {"G1"}));
        } else {
            model.elements.push_back(EvidenceLink("R1", {"Sn1"}, {"G1"}));
            model.elements.push_back(EvidenceLink("R2", {"Sn1"}, {"G1"}));
        }
        return core::registers::DeriveCseLinks(model);
    };

    const std::vector<core::registers::CseLink> in_order = links_for(false);
    const std::vector<core::registers::CseLink> reversed = links_for(true);
    ASSERT_EQ(in_order.size(), 1u) << "one pairing is one row however many relationships carry it";
    ASSERT_EQ(reversed.size(), 1u);
    EXPECT_EQ(in_order[0].relationship_id, "R1");
    EXPECT_EQ(reversed[0].relationship_id, "R1") << "the chosen relationship followed document order";
    EXPECT_EQ(in_order[0].shares_relationship, reversed[0].shares_relationship);
}
