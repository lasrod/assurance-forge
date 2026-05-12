#include "export/gsn_layout.h"
#include "export/gsn_projection.h"
#include "export/gsn_svg_exporter.h"
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
    parser::SacmElement context = Element("C1", "artifactreference", "Operational context", "Defined operating domain.");
    parser::SacmElement assumption = Element("A1", "claim", "Hazard assumption", "All hazards are identified.");
    assumption.assertion_declaration = "assumed";
    parser::SacmElement justification = Element("J1", "claim", "Decomposition justification", "Decomposition is appropriate.");
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

const export_gsn::GsnNode* FindNode(const export_gsn::GsnDiagram& diagram, const std::string& id) {
    for (const export_gsn::GsnNode& node : diagram.nodes) {
        if (node.id == id)
            return &node;
    }
    return nullptr;
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

TEST(GsnSvgExporterTest, ProjectionIgnoresVisibleTerminologyContexts) {
    parser::AssuranceCase model;
    parser::SacmElement visible_term = Element("TC1", "artifactreference", "ABS: Anti-lock braking system", "Term definition.");
    parser::SacmElement visible_context = Relationship("AC1", "assertedcontext", {"TC1"}, {"G1"});
    visible_context.description = core::kVisibleTerminologyContextMarker;
    model.elements = {Element("G1", "claim", "Goal", "Vehicle braking remains safe."), visible_term, visible_context};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);

    ASSERT_EQ(projection.diagram.nodes.size(), 1u);
    EXPECT_NE(FindNode(projection.diagram, "G1"), nullptr);
    EXPECT_EQ(FindNode(projection.diagram, "TC1"), nullptr);
    EXPECT_TRUE(projection.diagram.edges.empty());
}

TEST(GsnSvgExporterTest, ProjectionUsesContentBeforeElementName) {
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Term-like label", "Actual safety claim content.")};

    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(model);

    ASSERT_EQ(projection.diagram.nodes.size(), 1u);
    EXPECT_EQ(projection.diagram.nodes.front().text, "Actual safety claim content.");
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
    export_gsn::LayoutGsnDiagram(projection.diagram);

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
    export_gsn::LayoutGsnDiagram(projection.diagram);

    const export_gsn::GsnNode* goal = FindNode(projection.diagram, "G1");
    const export_gsn::GsnNode* context = FindNode(projection.diagram, "C1");
    const export_gsn::GsnNode* justification = FindNode(projection.diagram, "J1");
    ASSERT_NE(goal, nullptr);
    ASSERT_NE(context, nullptr);
    ASSERT_NE(justification, nullptr);

    const double goal_center_y = goal->y + goal->height / 2.0;
    const double side_stack_center_y = (context->y + justification->y + justification->height) / 2.0;
    EXPECT_NEAR(side_stack_center_y, goal_center_y, 1.0);
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
    export_gsn::LayoutGsnDiagram(projection.diagram);

    ASSERT_EQ(projection.diagram.nodes.size(), 1u);
    EXPECT_GT(projection.diagram.nodes.front().height, 86.0);
}

TEST(GsnSvgExporterTest, SvgContainsNamespaceMarkersAndPublicationStyle) {
    export_gsn::GsnProjectionResult projection = export_gsn::BuildGsnProjection(BuildRepresentativeCase());
    export_gsn::LayoutGsnDiagram(projection.diagram);

    std::string svg = export_gsn::GenerateGsnSvg(projection.diagram);

    EXPECT_NE(svg.find("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"), std::string::npos);
    EXPECT_NE(svg.find("xmlns=\"http://www.w3.org/2000/svg\""), std::string::npos);
    EXPECT_NE(svg.find("id=\"supportedByArrow\""), std::string::npos);
    EXPECT_NE(svg.find("id=\"contextArrow\""), std::string::npos);
    EXPECT_NE(svg.find("fill=\"white\""), std::string::npos);
    EXPECT_NE(svg.find("stroke=\"black\""), std::string::npos);
    EXPECT_NE(svg.find("class=\"gsn-solution\""), std::string::npos);
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
