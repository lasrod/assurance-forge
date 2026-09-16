// GSN v3 Modular Extension: Away Goal (GSN3-MOD-003).
//
// An Away Goal is a Goal defined in another module and cited here. The mapping
// under test is the evidenced one in docs/sacm/sacm-gsn-mapping.md: a SACM
// Claim carrying isCitation with citedElement naming the cited Claim, where the
// module is the ArgumentPackage owning that Claim.
//
// The distinction these tests defend is that a citation is only *away* when it
// crosses a module boundary. A citation inside one module drawn as an away goal
// would tell a reader that another module carries supporting argument when no
// such module exists.

#include "core/element_factory.h"
#include "core/commands/element_commands.h"
#include "core/commands/command_bus.h"
#include "core/audit/audit_event.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_model.h"

#include <gtest/gtest.h>

namespace {

// Two modules. AP_Local owns the local top goal G1; AP_Remote owns G2, the goal
// that an away goal will cite, and a piece of evidence that is not a goal.
struct TwoModuleCase {
    parser::AssuranceCase ac;
    sacm::AssuranceCasePackage pkg;
};

parser::SacmElement MakeElement(const std::string& id, const std::string& type, const std::string& name) {
    parser::SacmElement element;
    element.id = id;
    element.type = type;
    element.name = name;
    return element;
}

TwoModuleCase MakeTwoModuleCase() {
    TwoModuleCase mc;
    mc.ac.elements.push_back(MakeElement("G1", "claim", "Local top goal"));
    mc.ac.elements.push_back(MakeElement("G2", "claim", "Goal proved in the platform module"));
    mc.ac.elements.push_back(MakeElement("G3", "claim", "Second local goal"));
    mc.ac.elements.push_back(MakeElement("E1", "artifactreference", "Platform test report"));

    sacm::ArgumentPackage local;
    local.id = "AP_Local";
    local.name = "Vehicle";
    sacm::Claim g1;
    g1.id = "G1";
    g1.name = "Local top goal";
    local.claims.push_back(g1);
    sacm::Claim g3;
    g3.id = "G3";
    g3.name = "Second local goal";
    local.claims.push_back(g3);

    sacm::ArgumentPackage remote;
    remote.id = "AP_Remote";
    remote.name = "Platform";
    sacm::Claim g2;
    g2.id = "G2";
    g2.name = "Goal proved in the platform module";
    remote.claims.push_back(g2);
    sacm::ArtifactReference e1;
    e1.id = "E1";
    remote.artifactReferences.push_back(e1);

    mc.pkg.argumentPackages.push_back(local);
    mc.pkg.argumentPackages.push_back(remote);
    return mc;
}

const parser::SacmElement* Find(const parser::AssuranceCase& ac, const std::string& id) {
    for (const parser::SacmElement& element : ac.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

const sacm::Claim* FindClaim(const sacm::AssuranceCasePackage& pkg, const std::string& id) {
    for (const sacm::ArgumentPackage& ap : pkg.argumentPackages) {
        for (const sacm::Claim& claim : ap.claims) {
            if (claim.id == id)
                return &claim;
        }
    }
    return nullptr;
}

} // namespace

TEST(AwayGoalTest, GSN3_MOD_003_CitesAGoalInAnotherModuleAsACitation) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;

    ASSERT_TRUE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "G2", new_id, relationship_id, error)) << error;
    ASSERT_FALSE(new_id.empty());

    const parser::SacmElement* away = Find(mc.ac, new_id);
    ASSERT_NE(away, nullptr);
    EXPECT_EQ(away->type, "claim");
    EXPECT_TRUE(away->is_citation);
    EXPECT_EQ(away->cited_element_id, "G2");
    EXPECT_TRUE(core::IsAwayGoal(*away));
}

TEST(AwayGoalTest, GSN3_MOD_003_RecordsTheModuleTheGoalComesFrom) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;
    ASSERT_TRUE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "G2", new_id, relationship_id, error)) << error;

    const parser::SacmElement* away = Find(mc.ac, new_id);
    ASSERT_NE(away, nullptr);
    // GSN draws the source module's identifier in the away goal's bottom
    // compartment. Without it the reader cannot tell which module to go read.
    EXPECT_EQ(away->away_module_identifier, "Platform");
}

TEST(AwayGoalTest, GSN3_MOD_003_SupportsItsParentInSacmDirection) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;
    ASSERT_TRUE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "G2", new_id, relationship_id, error)) << error;

    const parser::SacmElement* relationship = Find(mc.ac, relationship_id);
    ASSERT_NE(relationship, nullptr);
    EXPECT_EQ(relationship->type, "assertedinference");
    // GSN SupportedBy runs conclusion to premise; SACM AssertedInference runs
    // premise to conclusion. The away goal is the premise.
    ASSERT_EQ(relationship->source_refs.size(), 1u);
    EXPECT_EQ(relationship->source_refs.front(), new_id);
    ASSERT_EQ(relationship->target_refs.size(), 1u);
    EXPECT_EQ(relationship->target_refs.front(), "G1");
    EXPECT_FALSE(relationship->is_counter);
}

TEST(AwayGoalTest, GSN3_MOD_003_CitationSurvivesIntoTheSacmPackage) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;
    ASSERT_TRUE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "G2", new_id, relationship_id, error)) << error;

    // The POD model is a projection. If the citation does not reach the SACM
    // package it is lost on save, and the away goal reloads as a local goal
    // with no support -- the argument silently changes meaning.
    const sacm::Claim* mirrored = FindClaim(mc.pkg, new_id);
    ASSERT_NE(mirrored, nullptr);
    EXPECT_TRUE(mirrored->isCitation);
    EXPECT_EQ(mirrored->citedElement, "G2");
}

TEST(AwayGoalTest, GSN3_MOD_003_DoesNotCopyTheCitedGoalStatement) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;
    ASSERT_TRUE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "G2", new_id, relationship_id, error)) << error;

    const parser::SacmElement* away = Find(mc.ac, new_id);
    ASSERT_NE(away, nullptr);
    // The away goal IS the cited goal read from here. Copying its statement
    // would create a second place the same claim is written, and the two would
    // drift apart the moment either is edited.
    EXPECT_TRUE(away->name.empty());
    EXPECT_TRUE(away->content.empty());
}

TEST(AwayGoalTest, GSN3_MOD_003_RefusesACitationInsideTheSameModule) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;

    // G3 is in the citing goal's own module, so this is not an away goal.
    EXPECT_FALSE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "G3", new_id, relationship_id, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(new_id.empty());
}

TEST(AwayGoalTest, GSN3_MOD_003_SameModuleCitationIsNotAnAwayGoal) {
    parser::SacmElement citation = MakeElement("G4", "claim", "");
    citation.is_citation = true;
    citation.cited_element_id = "G3";
    // No module resolved, because the cited goal is in the same package.
    EXPECT_FALSE(core::IsAwayGoal(citation));
}

TEST(AwayGoalTest, GSN3_MOD_003_RefusesCitingSomethingThatIsNotAGoal) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;

    EXPECT_FALSE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "E1", new_id, relationship_id, error));
    EXPECT_FALSE(error.empty());
}

TEST(AwayGoalTest, GSN3_MOD_003_RefusesAnUnresolvableCitation) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;

    EXPECT_FALSE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "GX", new_id, relationship_id, error));
    EXPECT_FALSE(error.empty());
}

TEST(AwayGoalTest, GSN3_MOD_003_RefusesAGoalCitingItself) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;

    EXPECT_FALSE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "G1", new_id, relationship_id, error));
    EXPECT_FALSE(error.empty());
}

TEST(AwayGoalTest, GSN3_MOD_003_RefusalLeavesBothModelsUnchanged) {
    TwoModuleCase mc = MakeTwoModuleCase();
    const size_t elements_before = mc.ac.elements.size();
    const size_t local_claims_before = mc.pkg.argumentPackages.front().claims.size();

    std::string new_id;
    std::string relationship_id;
    std::string error;
    EXPECT_FALSE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "G3", new_id, relationship_id, error));

    EXPECT_EQ(mc.ac.elements.size(), elements_before);
    EXPECT_EQ(mc.pkg.argumentPackages.front().claims.size(), local_claims_before);
}

TEST(AwayGoalTest, GSN3_MOD_003_ModuleResolutionNeedsPackageInformation) {
    TwoModuleCase mc = MakeTwoModuleCase();
    // With no package there is no module, so nothing can be away. Reporting a
    // module here would be inventing one.
    EXPECT_TRUE(core::ResolveAwayModuleIdentifier(nullptr, "G1", "G2").empty());
    EXPECT_EQ(core::ResolveAwayModuleIdentifier(&mc.pkg, "G1", "G2"), "Platform");
    EXPECT_TRUE(core::ResolveAwayModuleIdentifier(&mc.pkg, "G1", "G3").empty());
}

TEST(AwayGoalTest, GSN3_MOD_003_ReplayInstallsTheSameCitationWithGivenIds) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string error;

    ASSERT_TRUE(core::AddAwayGoalWithIds(mc.ac, &mc.pkg, "G1", "G2", "AG7", "R7", error)) << error;
    const parser::SacmElement* away = Find(mc.ac, "AG7");
    ASSERT_NE(away, nullptr);
    EXPECT_TRUE(away->is_citation);
    EXPECT_EQ(away->cited_element_id, "G2");
    EXPECT_NE(Find(mc.ac, "R7"), nullptr);
}

// --------------------------------------------------------------------------
// Choosing what to cite, and the command a menu click runs.
// --------------------------------------------------------------------------

TEST(AwayGoalTest, GSN3_MOD_003_OffersOnlyGoalsFromOtherModules) {
    TwoModuleCase mc = MakeTwoModuleCase();

    const std::vector<core::AwayGoalCandidate> candidates = core::ListAwayGoalCandidates(mc.ac, &mc.pkg, "G1");

    ASSERT_EQ(candidates.size(), 1u);
    EXPECT_EQ(candidates.front().id, "G2");
    EXPECT_EQ(candidates.front().module_identifier, "Platform");
    // G3 is a goal in the selection's own module and E1 is not a goal, so
    // neither may be cited; offering them would invite a refusal after a click.
    for (const core::AwayGoalCandidate& candidate : candidates) {
        EXPECT_NE(candidate.id, "G3");
        EXPECT_NE(candidate.id, "E1");
        EXPECT_NE(candidate.id, "G1");
    }
}

TEST(AwayGoalTest, GSN3_MOD_003_DoesNotOfferACitationOfACitation) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;
    ASSERT_TRUE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "G2", new_id, relationship_id, error)) << error;

    // The new away goal now sits in the local module. Seen from the remote
    // module it is a goal elsewhere, but citing it would point a reader at a
    // signpost rather than at the argument it points to.
    const std::vector<core::AwayGoalCandidate> candidates = core::ListAwayGoalCandidates(mc.ac, &mc.pkg, "G2");
    for (const core::AwayGoalCandidate& candidate : candidates) {
        EXPECT_NE(candidate.id, new_id);
    }
}

TEST(AwayGoalTest, GSN3_MOD_003_CommandCreatesTheCitationAndRecordsIt) {
    TwoModuleCase mc = MakeTwoModuleCase();
    core::commands::CommandContext ctx{mc.ac, mc.pkg};
    core::commands::CreateAwayGoalCommand cmd("G1", "G2");
    core::audit::AuditEvent event;
    std::string error;

    ASSERT_TRUE(cmd.Apply(ctx, event, error)) << error;
    ASSERT_FALSE(cmd.GeneratedId().empty());

    const parser::SacmElement* away = Find(mc.ac, cmd.GeneratedId());
    ASSERT_NE(away, nullptr);
    EXPECT_TRUE(away->is_citation);
    EXPECT_EQ(away->cited_element_id, "G2");

    // The audit payload has to carry what was cited. Replay that recreates the
    // goal without its citation recreates a different argument.
    EXPECT_EQ(event.event_type, "CreateAwayGoal");
    EXPECT_EQ(event.payload["parent_id"], "G1");
    EXPECT_EQ(event.payload["cited_id"], "G2");
    EXPECT_EQ(event.payload["generated_id"], cmd.GeneratedId());
}

TEST(AwayGoalTest, GSN3_MOD_003_CommandRefusesASameModuleCitation) {
    TwoModuleCase mc = MakeTwoModuleCase();
    const size_t elements_before = mc.ac.elements.size();

    core::commands::CommandContext ctx{mc.ac, mc.pkg};
    core::commands::CreateAwayGoalCommand cmd("G1", "G3");
    core::audit::AuditEvent event;
    std::string error;

    EXPECT_FALSE(cmd.Apply(ctx, event, error));
    EXPECT_FALSE(error.empty());
    // Refused before anything was created, so no bare goal is left behind
    // claiming support that was never cited.
    EXPECT_EQ(mc.ac.elements.size(), elements_before);
    EXPECT_TRUE(cmd.GeneratedId().empty());
}
