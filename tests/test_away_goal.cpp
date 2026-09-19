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
#include "app/structure_problem_sync.h"
#include "core/problems/problems_manager.h"
#include "core/assurance_tree.h"
#include "core/library_package_projection.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"
#include "core/problems/gsn_wellformedness.h"
#include "export/gsn_projection.h"
#include "export/svg_writer.h"
#include "export/gsn_svg_layout.h"
#include "core/commands/element_commands.h"
#include "core/commands/command_bus.h"
#include "core/audit/audit_event.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_model.h"

#include <gtest/gtest.h>
#include <memory>
#include <string_view>

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

// --------------------------------------------------------------------------
// Rendering. The canvas and the SVG export are separate renderers with
// separate models, so each is checked on its own: a decorator added to one is
// silently absent from the other.
// --------------------------------------------------------------------------

TEST(AwayGoalTest, GSN3_MOD_003_CanvasNodeCarriesTheModuleAndTheCitedStatement) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;
    ASSERT_TRUE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "G2", new_id, relationship_id, error)) << error;

    const core::AssuranceTree tree = core::AssuranceTree::Build(mc.ac, "");
    const core::TreeNode* away = nullptr;
    for (const std::unique_ptr<core::TreeNode>& node : tree.nodes) {
        if (node->id == new_id) {
            away = node.get();
            break;
        }
    }
    ASSERT_NE(away, nullptr);
    EXPECT_EQ(away->away_module_identifier, "Platform");
    // The away goal holds no statement of its own, so the label has to come
    // through the citation. A blank node here would be an argument step the
    // reader cannot read.
    EXPECT_NE(away->label.find("Goal proved in the platform module"), std::string::npos);
}

TEST(AwayGoalTest, GSN3_MOD_003_SvgExportDrawsTheModuleCompartment) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;
    ASSERT_TRUE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "G2", new_id, relationship_id, error)) << error;

    const export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(mc.ac);
    const export_gsn::GsnNode* exported = nullptr;
    for (const export_gsn::GsnNode& node : projection.diagram.nodes) {
        if (node.display_id == new_id || node.id == new_id) {
            exported = &node;
            break;
        }
    }
    ASSERT_NE(exported, nullptr);
    EXPECT_EQ(exported->away_module_identifier, "Platform");
    // Resolved through the citation here too. The export projection is a
    // separate model from the canvas, so it can lose this on its own.
    EXPECT_EQ(exported->title, "Goal proved in the platform module");

    const std::string svg = export_gsn::GenerateGsnSvg(projection.diagram);
    EXPECT_NE(svg.find("gsn-away-module-divider"), std::string::npos);
    EXPECT_NE(svg.find(">Platform<"), std::string::npos);
}

TEST(AwayGoalTest, GSN3_MOD_003_SvgExportLeavesOrdinaryGoalsUndecorated) {
    TwoModuleCase mc = MakeTwoModuleCase();

    const export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(mc.ac);
    const std::string svg = export_gsn::GenerateGsnSvg(projection.diagram);

    // Nothing here is away, so the compartment must not appear. Drawing it on
    // an ordinary goal would claim its support lives in another module.
    EXPECT_EQ(svg.find("gsn-away-module-divider"), std::string::npos);
}

// --------------------------------------------------------------------------
// Validation. Two things go wrong with an away goal that no Core rule covers:
// the citation points at nothing, or the goal is also argued here.
// --------------------------------------------------------------------------

namespace {

bool HasRule(const std::vector<core::GsnFinding>& findings, core::GsnRule rule, const std::string& element_id) {
    for (const core::GsnFinding& finding : findings) {
        if (finding.rule == rule && finding.element_id == element_id)
            return true;
    }
    return false;
}

parser::SacmElement MakeSupport(const std::string& id, const std::string& source, const std::string& target) {
    parser::SacmElement relationship = MakeElement(id, "assertedinference", "");
    relationship.source_refs.push_back(source);
    relationship.target_refs.push_back(target);
    return relationship;
}

} // namespace

TEST(AwayGoalTest, GSN3_MOD_003_ReportsACitationThatResolvesToNothing) {
    TwoModuleCase mc = MakeTwoModuleCase();
    parser::SacmElement away = MakeElement("AG1", "claim", "");
    away.is_citation = true;
    away.cited_element_id = "G_MISSING";
    mc.ac.elements.push_back(away);
    mc.ac.elements.push_back(MakeSupport("R1", "AG1", "G1"));

    const std::vector<core::GsnFinding> findings = core::CheckGsnWellFormedness(mc.ac);
    ASSERT_TRUE(HasRule(findings, core::GsnRule::AwayGoalCitationUnresolved, "AG1"));
    for (const core::GsnFinding& finding : findings) {
        if (finding.rule == core::GsnRule::AwayGoalCitationUnresolved) {
            // The diagnostic names the requirement it enforces, so a reader can
            // check the tool against the standard instead of trusting it.
            EXPECT_STREQ(core::GsnRequirementId(finding.rule), "GSN3-MOD-003");
            EXPECT_EQ(finding.detail, "G_MISSING");
        }
    }
}

TEST(AwayGoalTest, GSN3_MOD_003_ReportsAnAwayGoalThatIsAlsoArguedHere) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;
    ASSERT_TRUE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "G2", new_id, relationship_id, error)) << error;

    // Something in this module now supports the away goal, which is the module
    // claiming to prove what it said another module proves.
    mc.ac.elements.push_back(MakeElement("Sn1", "artifactreference", "Local report"));
    mc.ac.elements.push_back(MakeSupport("R9", "Sn1", new_id));

    const std::vector<core::GsnFinding> findings = core::CheckGsnWellFormedness(mc.ac);
    EXPECT_TRUE(HasRule(findings, core::GsnRule::AwayGoalDevelopedLocally, new_id));
}

TEST(AwayGoalTest, GSN3_MOD_003_SaysNothingAboutAWellFormedAwayGoal) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;
    ASSERT_TRUE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "G2", new_id, relationship_id, error)) << error;

    // A check that never fires on good input is not evidence that it works, so
    // this is the companion of the two above rather than a formality.
    const std::vector<core::GsnFinding> findings = core::CheckGsnWellFormedness(mc.ac);
    for (const core::GsnFinding& finding : findings) {
        EXPECT_NE(finding.rule, core::GsnRule::AwayGoalCitationUnresolved);
        EXPECT_NE(finding.rule, core::GsnRule::AwayGoalDevelopedLocally);
    }
}

// --------------------------------------------------------------------------
// Round trip. The flat POD model is rebuilt into the legacy package on every
// save and every canonical hash, so a citation dropped anywhere on that path
// turns an away goal back into an ordinary local goal with no warning.
// --------------------------------------------------------------------------

namespace {

// Two argument packages in one case. AG1 in "Vehicle" cites G2 in "Platform",
// so it is an away goal; C1 in "Vehicle" cites G1 in its own package, so it is
// a citation but not an away one.
constexpr std::string_view kTwoModuleCitationCase = R"(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmlns:S="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0">
  <S:AssuranceCasePackage xmi:id="acp_1">
    <name content="Two module case"/>
    <argumentPackage xmi:id="ap_vehicle">
      <name content="Vehicle"/>
      <argumentElement xsi:type="S:Claim" xmi:id="G1">
        <name lang="en" content="The vehicle is acceptably safe."/>
      </argumentElement>
      <argumentElement xsi:type="S:Claim" xmi:id="AG1" isCitation="true" citedElement="G2"/>
      <argumentElement xsi:type="S:Claim" xmi:id="C1" isCitation="true" citedElement="G1"/>
      <argumentElement xsi:type="S:AssertedInference" xmi:id="inf_1" source="AG1" target="G1"/>
    </argumentPackage>
    <argumentPackage xmi:id="ap_platform">
      <name content="Platform"/>
      <argumentElement xsi:type="S:Claim" xmi:id="G2">
        <name lang="en" content="The platform is acceptably safe."/>
      </argumentElement>
    </argumentPackage>
  </S:AssuranceCasePackage>
</xmi:XMI>
)";

const parser::SacmElement& RequireProjected(const parser::AssuranceCase& ac, const std::string& id) {
    const parser::SacmElement* element = Find(ac, id);
    if (element == nullptr) {
        ADD_FAILURE() << "element " << id << " is missing from the projection";
        static const parser::SacmElement empty;
        return empty;
    }
    return *element;
}

} // namespace

TEST(AwayGoalTest, GSN3_MOD_003_LoadResolvesOnlyTheCrossModuleCitationAsAway) {
    sacm_adapter::LibraryDocument document;
    ASSERT_TRUE(sacm_adapter::reload_document(document, kTwoModuleCitationCase));

    const parser::AssuranceCase projected = sacm_adapter::project_case(document);
    const parser::SacmElement& away = RequireProjected(projected, "AG1");
    EXPECT_TRUE(away.is_citation);
    EXPECT_EQ(away.cited_element_id, "G2");
    EXPECT_EQ(away.away_module_identifier, "Platform");
    EXPECT_TRUE(core::IsAwayGoal(away));

    // A citation inside one module is still a citation, but not an away goal.
    const parser::SacmElement& local = RequireProjected(projected, "C1");
    EXPECT_TRUE(local.is_citation);
    EXPECT_TRUE(local.away_module_identifier.empty());
    EXPECT_FALSE(core::IsAwayGoal(local));
}

TEST(AwayGoalTest, GSN3_MOD_003_AwayGoalSurvivesSaveAndReload) {
    sacm_adapter::LibraryDocument document;
    ASSERT_TRUE(sacm_adapter::reload_document(document, kTwoModuleCitationCase));

    // The application's save: the library document written as SACM XMI and
    // read back. The module is re-resolved from the reloaded packages, so this
    // also checks that the two modules are still two.
    const sacm_adapter::SaveOutcome saved = sacm_adapter::save_document(document);
    ASSERT_TRUE(saved.ok);
    sacm_adapter::LibraryDocument reloaded;
    ASSERT_TRUE(sacm_adapter::reload_document(reloaded, saved.xml));

    const parser::AssuranceCase after = sacm_adapter::project_case(reloaded);
    const parser::SacmElement& away = RequireProjected(after, "AG1");
    EXPECT_TRUE(away.is_citation);
    EXPECT_EQ(away.cited_element_id, "G2");
    EXPECT_EQ(away.away_module_identifier, "Platform");
}

TEST(AwayGoalTest, GSN3_MOD_003_ApplicationPackageKeepsTheCitationInItsModule) {
    sacm_adapter::LibraryDocument document;
    ASSERT_TRUE(sacm_adapter::reload_document(document, kTwoModuleCitationCase));

    // The package the application keeps beside the library document, and saves
    // from on the compatibility path. It is rebuilt from the flat model one
    // package at a time; the claim copier it uses dropped the citation, so an
    // away goal came back an ordinary local goal.
    const sacm::AssuranceCasePackage package = core::project_library_package_with_tags(document);
    const sacm::ArgumentPackage* vehicle = nullptr;
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        if (argument_package.id == "ap_vehicle")
            vehicle = &argument_package;
    }
    ASSERT_NE(vehicle, nullptr);

    const sacm::Claim* away = nullptr;
    for (const sacm::Claim& claim : vehicle->claims) {
        if (claim.id == "AG1")
            away = &claim;
    }
    ASSERT_NE(away, nullptr) << "the away goal must stay in the module that cites";
    EXPECT_TRUE(away->isCitation);
    EXPECT_EQ(away->citedElement, "G2");
}

TEST(AwayGoalTest, GSN3_MOD_003_AuditProjectionCarriesTheCitation) {
    sacm_adapter::LibraryDocument document;
    ASSERT_TRUE(sacm_adapter::reload_document(document, kTwoModuleCitationCase));

    // The audit projection collapses packages by design -- it only has to agree
    // with itself on both sides of the canonical hash -- so the module is not
    // expected to survive it. The citation is: a hash that ignored it could not
    // tell an away goal from a local one, and verification would pass over a
    // change in what the argument rests on.
    const sacm::AssuranceCasePackage package = core::project_library_package(document);
    const sacm::Claim* away = FindClaim(package, "AG1");
    ASSERT_NE(away, nullptr);
    EXPECT_TRUE(away->isCitation);
    EXPECT_EQ(away->citedElement, "G2");
}

// --------------------------------------------------------------------------
// Review follow-ups: the exported shape has room for its compartment, and a
// document rooted at bare argument packages still resolves its modules.
// --------------------------------------------------------------------------

TEST(AwayGoalTest, GSN3_MOD_003_SvgLayoutMakesRoomForTheModuleCompartment) {
    // Two identical goals, one of them away. The away one must be taller by the
    // compartment, or its statement's last line runs under the divider.
    export_gsn::GsnDiagram diagram;
    export_gsn::GsnNode local;
    local.id = "G1";
    local.display_id = "G1";
    local.kind = export_gsn::GsnNodeKind::Goal;
    local.title = "A goal whose statement is long enough to need several wrapped lines of text in the box";
    export_gsn::GsnNode away = local;
    away.id = "AG1";
    away.display_id = "AG1";
    away.away_module_identifier = "Platform";
    diagram.nodes.push_back(local);
    diagram.nodes.push_back(away);

    export_gsn::LayoutGsnSvgDiagram(diagram);

    double local_height = 0.0;
    double away_height = 0.0;
    for (const export_gsn::GsnNode& node : diagram.nodes) {
        if (node.id == "G1")
            local_height = node.height;
        if (node.id == "AG1")
            away_height = node.height;
    }
    ASSERT_GT(local_height, 0.0);
    EXPECT_GE(away_height, local_height + export_gsn::kAwayModuleCompartmentHeight);
}

namespace {

// Two bare ArgumentPackages as interchange roots, which SACM clause 2 permits.
// They arrive in other_roots() rather than roots().
constexpr std::string_view kBarePackageRootsCase = R"(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmlns:S="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0">
  <S:ArgumentPackage xmi:id="ap_vehicle">
    <name content="Vehicle"/>
    <argumentElement xsi:type="S:Claim" xmi:id="G1">
      <name lang="en" content="The vehicle is acceptably safe."/>
    </argumentElement>
    <argumentElement xsi:type="S:Claim" xmi:id="AG1" isCitation="true" citedElement="G2"/>
  </S:ArgumentPackage>
  <S:ArgumentPackage xmi:id="ap_platform">
    <name content="Platform"/>
    <argumentElement xsi:type="S:Claim" xmi:id="G2">
      <name lang="en" content="The platform is acceptably safe."/>
    </argumentElement>
  </S:ArgumentPackage>
</xmi:XMI>
)";

} // namespace

TEST(AwayGoalTest, GSN3_MOD_003_ResolvesModulesInADocumentRootedAtBarePackages) {
    sacm_adapter::LibraryDocument document;
    ASSERT_TRUE(sacm_adapter::reload_document(document, kBarePackageRootsCase));

    const parser::AssuranceCase projected = sacm_adapter::project_case(document);
    const parser::SacmElement& away = RequireProjected(projected, "AG1");
    EXPECT_TRUE(away.is_citation);
    EXPECT_EQ(away.away_module_identifier, "Platform");
}

// --------------------------------------------------------------------------
// GSN v2 Annex B, which the v3 change list leaves unchanged on both points:
// an away goal is decomposed only in the module it comes from, and a module
// identifier must identify one module.
// --------------------------------------------------------------------------

TEST(AwayGoalTest, GSN3_MOD_003_DevelopingAnAwayGoalLocallyIsAnError) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;
    ASSERT_TRUE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "G2", new_id, relationship_id, error)) << error;
    mc.ac.elements.push_back(MakeElement("Sn1", "artifactreference", "Local report"));
    mc.ac.elements.push_back(MakeSupport("R9", "Sn1", new_id));

    core::ProblemsManager problems;
    app::SyncStructureProblems(problems, &mc.ac);

    const core::ProblemItem* found = nullptr;
    for (const core::ProblemItem& problem : problems.GetProblems()) {
        if (problem.type == "AwayGoalDevelopedLocally")
            found = &problem;
    }
    ASSERT_NE(found, nullptr);
    // "Away goals cannot be (hierarchically) decomposed and further supported
    // by sub-elements within the current argument module" is a prohibition, so
    // it is reported as a violation of the notation, not as advice.
    EXPECT_EQ(found->severity, core::ProblemSeverity::Error);
    EXPECT_EQ(found->guideline_id, "GSN3-MOD-003");
    EXPECT_EQ(found->element_id, new_id);
}

TEST(AwayGoalTest, GSN3_MOD_003_SharedModuleNameFallsBackToThePackageId) {
    TwoModuleCase mc = MakeTwoModuleCase();
    // Both packages now answer to "Vehicle", so the name identifies neither.
    mc.pkg.argumentPackages.back().name = "Vehicle";

    EXPECT_EQ(core::ResolveAwayModuleIdentifier(&mc.pkg, "G1", "G2"), "AP_Remote");
}

namespace {

// Two packages that share a name, which SACM permits and GSN's module
// identifier rule does not.
constexpr std::string_view kSharedModuleNameCase = R"(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmlns:S="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0">
  <S:AssuranceCasePackage xmi:id="acp_1">
    <name content="Shared names"/>
    <argumentPackage xmi:id="ap_vehicle">
      <name content="Safety"/>
      <argumentElement xsi:type="S:Claim" xmi:id="G1">
        <name lang="en" content="The vehicle is acceptably safe."/>
      </argumentElement>
      <argumentElement xsi:type="S:Claim" xmi:id="AG1" isCitation="true" citedElement="G2"/>
      <argumentElement xsi:type="S:AssertedInference" xmi:id="inf_1" source="AG1" target="G1"/>
    </argumentPackage>
    <argumentPackage xmi:id="ap_platform">
      <name content="Safety"/>
      <argumentElement xsi:type="S:Claim" xmi:id="G2">
        <name lang="en" content="The platform is acceptably safe."/>
      </argumentElement>
    </argumentPackage>
  </S:AssuranceCasePackage>
</xmi:XMI>
)";

} // namespace

TEST(AwayGoalTest, GSN3_MOD_003_LoadedModulesWithASharedNameAreNamedByPackageId) {
    sacm_adapter::LibraryDocument document;
    ASSERT_TRUE(sacm_adapter::reload_document(document, kSharedModuleNameCase));

    const parser::AssuranceCase projected = sacm_adapter::project_case(document);
    const parser::SacmElement& away = RequireProjected(projected, "AG1");
    // "Safety" would send a reader to either module; the package id sends them
    // to the one the goal actually comes from.
    EXPECT_EQ(away.away_module_identifier, "ap_platform");
}
