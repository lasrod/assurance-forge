#include "export/gsn_projection.h"
#include "export/gsn_svg_exporter.h"
#include "export/gsn_svg_layout.h"
#include "export/svg_writer.h"
#include "core/terminology_package_service.h"
#include "parser/xml_parser.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(std::filesystem::path value) : path(std::move(value)) {}
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::filesystem::path MakeTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("assurance_forge_gsn_svg_export_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

parser::SacmElement Element(std::string id, std::string type, std::string name, std::string text = {}) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = std::move(type);
    element.name = std::move(name);
    if (element.type == "claim" || element.type == "argumentreasoning") {
        element.content = std::move(text);
    } else {
        element.description = std::move(text);
    }
    return element;
}

parser::SacmElement Relationship(std::string id,
                                 std::string type,
                                 std::vector<std::string> sources,
                                 std::vector<std::string> targets,
                                 std::string reasoning = {}) {
    parser::SacmElement relationship;
    relationship.id = std::move(id);
    relationship.type = std::move(type);
    relationship.source_refs = std::move(sources);
    relationship.target_refs = std::move(targets);
    relationship.reasoning_ref = std::move(reasoning);
    return relationship;
}

parser::AssuranceCase BuildRepresentativeCase() {
    parser::AssuranceCase model;
    model.id = "case-1";
    model.name = "Representative Case";

    parser::SacmElement goal = Element("G1", "claim", "Top goal", "System is acceptably safe.");
    parser::SacmElement sub_goal = Element("G2", "claim", "Sub goal", "Hazards are mitigated.");
    parser::SacmElement strategy = Element("S1", "argumentreasoning", "Hazard strategy", "Argument over hazards.");
    parser::SacmElement solution = Element("Sn1", "artifactreference", "Test report", "Verification evidence.");
    parser::SacmElement context =
        Element("C1", "artifactreference", "Operational context", "Defined operating domain.");
    parser::SacmElement assumption = Element("A1", "claim", "Hazard assumption", "All hazards are identified.");
    assumption.assertion_declaration = "assumed";
    parser::SacmElement justification =
        Element("J1", "claim", "Decomposition justification", "Decomposition is appropriate.");
    justification.assertion_declaration = "justification";

    model.elements = {goal,
                      sub_goal,
                      strategy,
                      solution,
                      context,
                      assumption,
                      justification,
                      Relationship("inf1", "assertedinference", {"G2"}, {"G1"}, "S1"),
                      Relationship("ev1", "assertedevidence", {"Sn1"}, {"G2"}),
                      Relationship("ctx1", "assertedcontext", {"C1", "A1", "J1"}, {"G1"})};
    return model;
}

parser::SacmElement Challenge(std::string id,
                              std::string type,
                              std::vector<std::string> sources,
                              std::vector<std::string> targets) {
    parser::SacmElement relationship = Relationship(std::move(id), std::move(type), std::move(sources),
                                                    std::move(targets));
    relationship.is_counter = true;
    return relationship;
}

parser::AcpRecord Acp(std::string id, std::string target_kind, std::string target_id) {
    parser::AcpRecord acp;
    acp.id = std::move(id);
    acp.target_kind = std::move(target_kind);
    acp.target_id = std::move(target_id);
    return acp;
}

const export_gsn::GsnEdge* FindEdgeBetween(const export_gsn::GsnDiagram& diagram,
                                           const std::string& from_id,
                                           const std::string& to_id) {
    for (const export_gsn::GsnEdge& edge : diagram.edges) {
        if (edge.from_id == from_id && edge.to_id == to_id)
            return &edge;
    }
    return nullptr;
}

size_t CountEdgesOfKind(const export_gsn::GsnDiagram& diagram, export_gsn::GsnEdgeKind kind) {
    size_t count = 0;
    for (const export_gsn::GsnEdge& edge : diagram.edges) {
        if (edge.kind == kind)
            ++count;
    }
    return count;
}

const export_gsn::GsnNode* FindNode(const export_gsn::GsnDiagram& diagram, const std::string& id) {
    for (const export_gsn::GsnNode& node : diagram.nodes) {
        if (node.id == id)
            return &node;
    }
    return nullptr;
}

bool RectanglesOverlap(const export_gsn::GsnNode& first, const export_gsn::GsnNode& second) {
    const double first_right = first.x + first.width;
    const double first_bottom = first.y + first.height;
    const double second_right = second.x + second.width;
    const double second_bottom = second.y + second.height;
    return first.x < second_right && first_right > second.x && first.y < second_bottom && first_bottom > second.y;
}

} // namespace

TEST(GsnSvgExporterTest, EnsureExportsFolderCreatesDirectory) {
    TempDir temp(MakeTempDir());
    std::string error;

    std::filesystem::path exports_dir = export_gsn::EnsureExportsFolder(temp.path, error);

    EXPECT_TRUE(error.empty()) << error;
    EXPECT_EQ(exports_dir, temp.path / "exports");
    EXPECT_TRUE(std::filesystem::is_directory(exports_dir));
}

TEST(GsnSvgExporterTest, UniqueExportPathDoesNotOverwriteExistingFiles) {
    TempDir temp(MakeTempDir());
    std::filesystem::create_directories(temp.path / "exports");
    std::ofstream(temp.path / "exports" / "main_gsn.svg") << "first";
    std::ofstream(temp.path / "exports" / "main_gsn_001.svg") << "second";

    std::filesystem::path output = export_gsn::MakeGsnSvgExportPath(temp.path / "exports", "main");

    EXPECT_EQ(output.filename().string(), "main_gsn_002.svg");
}

TEST(GsnSvgExporterTest, ProjectionMapsCoreNodesAndRelationships) {
    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(BuildRepresentativeCase());

    ASSERT_EQ(projection.diagram.nodes.size(), 7u);
    ASSERT_NE(FindNode(projection.diagram, "G1"), nullptr);
    EXPECT_EQ(FindNode(projection.diagram, "G1")->kind, export_gsn::GsnNodeKind::Goal);
    EXPECT_EQ(FindNode(projection.diagram, "S1")->kind, export_gsn::GsnNodeKind::Strategy);
    EXPECT_EQ(FindNode(projection.diagram, "Sn1")->kind, export_gsn::GsnNodeKind::Solution);
    EXPECT_EQ(FindNode(projection.diagram, "C1")->kind, export_gsn::GsnNodeKind::Context);
    EXPECT_EQ(FindNode(projection.diagram, "A1")->kind, export_gsn::GsnNodeKind::Assumption);
    EXPECT_EQ(FindNode(projection.diagram, "J1")->kind, export_gsn::GsnNodeKind::Justification);
    EXPECT_EQ(projection.diagram.edges.size(), 6u);
}

TEST(GsnSvgExporterTest, ProjectionExportsVisibleTerminologyContextsAsNormalContexts) {
    parser::AssuranceCase model;
    parser::SacmElement visible_term =
        Element("TC1", "artifactreference", "ABS: Anti-lock braking system", "Term definition.");
    parser::SacmElement visible_context = Relationship("AC1", "assertedcontext", {"TC1"}, {"G1"});
    visible_context.description = core::kVisibleTerminologyContextMarker;
    model.elements = {Element("G1", "claim", "Goal", "Vehicle braking remains safe."), visible_term, visible_context};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);

    ASSERT_EQ(projection.diagram.nodes.size(), 2u);
    EXPECT_NE(FindNode(projection.diagram, "G1"), nullptr);
    const export_gsn::GsnNode* context = FindNode(projection.diagram, "C1");
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->kind, export_gsn::GsnNodeKind::Context);
    EXPECT_EQ(context->title, "ABS: Anti-lock braking system");
    EXPECT_EQ(context->text, "Term definition.");
    EXPECT_EQ(FindNode(projection.diagram, "TC1"), nullptr);
    ASSERT_EQ(projection.diagram.edges.size(), 1u);
    EXPECT_EQ(projection.diagram.edges.front().from_id, "G1");
    EXPECT_EQ(projection.diagram.edges.front().to_id, "C1");
    EXPECT_EQ(projection.diagram.edges.front().kind, export_gsn::GsnEdgeKind::InContextOf);
}

TEST(GsnSvgExporterTest, ProjectionIgnoresHiddenTermAssociationsOnContextNodes) {
    parser::AssuranceCase model;
    parser::SacmElement context =
        Element("C1", "artifactreference", "Operational context", "Defined operating domain.");
    parser::SacmElement term_reference = Element("AR1", "artifactreference", "ABS", "Anti-lock braking system.");
    model.elements = {Element("G1", "claim", "Goal", "Vehicle braking remains safe."),
                      context,
                      term_reference,
                      Relationship("ctx1", "assertedcontext", {"C1"}, {"G1"}),
                      Relationship("term-link", "assertedcontext", {"AR1"}, {"C1"})};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);

    ASSERT_EQ(projection.diagram.nodes.size(), 2u);
    EXPECT_NE(FindNode(projection.diagram, "G1"), nullptr);
    EXPECT_NE(FindNode(projection.diagram, "C1"), nullptr);
    EXPECT_EQ(FindNode(projection.diagram, "AR1"), nullptr);
    ASSERT_EQ(projection.diagram.edges.size(), 1u);
    EXPECT_EQ(projection.diagram.edges.front().from_id, "G1");
    EXPECT_EQ(projection.diagram.edges.front().to_id, "C1");
    EXPECT_EQ(projection.diagram.edges.front().kind, export_gsn::GsnEdgeKind::InContextOf);
}

TEST(GsnSvgExporterTest, ProjectionSkipsVisibleTermContextAttachedToContextNode) {
    parser::AssuranceCase model;
    parser::SacmElement context = Element("C3", "claim", "Kitchen context", "The kitchen operating area.");
    parser::SacmElement visible_term = Element("TC2", "artifactreference", "kitchen", "Kitchen term definition.");
    parser::SacmElement visible_context = Relationship("AC5", "assertedcontext", {"TC2"}, {"C3"});
    visible_context.description = core::kVisibleTerminologyContextMarker;
    model.elements = {Element("G1", "claim", "Goal", "Robot operation remains safe."),
                      context,
                      visible_term,
                      Relationship("R29", "assertedcontext", {"C3"}, {"G1"}),
                      visible_context};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);

    ASSERT_EQ(projection.diagram.nodes.size(), 2u);
    EXPECT_NE(FindNode(projection.diagram, "G1"), nullptr);
    const export_gsn::GsnNode* context_node = FindNode(projection.diagram, "C3");
    ASSERT_NE(context_node, nullptr);
    EXPECT_EQ(context_node->kind, export_gsn::GsnNodeKind::Context);
    EXPECT_EQ(FindNode(projection.diagram, "TC2"), nullptr);
    ASSERT_EQ(projection.diagram.edges.size(), 1u);
    EXPECT_EQ(projection.diagram.edges.front().from_id, "G1");
    EXPECT_EQ(projection.diagram.edges.front().to_id, "C3");
    EXPECT_EQ(projection.diagram.edges.front().kind, export_gsn::GsnEdgeKind::InContextOf);
}

TEST(GsnSvgExporterTest, ProjectionKeepsElementNameAndContent) {
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Term-like label", "Actual safety claim content.")};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);

    ASSERT_EQ(projection.diagram.nodes.size(), 1u);
    EXPECT_EQ(projection.diagram.nodes.front().title, "Term-like label");
    EXPECT_EQ(projection.diagram.nodes.front().text, "Actual safety claim content.");
}

TEST(GsnSvgExporterTest, SvgRendersElementNameAndContent) {
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Top safety claim", "Actual safety claim content.")};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);
    export_gsn::LayoutGsnSvgDiagram(projection.diagram);

    const std::string svg = export_gsn::GenerateGsnSvg(projection.diagram);

    EXPECT_NE(svg.find("G1: Top safety claim"), std::string::npos);
    EXPECT_NE(svg.find("Actual safety claim content."), std::string::npos);
}

TEST(GsnSvgExporterTest, MissingRelationshipEndpointProducesWarning) {
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Goal"), Relationship("inf1", "assertedinference", {"missing"}, {"G1"})};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);

    EXPECT_EQ(projection.diagram.nodes.size(), 1u);
    EXPECT_TRUE(projection.diagram.edges.empty());
    ASSERT_FALSE(projection.warnings.empty());
}

TEST(GsnSvgExporterTest, DuplicateNodeIdsAreMadeSafeForSvg) {
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Goal one"), Element("G1", "claim", "Goal two")};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);

    ASSERT_EQ(projection.diagram.nodes.size(), 2u);
    EXPECT_EQ(projection.diagram.nodes[0].id, "G1");
    EXPECT_EQ(projection.diagram.nodes[1].id, "G1_2");
    EXPECT_FALSE(projection.warnings.empty());
}

TEST(GsnSvgExporterTest, LayoutPlacesSupportBelowAndContextToSide) {
    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(BuildRepresentativeCase());
    export_gsn::LayoutGsnSvgDiagram(projection.diagram);

    const export_gsn::GsnNode* goal = FindNode(projection.diagram, "G1");
    const export_gsn::GsnNode* strategy = FindNode(projection.diagram, "S1");
    const export_gsn::GsnNode* solution = FindNode(projection.diagram, "Sn1");
    const export_gsn::GsnNode* context = FindNode(projection.diagram, "C1");
    ASSERT_NE(goal, nullptr);
    ASSERT_NE(strategy, nullptr);
    ASSERT_NE(solution, nullptr);
    ASSERT_NE(context, nullptr);

    EXPECT_GT(strategy->y, goal->y);
    EXPECT_GT(solution->y, strategy->y);
    EXPECT_NE(context->x, goal->x);
}

TEST(GsnSvgExporterTest, LayoutCentersMultipleSideAttachmentsAroundOwner) {
    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(BuildRepresentativeCase());
    export_gsn::LayoutGsnSvgDiagram(projection.diagram);

    const export_gsn::GsnNode* goal = FindNode(projection.diagram, "G1");
    const export_gsn::GsnNode* context = FindNode(projection.diagram, "C1");
    const export_gsn::GsnNode* assumption = FindNode(projection.diagram, "A1");
    const export_gsn::GsnNode* justification = FindNode(projection.diagram, "J1");
    ASSERT_NE(goal, nullptr);
    ASSERT_NE(context, nullptr);
    ASSERT_NE(assumption, nullptr);
    ASSERT_NE(justification, nullptr);

    const double goal_center_y = goal->y + goal->height / 2.0;
    const double left_stack_center_y = (context->y + assumption->y + assumption->height) / 2.0;
    const double right_stack_center_y = justification->y + justification->height / 2.0;
    EXPECT_NEAR(left_stack_center_y, goal_center_y, 1.0);
    EXPECT_NEAR(right_stack_center_y, goal_center_y, 1.0);
}

TEST(GsnSvgExporterTest, LayoutPlacesStandaloneAssumptionsAsRoots) {
    parser::AssuranceCase model;
    parser::SacmElement assumption = Element("A1", "claim", "Standalone assumption", "Assumption text.");
    assumption.assertion_declaration = "assumed";
    model.elements = {Element("G1", "claim", "Goal", "Goal text."), assumption};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);
    export_gsn::LayoutGsnSvgDiagram(projection.diagram);

    const export_gsn::GsnNode* goal = FindNode(projection.diagram, "G1");
    const export_gsn::GsnNode* standalone_assumption = FindNode(projection.diagram, "A1");
    ASSERT_NE(goal, nullptr);
    ASSERT_NE(standalone_assumption, nullptr);
    EXPECT_GT(standalone_assumption->x, goal->x);
    EXPECT_GE(standalone_assumption->y, 48.0);
}

TEST(GsnSvgExporterTest, LayoutKeepsVisibleTermContextClearOfSiblingGoal) {
    parser::AssuranceCase model;
    parser::SacmElement visible_term = Element("TC1", "artifactreference", "Kitchen", "Kitchen term definition.");
    parser::SacmElement visible_context = Relationship("AC1", "assertedcontext", {"TC1"}, {"G11"});
    visible_context.description = core::kVisibleTerminologyContextMarker;
    model.elements = {Element("G1", "claim", "Top goal", "System operation is safe."),
                      Element("S1", "argumentreasoning", "Strategy", "Argument over operating areas."),
                      Element("G10", "claim", "Sibling goal", "Nearby sibling remains safe."),
                      Element("G11", "claim", "Kitchen goal", "Kitchen operation remains safe."),
                      visible_term,
                      Relationship("R1", "assertedinference", {"G10", "G11"}, {"G1"}, "S1"),
                      visible_context};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);
    export_gsn::LayoutGsnSvgDiagram(projection.diagram);

    const export_gsn::GsnNode* sibling = FindNode(projection.diagram, "G10");
    const export_gsn::GsnNode* generated_context = FindNode(projection.diagram, "C1");
    ASSERT_NE(sibling, nullptr);
    ASSERT_NE(generated_context, nullptr);
    EXPECT_FALSE(RectanglesOverlap(*sibling, *generated_context));
}

TEST(GsnSvgExporterTest, LayoutResizesLongTextNodes) {
    parser::AssuranceCase model;
    model.elements = {Element("G1",
                              "claim",
                              "Goal",
                              "This safety claim contains a deliberately long publication text block that should "
                              "force the exported GSN goal shape to grow so the SVG text remains inside the border "
                              "instead of spilling outside the element.")};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);
    export_gsn::LayoutGsnSvgDiagram(projection.diagram);

    ASSERT_EQ(projection.diagram.nodes.size(), 1u);
    EXPECT_GT(projection.diagram.nodes.front().height, 86.0);
}

TEST(GsnSvgExporterTest, LayoutKeepsShortTitleAndContentAtBaseHeight) {
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Goal", "Short claim.")};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);
    export_gsn::LayoutGsnSvgDiagram(projection.diagram);

    ASSERT_EQ(projection.diagram.nodes.size(), 1u);
    EXPECT_DOUBLE_EQ(projection.diagram.nodes.front().height, 86.0);
}

TEST(GsnSvgExporterTest, SvgContainsNamespaceMarkersAndPublicationStyle) {
    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(BuildRepresentativeCase());
    export_gsn::LayoutGsnSvgDiagram(projection.diagram);

    std::string svg = export_gsn::GenerateGsnSvg(projection.diagram);

    EXPECT_NE(svg.find("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"), std::string::npos);
    EXPECT_NE(svg.find("xmlns=\"http://www.w3.org/2000/svg\""), std::string::npos);
    EXPECT_NE(svg.find("id=\"supportedByArrow\""), std::string::npos);
    EXPECT_NE(svg.find("id=\"contextArrow\""), std::string::npos);
    EXPECT_NE(svg.find("fill=\"white\""), std::string::npos);
    EXPECT_NE(svg.find("stroke=\"black\""), std::string::npos);
    EXPECT_NE(svg.find("class=\"gsn-solution\""), std::string::npos);
}

// ===== AF-ENG-015: the exported SVG must not understate the argument =====

TEST(GsnSvgExporterTest, CounterEvidenceIsNeverProjectedAsSupport) {
    // The defect this guards: a counter relationship exported as SupportedBy
    // shows evidence *against* a claim as evidence *for* it.
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Goal", "System is acceptably safe."),
                      Element("Sn9", "artifactreference", "Field incident report", "Counter evidence."),
                      Challenge("ch1", "assertedevidence", {"Sn9"}, {"G1"})};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);

    EXPECT_EQ(CountEdgesOfKind(projection.diagram, export_gsn::GsnEdgeKind::SupportedBy), 0u);
    ASSERT_EQ(CountEdgesOfKind(projection.diagram, export_gsn::GsnEdgeKind::Challenges), 1u);
    const export_gsn::GsnEdge* challenge = FindEdgeBetween(projection.diagram, "Sn9", "G1");
    ASSERT_NE(challenge, nullptr);
    EXPECT_EQ(challenge->kind, export_gsn::GsnEdgeKind::Challenges);
}

TEST(GsnSvgExporterTest, CounterClaimChallengesItsTargetElement) {
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Goal", "System is acceptably safe."),
                      Element("G9", "claim", "Counter claim", "The hazard list is incomplete."),
                      Challenge("ch1", "assertedinference", {"G9"}, {"G1"})};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);

    ASSERT_EQ(projection.diagram.edges.size(), 1u);
    EXPECT_EQ(projection.diagram.edges.front().kind, export_gsn::GsnEdgeKind::Challenges);
    EXPECT_EQ(projection.diagram.edges.front().from_id, "G9");
    EXPECT_EQ(projection.diagram.edges.front().to_id, "G1");
}

TEST(GsnSvgExporterTest, ChallengeTargetingRelationshipLandsOnThatEdge) {
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Goal", "System is acceptably safe."),
                      Element("C1", "artifactreference", "Operating context", "Defined operating domain."),
                      Element("G9", "claim", "Counter claim", "That context does not hold."),
                      Relationship("ctx1", "assertedcontext", {"C1"}, {"G1"}),
                      Challenge("ch1", "assertedinference", {"G9"}, {"ctx1"})};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);

    ASSERT_EQ(CountEdgesOfKind(projection.diagram, export_gsn::GsnEdgeKind::Challenges), 1u);
    const export_gsn::GsnEdge* challenge = nullptr;
    for (const export_gsn::GsnEdge& edge : projection.diagram.edges) {
        if (edge.kind == export_gsn::GsnEdgeKind::Challenges)
            challenge = &edge;
    }
    ASSERT_NE(challenge, nullptr);
    EXPECT_EQ(challenge->from_id, "G9");
    EXPECT_TRUE(challenge->to_id.empty());
    EXPECT_EQ(challenge->to_edge_id, "ctx1");
}

TEST(GsnSvgExporterTest, ChallengeWithUnexportedTargetWarnsInsteadOfVanishing) {
    parser::AssuranceCase model;
    model.elements = {Element("G9", "claim", "Counter claim", "Something is wrong."),
                      Challenge("ch1", "assertedinference", {"G9"}, {"missing"})};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);

    EXPECT_TRUE(projection.diagram.edges.empty());
    ASSERT_FALSE(projection.warnings.empty());
}

TEST(GsnSvgExporterTest, LayoutKeepsCounterOutOfTheSupportTree) {
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Goal", "System is acceptably safe."),
                      Element("G2", "claim", "Sub goal", "Hazards are mitigated."),
                      Element("G9", "claim", "Counter claim", "The hazard list is incomplete."),
                      Relationship("inf1", "assertedinference", {"G2"}, {"G1"}),
                      Challenge("ch1", "assertedinference", {"G9"}, {"G1"})};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);
    export_gsn::LayoutGsnSvgDiagram(projection.diagram);

    const export_gsn::GsnNode* goal = FindNode(projection.diagram, "G1");
    const export_gsn::GsnNode* sub_goal = FindNode(projection.diagram, "G2");
    const export_gsn::GsnNode* counter = FindNode(projection.diagram, "G9");
    ASSERT_NE(goal, nullptr);
    ASSERT_NE(sub_goal, nullptr);
    ASSERT_NE(counter, nullptr);

    // The supported sub-goal hangs below; the counter sits beside, not below.
    EXPECT_GT(sub_goal->y, goal->y);
    EXPECT_NE(counter->x, goal->x);
    EXPECT_FALSE(RectanglesOverlap(*counter, *sub_goal));
}

TEST(GsnSvgExporterTest, SvgDrawsChallengeDistinctlyFromSupport) {
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Goal", "System is acceptably safe."),
                      Element("G9", "claim", "Counter claim", "The hazard list is incomplete."),
                      Challenge("ch1", "assertedinference", {"G9"}, {"G1"})};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);
    export_gsn::LayoutGsnSvgDiagram(projection.diagram);
    const std::string svg = export_gsn::GenerateGsnSvg(projection.diagram);

    EXPECT_NE(svg.find("id=\"challengeArrow\""), std::string::npos);
    EXPECT_NE(svg.find("class=\"gsn-challenges\""), std::string::npos);
    EXPECT_NE(svg.find("marker-end=\"url(#challengeArrow)\""), std::string::npos);
    EXPECT_EQ(svg.find("marker-end=\"url(#supportedByArrow)\""), std::string::npos);
}

TEST(GsnSvgExporterTest, ProjectionCarriesUndevelopedFlagToTheDiagram) {
    parser::AssuranceCase model;
    parser::SacmElement undeveloped_goal = Element("G2", "claim", "Undeveloped goal", "Not yet argued.");
    undeveloped_goal.undeveloped = true;
    model.elements = {Element("G1", "claim", "Goal", "System is acceptably safe."),
                      undeveloped_goal,
                      Relationship("inf1", "assertedinference", {"G2"}, {"G1"})};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);

    ASSERT_NE(FindNode(projection.diagram, "G2"), nullptr);
    EXPECT_TRUE(FindNode(projection.diagram, "G2")->undeveloped);
    EXPECT_FALSE(FindNode(projection.diagram, "G1")->undeveloped);
}

TEST(GsnSvgExporterTest, SvgDrawsTheUndevelopedDiamond) {
    // Without this the export shows an incomplete argument as a finished one.
    parser::AssuranceCase model;
    parser::SacmElement undeveloped_goal = Element("G1", "claim", "Undeveloped goal", "Not yet argued.");
    undeveloped_goal.undeveloped = true;
    model.elements = {undeveloped_goal};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);
    export_gsn::LayoutGsnSvgDiagram(projection.diagram);
    const std::string svg = export_gsn::GenerateGsnSvg(projection.diagram);

    EXPECT_NE(svg.find("class=\"gsn-undeveloped\""), std::string::npos);
}

TEST(GsnSvgExporterTest, SvgOmitsTheUndevelopedDiamondWhenNotUndeveloped) {
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Developed goal", "Fully argued.")};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);
    export_gsn::LayoutGsnSvgDiagram(projection.diagram);
    const std::string svg = export_gsn::GenerateGsnSvg(projection.diagram);

    EXPECT_EQ(svg.find("class=\"gsn-undeveloped\""), std::string::npos);
}

TEST(GsnSvgExporterTest, AcpOnAnElementIsProjectedAndDrawn) {
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Goal", "System is acceptably safe.")};
    model.acps = {Acp("ACP1", "element", "G1")};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);
    export_gsn::LayoutGsnSvgDiagram(projection.diagram);
    const std::string svg = export_gsn::GenerateGsnSvg(projection.diagram);

    ASSERT_NE(FindNode(projection.diagram, "G1"), nullptr);
    ASSERT_EQ(FindNode(projection.diagram, "G1")->acp_labels.size(), 1u);
    EXPECT_EQ(FindNode(projection.diagram, "G1")->acp_labels.front(), "ACP1");
    EXPECT_NE(svg.find("class=\"gsn-acp\""), std::string::npos);
    EXPECT_NE(svg.find("ACP1"), std::string::npos);
}

TEST(GsnSvgExporterTest, AcpOnARelationshipIsProjectedAndDrawn) {
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Goal", "System is acceptably safe."),
                      Element("G2", "claim", "Sub goal", "Hazards are mitigated."),
                      Relationship("inf1", "assertedinference", {"G2"}, {"G1"})};
    model.acps = {Acp("ACP7", "relationship", "inf1")};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);
    export_gsn::LayoutGsnSvgDiagram(projection.diagram);
    const std::string svg = export_gsn::GenerateGsnSvg(projection.diagram);

    ASSERT_EQ(projection.diagram.edges.size(), 1u);
    ASSERT_EQ(projection.diagram.edges.front().acp_labels.size(), 1u);
    EXPECT_EQ(projection.diagram.edges.front().acp_labels.front(), "ACP7");
    EXPECT_NE(svg.find("ACP7"), std::string::npos);
}

TEST(GsnSvgExporterTest, NodeCarryingBothDecoratorsKeepsThemApart) {
    parser::AssuranceCase model;
    parser::SacmElement goal = Element("G1", "claim", "Goal", "System is acceptably safe.");
    goal.undeveloped = true;
    model.elements = {goal};
    model.acps = {Acp("ACP1", "element", "G1")};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);
    export_gsn::LayoutGsnSvgDiagram(projection.diagram);
    const std::string svg = export_gsn::GenerateGsnSvg(projection.diagram);

    EXPECT_NE(svg.find("class=\"gsn-undeveloped\""), std::string::npos);
    EXPECT_NE(svg.find("class=\"gsn-acp\""), std::string::npos);
}

TEST(GsnSvgExporterTest, ExportWritesStandaloneSvgAndUsesUniqueNames) {
    TempDir temp(MakeTempDir());
    parser::AssuranceCase model = BuildRepresentativeCase();

    export_gsn::GsnSvgExportResult first = export_gsn::ExportCurrentSafetyCaseToGsnSvg(model, temp.path, "main");
    export_gsn::GsnSvgExportResult second = export_gsn::ExportCurrentSafetyCaseToGsnSvg(model, temp.path, "main");

    ASSERT_TRUE(first.success) << first.error_message;
    ASSERT_TRUE(second.success) << second.error_message;
    EXPECT_EQ(first.output_path.filename().string(), "main_gsn.svg");
    EXPECT_EQ(second.output_path.filename().string(), "main_gsn_001.svg");
    EXPECT_TRUE(std::filesystem::exists(first.output_path));
    EXPECT_TRUE(std::filesystem::exists(second.output_path));
}
