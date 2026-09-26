#include "core/element_factory.h"

#include "core/assurance_tree.h"
#include "core/audit/audit_event.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/problems/gsn_wellformedness.h"
#include "export/gsn_projection.h"
#include "export/svg_writer.h"
#include "legacy_sacm/sacm_model.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// GSN v3 Modular Extension: Away Assumption (GSN3-MOD-006) and Away
// Justification (GSN3-MOD-007).
//
// Both are citing Claims, like an Away Goal (GSN3-MOD-003), and differ from it
// only in declaration and attachment. That similarity is the hazard these tests
// guard: before the kinds were told apart, an imported away assumption was
// classified as an away goal, and the Away Goal menu offered assumptions and
// justifications as goals to cite.

namespace {

struct TwoModuleCase {
    parser::AssuranceCase ac;
    sacm::AssuranceCasePackage pkg;
};

parser::SacmElement MakeClaim(const std::string& id, const std::string& name, const std::string& declaration = {}) {
    parser::SacmElement element;
    element.id = id;
    element.type = "claim";
    element.name = name;
    element.assertion_declaration = declaration;
    return element;
}

sacm::Claim MirrorOf(const parser::SacmElement& element) {
    sacm::Claim claim;
    claim.id = element.id;
    claim.name = element.name;
    claim.assertionDeclaration = element.assertion_declaration;
    return claim;
}

// "Vehicle" owns the local goal G1 and a local assumption A1. "Platform" owns a
// goal G2, an assumption A2 and a justification J2 that the vehicle module may
// cite.
TwoModuleCase MakeTwoModuleCase() {
    TwoModuleCase mc;
    const std::vector<parser::SacmElement> local{
        MakeClaim("G1", "The vehicle is acceptably safe"),
        MakeClaim("A1", "The vehicle is driven on public roads", "assumed"),
    };
    const std::vector<parser::SacmElement> remote{
        MakeClaim("G2", "The platform is acceptably safe"),
        MakeClaim("A2", "Operators are trained", "assumed"),
        MakeClaim("J2", "Platform hazards were identified by HAZOP", "justification"),
    };

    sacm::ArgumentPackage vehicle;
    vehicle.id = "AP_Local";
    vehicle.name = "Vehicle";
    for (const parser::SacmElement& element : local) {
        mc.ac.elements.push_back(element);
        vehicle.claims.push_back(MirrorOf(element));
    }
    sacm::ArgumentPackage platform;
    platform.id = "AP_Remote";
    platform.name = "Platform";
    for (const parser::SacmElement& element : remote) {
        mc.ac.elements.push_back(element);
        platform.claims.push_back(MirrorOf(element));
    }
    mc.pkg.argumentPackages.push_back(vehicle);
    mc.pkg.argumentPackages.push_back(platform);
    return mc;
}

const parser::SacmElement* Find(const parser::AssuranceCase& ac, const std::string& id) {
    for (const parser::SacmElement& element : ac.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

std::vector<std::string> CandidateIds(const std::vector<core::AwayCandidate>& candidates) {
    std::vector<std::string> ids;
    for (const core::AwayCandidate& candidate : candidates)
        ids.push_back(candidate.id);
    return ids;
}

bool HasRule(const std::vector<core::GsnFinding>& findings, core::GsnRule rule, const std::string& element_id) {
    for (const core::GsnFinding& finding : findings) {
        if (finding.rule == rule && finding.element_id == element_id)
            return true;
    }
    return false;
}

// An imported Away Assumption, as another tool would write it: a citing Claim
// declared `assumed`, in context of a goal in its own module.
constexpr std::string_view kImportedAwayAssumption = R"(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmlns:S="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0">
  <S:AssuranceCasePackage xmi:id="acp_1">
    <name content="Imported away assumption"/>
    <argumentPackage xmi:id="ap_vehicle">
      <name content="Vehicle"/>
      <argumentElement xsi:type="S:Claim" xmi:id="G1">
        <name lang="en" content="The vehicle is acceptably safe."/>
      </argumentElement>
      <argumentElement xsi:type="S:Claim" xmi:id="AA1" assertionDeclaration="assumed" isCitation="true" citedElement="A2"/>
      <argumentElement xsi:type="S:AssertedContext" xmi:id="ctx_1" source="AA1" target="G1"/>
    </argumentPackage>
    <argumentPackage xmi:id="ap_platform">
      <name content="Platform"/>
      <argumentElement xsi:type="S:Claim" xmi:id="A2" assertionDeclaration="assumed">
        <name lang="en" content="Operators are trained."/>
      </argumentElement>
    </argumentPackage>
  </S:AssuranceCasePackage>
</xmi:XMI>
)";

} // namespace

// ---------------------------------------------------------------------------
// Classification
// ---------------------------------------------------------------------------

TEST(AwayAssumptionJustificationTest, GSN3_MOD_006_AnImportedAwayAssumptionIsNotAnAwayGoal) {
    sacm_adapter::LibraryDocument document;
    ASSERT_TRUE(sacm_adapter::reload_document(document, kImportedAwayAssumption));
    const parser::AssuranceCase projected = sacm_adapter::project_case(document);
    const parser::SacmElement* away = Find(projected, "AA1");
    ASSERT_NE(away, nullptr);

    EXPECT_EQ(core::AwayElementKindOf(*away), core::AwayElementKind::Assumption);
    EXPECT_TRUE(core::IsAwayElement(*away));
    EXPECT_FALSE(core::IsAwayGoal(*away)) << "an away assumption was classified as an away goal";
}

TEST(AwayAssumptionJustificationTest, GSN3_MOD_006_AnImportedAwayAssumptionIsDrawnAsAnAssumptionFromItsModule) {
    sacm_adapter::LibraryDocument document;
    ASSERT_TRUE(sacm_adapter::reload_document(document, kImportedAwayAssumption));
    const parser::AssuranceCase projected = sacm_adapter::project_case(document);

    const core::AssuranceTree tree = core::AssuranceTree::Build(projected, "");
    const core::TreeNode* node = core::FindTreeNode(tree, "AA1");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->role, core::NodeRole::Assumption);
    EXPECT_EQ(node->away_module_identifier, "Platform");
    EXPECT_NE(node->label.find("Operators are trained."), std::string::npos)
        << "the statement is read through the citation: " << node->label;
}

// ---------------------------------------------------------------------------
// Authoring
// ---------------------------------------------------------------------------

TEST(AwayAssumptionJustificationTest, GSN3_MOD_006_CitesAnAssumptionInAnotherModuleInContextOfTheGoal) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;
    ASSERT_TRUE(core::AddAwayElement(
        mc.ac, &mc.pkg, "G1", "A2", core::AwayElementKind::Assumption, new_id, relationship_id, error))
        << error;

    const parser::SacmElement* away = Find(mc.ac, new_id);
    ASSERT_NE(away, nullptr);
    EXPECT_EQ(away->type, "claim");
    EXPECT_EQ(away->assertion_declaration, "assumed");
    EXPECT_TRUE(away->is_citation);
    EXPECT_EQ(away->cited_element_id, "A2");
    EXPECT_EQ(away->away_module_identifier, "Platform");
    EXPECT_EQ(core::AwayElementKindOf(*away), core::AwayElementKind::Assumption);
    EXPECT_TRUE(away->name.empty()) << "the cited statement must not be copied";

    // InContextOf, in SACM direction: the assumption is the source.
    const parser::SacmElement* relationship = Find(mc.ac, relationship_id);
    ASSERT_NE(relationship, nullptr);
    EXPECT_EQ(relationship->type, "assertedcontext");
    EXPECT_EQ(relationship->source_refs, std::vector<std::string>{new_id});
    EXPECT_EQ(relationship->target_refs, std::vector<std::string>{"G1"});
}

TEST(AwayAssumptionJustificationTest, GSN3_MOD_007_CitesAJustificationInAnotherModuleInContextOfTheGoal) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;
    ASSERT_TRUE(core::AddAwayElement(
        mc.ac, &mc.pkg, "G1", "J2", core::AwayElementKind::Justification, new_id, relationship_id, error))
        << error;

    const parser::SacmElement* away = Find(mc.ac, new_id);
    ASSERT_NE(away, nullptr);
    EXPECT_EQ(away->assertion_declaration, "justification");
    EXPECT_EQ(away->cited_element_id, "J2");
    EXPECT_EQ(core::AwayElementKindOf(*away), core::AwayElementKind::Justification);
    const parser::SacmElement* relationship = Find(mc.ac, relationship_id);
    ASSERT_NE(relationship, nullptr);
    EXPECT_EQ(relationship->type, "assertedcontext");
}

TEST(AwayAssumptionJustificationTest, GSN3_MOD_006_RefusesCitingAnythingButAnAssumption) {
    TwoModuleCase mc = MakeTwoModuleCase();
    const std::size_t before = mc.ac.elements.size();
    std::string new_id;
    std::string relationship_id;
    std::string error;
    EXPECT_FALSE(core::AddAwayElement(
        mc.ac, &mc.pkg, "G1", "G2", core::AwayElementKind::Assumption, new_id, relationship_id, error));
    EXPECT_EQ(error, "An away assumption must cite an Assumption.");
    EXPECT_FALSE(core::AddAwayElement(
        mc.ac, &mc.pkg, "G1", "J2", core::AwayElementKind::Assumption, new_id, relationship_id, error));
    EXPECT_EQ(mc.ac.elements.size(), before) << "a refusal must leave the model unchanged";
}

TEST(AwayAssumptionJustificationTest, GSN3_MOD_006_RefusesAnAssumptionInTheSameModule) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;
    EXPECT_FALSE(core::AddAwayElement(
        mc.ac, &mc.pkg, "G1", "A1", core::AwayElementKind::Assumption, new_id, relationship_id, error));
    EXPECT_EQ(error, "An away assumption must cite an assumption in another module.");
}

// The defect this change found in the shipped Away Goal menu: every Claim in
// another module was offered, assumptions and justifications included, and
// choosing one drew a goal where the other module states only an assumption.
TEST(AwayAssumptionJustificationTest, GSN3_MOD_003_TheAwayGoalMenuOffersOnlyGoals) {
    TwoModuleCase mc = MakeTwoModuleCase();
    EXPECT_EQ(CandidateIds(core::ListAwayGoalCandidates(mc.ac, &mc.pkg, "G1")), std::vector<std::string>{"G2"});

    std::string new_id;
    std::string relationship_id;
    std::string error;
    EXPECT_FALSE(core::AddAwayGoal(mc.ac, &mc.pkg, "G1", "A2", new_id, relationship_id, error));
    EXPECT_EQ(error, "An away goal must cite a Goal.");
}

TEST(AwayAssumptionJustificationTest, GSN3_MOD_006_GSN3_MOD_007_EachMenuOffersOnlyItsOwnKind) {
    TwoModuleCase mc = MakeTwoModuleCase();
    EXPECT_EQ(CandidateIds(core::ListAwayElementCandidates(mc.ac, &mc.pkg, "G1", core::AwayElementKind::Assumption)),
              std::vector<std::string>{"A2"})
        << "the local assumption A1 is not away, and G2 and J2 are not assumptions";
    EXPECT_EQ(CandidateIds(core::ListAwayElementCandidates(mc.ac, &mc.pkg, "G1", core::AwayElementKind::Justification)),
              std::vector<std::string>{"J2"});
}

// An assumption is context to a Goal or a Strategy. It cannot be put in context
// of another assumption -- the Core rule a local assumption answers to.
TEST(AwayAssumptionJustificationTest, GSN3_MOD_006_AnswersToTheCoreContextRules) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string error;
    EXPECT_FALSE(core::CanAddAwayElement(mc.ac, &mc.pkg, "A1", "A2", core::AwayElementKind::Assumption, error));
    EXPECT_FALSE(error.empty());
}

TEST(AwayAssumptionJustificationTest, GSN3_MOD_006_CommandRecordsTheKindForReplay) {
    TwoModuleCase mc = MakeTwoModuleCase();
    core::commands::CommandContext ctx{mc.ac, mc.pkg};
    core::commands::CreateAwayElementCommand cmd("G1", "A2", core::AwayElementKind::Assumption);
    core::audit::AuditEvent event;
    std::string error;
    ASSERT_TRUE(cmd.Apply(ctx, event, error)) << error;

    EXPECT_EQ(event.event_type, "CreateAwayElement");
    EXPECT_EQ(event.payload["kind"], "assumption");
    EXPECT_EQ(event.payload["parent_id"], "G1");
    EXPECT_EQ(event.payload["cited_id"], "A2");
    EXPECT_EQ(event.payload["generated_id"], cmd.GeneratedId());
}

// The Away Goal event must stay byte-for-byte what it was, or audit logs
// written before the other kinds existed would stop verifying.
TEST(AwayAssumptionJustificationTest, GSN3_MOD_003_AnAwayGoalIsStillRecordedWithoutAKind) {
    TwoModuleCase mc = MakeTwoModuleCase();
    core::commands::CommandContext ctx{mc.ac, mc.pkg};
    core::commands::CreateAwayElementCommand cmd("G1", "G2", core::AwayElementKind::Goal);
    core::audit::AuditEvent event;
    std::string error;
    ASSERT_TRUE(cmd.Apply(ctx, event, error)) << error;
    EXPECT_EQ(event.event_type, "CreateAwayGoal");
    EXPECT_FALSE(event.payload.contains("kind"));
}

// An away goal under a strategy joins the strategy's single inference exactly as
// a local sub-goal does. Before, it got an inference of its own whose target was
// the strategy -- a different graph from the library path's -- and could not
// replay the empty relationship id an extending sub-goal records.
TEST(AwayAssumptionJustificationTest, GSN3_MOD_003_AnAwayGoalUnderAStrategyJoinsItsInference) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string strategy_id;
    std::string error;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1", core::NewElementKind::Strategy, strategy_id, error))
        << error;

    std::string local_id;
    std::string materialized_id;
    ASSERT_TRUE(core::AddChildElement(
        mc.ac, &mc.pkg, strategy_id, core::NewElementKind::Goal, local_id, materialized_id, error))
        << error;
    ASSERT_FALSE(materialized_id.empty());

    std::string away_id;
    std::string away_relationship_id;
    ASSERT_TRUE(core::AddAwayGoal(mc.ac, &mc.pkg, strategy_id, "G2", away_id, away_relationship_id, error)) << error;
    EXPECT_TRUE(away_relationship_id.empty()) << "extending the inference creates no relationship";

    int inferences_of_strategy = 0;
    for (const parser::SacmElement& element : mc.ac.elements) {
        if (element.type != "assertedinference")
            continue;
        EXPECT_NE(element.target_refs, std::vector<std::string>{strategy_id}) << "no inference may target a strategy";
        if (element.reasoning_ref == strategy_id) {
            ++inferences_of_strategy;
            EXPECT_EQ(element.target_refs, std::vector<std::string>{"G1"});
            EXPECT_EQ(element.source_refs, (std::vector<std::string>{local_id, away_id}));
        }
    }
    EXPECT_EQ(inferences_of_strategy, 1);

    const parser::SacmElement* away = Find(mc.ac, away_id);
    ASSERT_NE(away, nullptr);
    EXPECT_TRUE(away->is_citation);
    EXPECT_EQ(away->cited_element_id, "G2");
    EXPECT_TRUE(core::IsAwayGoal(*away));

    // Replaying the recorded event: an empty relationship id is what the live
    // path wrote, so the replay entry point must accept it.
    TwoModuleCase replay = MakeTwoModuleCase();
    ASSERT_TRUE(core::AddChildElementWithIds(
        replay.ac, &replay.pkg, "G1", core::NewElementKind::Strategy, strategy_id, "", error))
        << error;
    ASSERT_TRUE(core::AddChildElementWithIds(
        replay.ac, &replay.pkg, strategy_id, core::NewElementKind::Goal, local_id, materialized_id, error))
        << error;
    EXPECT_TRUE(core::AddAwayGoalWithIds(replay.ac, &replay.pkg, strategy_id, "G2", away_id, "", error)) << error;
}

// The legacy replay entry point accepts any cited claim, as the menu that wrote
// `CreateAwayGoal` events did; the live and kind-aware paths do not.
TEST(AwayAssumptionJustificationTest, GSN3_MOD_003_OnlyTheLegacyReplayAcceptsAnAwayGoalCitingAnAssumption) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string error;
    EXPECT_FALSE(
        core::AddAwayElementWithIds(mc.ac, &mc.pkg, "G1", "A2", core::AwayElementKind::Goal, "AG1", "R1", error));
    EXPECT_EQ(error, "An away goal must cite a Goal.");
    EXPECT_TRUE(core::AddAwayGoalWithIds(mc.ac, &mc.pkg, "G1", "A2", "AG1", "R1", error)) << error;
    EXPECT_FALSE(core::AddAwayGoalWithIds(mc.ac, &mc.pkg, "G1", "G1", "AG2", "R2", error))
        << "the other citation rules still apply";
}

// ---------------------------------------------------------------------------
// Rendering: the SVG export is a separate renderer from the canvas.
// ---------------------------------------------------------------------------

TEST(AwayAssumptionJustificationTest, GSN3_MOD_006_SvgExportDrawsAnAssumptionWithItsModule) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;
    ASSERT_TRUE(core::AddAwayElement(
        mc.ac, &mc.pkg, "G1", "A2", core::AwayElementKind::Assumption, new_id, relationship_id, error))
        << error;

    const export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(mc.ac);
    const export_gsn::GsnNode* exported = nullptr;
    for (const export_gsn::GsnNode& node : projection.diagram.nodes) {
        if (node.display_id == new_id || node.id == new_id) {
            exported = &node;
            break;
        }
    }
    ASSERT_NE(exported, nullptr);
    EXPECT_EQ(exported->kind, export_gsn::GsnNodeKind::Assumption);
    EXPECT_EQ(exported->away_module_identifier, "Platform");
    EXPECT_EQ(exported->title, "Operators are trained");

    const std::string svg = export_gsn::GenerateGsnSvg(projection.diagram);
    EXPECT_NE(svg.find(">Platform<"), std::string::npos);
}

TEST(AwayAssumptionJustificationTest, GSN3_MOD_007_BothRenderersDrawAJustificationWithItsModule) {
    TwoModuleCase mc = MakeTwoModuleCase();
    std::string new_id;
    std::string relationship_id;
    std::string error;
    ASSERT_TRUE(core::AddAwayElement(
        mc.ac, &mc.pkg, "G1", "J2", core::AwayElementKind::Justification, new_id, relationship_id, error))
        << error;

    const core::AssuranceTree tree = core::AssuranceTree::Build(mc.ac, "");
    const core::TreeNode* node = core::FindTreeNode(tree, new_id);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->role, core::NodeRole::Justification);
    EXPECT_EQ(node->away_module_identifier, "Platform");
    EXPECT_NE(node->label.find("Platform hazards were identified by HAZOP"), std::string::npos);

    const export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(mc.ac);
    const export_gsn::GsnNode* exported = nullptr;
    for (const export_gsn::GsnNode& candidate : projection.diagram.nodes) {
        if (candidate.display_id == new_id || candidate.id == new_id) {
            exported = &candidate;
            break;
        }
    }
    ASSERT_NE(exported, nullptr);
    EXPECT_EQ(exported->kind, export_gsn::GsnNodeKind::Justification);
    EXPECT_EQ(exported->away_module_identifier, "Platform");
    EXPECT_EQ(exported->title, "Platform hazards were identified by HAZOP");
}

// ---------------------------------------------------------------------------
// Validation: each finding names the requirement it enforces.
// ---------------------------------------------------------------------------

TEST(AwayAssumptionJustificationTest, GSN3_MOD_006_ReportsAnUnresolvedAssumptionCitationUnderItsOwnRequirement) {
    TwoModuleCase mc = MakeTwoModuleCase();
    parser::SacmElement away = MakeClaim("AA1", "", "assumed");
    away.is_citation = true;
    away.cited_element_id = "A_MISSING";
    mc.ac.elements.push_back(away);

    const std::vector<core::GsnFinding> findings = core::CheckGsnWellFormedness(mc.ac);
    ASSERT_TRUE(HasRule(findings, core::GsnRule::AwayAssumptionCitationUnresolved, "AA1"));
    EXPECT_FALSE(HasRule(findings, core::GsnRule::AwayGoalCitationUnresolved, "AA1"));
    EXPECT_STREQ(core::GsnRequirementId(core::GsnRule::AwayAssumptionCitationUnresolved), "GSN3-MOD-006");
}

TEST(AwayAssumptionJustificationTest, GSN3_MOD_007_ReportsAnUnresolvedJustificationCitationUnderItsOwnRequirement) {
    TwoModuleCase mc = MakeTwoModuleCase();
    parser::SacmElement away = MakeClaim("AJ1", "", "justification");
    away.is_citation = true;
    away.cited_element_id = "J_MISSING";
    mc.ac.elements.push_back(away);

    const std::vector<core::GsnFinding> findings = core::CheckGsnWellFormedness(mc.ac);
    ASSERT_TRUE(HasRule(findings, core::GsnRule::AwayJustificationCitationUnresolved, "AJ1"));
    EXPECT_STREQ(core::GsnRequirementId(core::GsnRule::AwayJustificationCitationUnresolved), "GSN3-MOD-007");
}
