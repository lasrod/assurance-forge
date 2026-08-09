// Tests for GSN v3 dialectic challenge support: counter argument / counter
// evidence creation (core::AddChallenge), counter-aware tree building, removal,
// and isCounter round-trip through the parser and SACM serializer.

#include "core/assurance_tree.h"
#include "core/element_factory.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_model.h"
#include "legacy_sacm/sacm_parser.h"
#include "legacy_sacm/sacm_serializer.h"
#include "ui/gsn/gsn_layout.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>

namespace {

struct MiniCase {
    parser::AssuranceCase model;
    sacm::AssuranceCasePackage package;
};

void AddClaim(MiniCase& mini_case, const std::string& id) {
    parser::SacmElement element;
    element.id = id;
    element.type = "claim";
    element.name = id;
    mini_case.model.elements.push_back(element);

    if (mini_case.package.argumentPackages.empty())
        mini_case.package.argumentPackages.emplace_back();
    sacm::Claim claim;
    claim.id = id;
    claim.name = id;
    mini_case.package.argumentPackages.front().claims.push_back(claim);
}

void AddAssumption(MiniCase& mini_case, const std::string& id) {
    parser::SacmElement element;
    element.id = id;
    element.type = "claim";
    element.name = id;
    element.assertion_declaration = "assumed";
    mini_case.model.elements.push_back(element);

    if (mini_case.package.argumentPackages.empty())
        mini_case.package.argumentPackages.emplace_back();
    sacm::Claim claim;
    claim.id = id;
    claim.name = id;
    claim.assertionDeclaration = "assumed";
    mini_case.package.argumentPackages.front().claims.push_back(claim);
}

void AddContextLink(MiniCase& mini_case, const std::string& id, const std::string& target, const std::string& source) {
    parser::SacmElement relationship;
    relationship.id = id;
    relationship.type = "assertedcontext";
    relationship.target_refs.push_back(target);
    relationship.source_refs.push_back(source);
    mini_case.model.elements.push_back(relationship);

    if (mini_case.package.argumentPackages.empty())
        mini_case.package.argumentPackages.emplace_back();
    sacm::AssertedContext asserted_context;
    asserted_context.id = id;
    asserted_context.targets.push_back(target);
    asserted_context.sources.push_back(source);
    mini_case.package.argumentPackages.front().assertedContexts.push_back(asserted_context);
}

void AddArtifactReference(MiniCase& mini_case, const std::string& id) {
    parser::SacmElement element;
    element.id = id;
    element.type = "artifactreference";
    element.name = id;
    mini_case.model.elements.push_back(element);

    if (mini_case.package.argumentPackages.empty())
        mini_case.package.argumentPackages.emplace_back();
    sacm::ArtifactReference artifact_reference;
    artifact_reference.id = id;
    artifact_reference.name = id;
    mini_case.package.argumentPackages.front().artifactReferences.push_back(artifact_reference);
}

void AddStrategy(MiniCase& mini_case, const std::string& id) {
    parser::SacmElement element;
    element.id = id;
    element.type = "argumentreasoning";
    element.name = id;
    mini_case.model.elements.push_back(element);

    if (mini_case.package.argumentPackages.empty())
        mini_case.package.argumentPackages.emplace_back();
    sacm::ArgumentReasoning reasoning;
    reasoning.id = id;
    reasoning.name = id;
    mini_case.package.argumentPackages.front().argumentReasonings.push_back(reasoning);
}

void AddInference(MiniCase& mini_case,
                  const std::string& id,
                  const std::string& target,
                  const std::string& source,
                  const std::string& reasoning = {}) {
    parser::SacmElement relationship;
    relationship.id = id;
    relationship.type = "assertedinference";
    relationship.target_refs.push_back(target);
    if (!source.empty())
        relationship.source_refs.push_back(source);
    relationship.reasoning_ref = reasoning;
    mini_case.model.elements.push_back(relationship);

    if (mini_case.package.argumentPackages.empty())
        mini_case.package.argumentPackages.emplace_back();
    sacm::AssertedInference inference;
    inference.id = id;
    inference.targets.push_back(target);
    if (!source.empty())
        inference.sources.push_back(source);
    inference.reasoning = reasoning;
    mini_case.package.argumentPackages.front().assertedInferences.push_back(inference);
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& ac, const std::string& id) {
    for (const auto& e : ac.elements)
        if (e.id == id)
            return &e;
    return nullptr;
}

const core::TreeNode* FindNode(const core::AssuranceTree& tree, const std::string& id) {
    return core::FindTreeNode(tree, id);
}

// Minimal two-claim case G1 <- G2 (inference INF1) used by several tests.
MiniCase MakeBaseCase() {
    MiniCase mini;
    AddClaim(mini, "G1");
    AddClaim(mini, "G2");
    AddInference(mini, "INF1", "G1", "G2");
    return mini;
}

} // namespace

// ===== Creation: counter argument against an element =====

TEST(DialecticChallenge, CounterArgumentAgainstElement) {
    MiniCase mini = MakeBaseCase();
    std::string new_id, rel_id, err;

    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "G1"};
    ASSERT_TRUE(core::AddChallenge(
        mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument, new_id, rel_id, err))
        << err;

    EXPECT_EQ(new_id.rfind("CG", 0), 0u) << "counter argument id should use CG prefix, got " << new_id;

    const parser::SacmElement* counter = FindElement(mini.model, new_id);
    ASSERT_NE(counter, nullptr);
    EXPECT_EQ(counter->type, "claim");
    EXPECT_FALSE(counter->undeveloped) << "counter argument should not default to undeveloped";

    const parser::SacmElement* rel = FindElement(mini.model, rel_id);
    ASSERT_NE(rel, nullptr);
    EXPECT_EQ(rel->type, "assertedinference");
    EXPECT_TRUE(rel->is_counter);
    ASSERT_EQ(rel->source_refs.size(), 1u);
    EXPECT_EQ(rel->source_refs.front(), new_id);
    ASSERT_EQ(rel->target_refs.size(), 1u);
    EXPECT_EQ(rel->target_refs.front(), "G1");

    // SACM mirror carries isCounter so it survives serialization.
    const auto& inferences = mini.package.argumentPackages.front().assertedInferences;
    auto it = std::find_if(
        inferences.begin(), inferences.end(), [&](const sacm::AssertedInference& i) { return i.id == rel_id; });
    ASSERT_NE(it, inferences.end());
    EXPECT_TRUE(it->isCounter);
}

// ===== Creation: counter evidence against an element =====

TEST(DialecticChallenge, CounterEvidenceAgainstElement) {
    MiniCase mini = MakeBaseCase();
    std::string new_id, rel_id, err;

    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "G2"};
    ASSERT_TRUE(core::AddChallenge(
        mini.model, &mini.package, target, core::ChallengeSourceType::CounterEvidence, new_id, rel_id, err))
        << err;

    EXPECT_EQ(new_id.rfind("CSn", 0), 0u) << "counter evidence id should use CSn prefix, got " << new_id;

    const parser::SacmElement* counter = FindElement(mini.model, new_id);
    ASSERT_NE(counter, nullptr);
    EXPECT_EQ(counter->type, "artifactreference");

    const parser::SacmElement* rel = FindElement(mini.model, rel_id);
    ASSERT_NE(rel, nullptr);
    EXPECT_EQ(rel->type, "assertedevidence");
    EXPECT_TRUE(rel->is_counter);
    EXPECT_EQ(rel->target_refs.front(), "G2");

    const auto& evidences = mini.package.argumentPackages.front().assertedEvidences;
    auto it = std::find_if(
        evidences.begin(), evidences.end(), [&](const sacm::AssertedEvidence& e) { return e.id == rel_id; });
    ASSERT_NE(it, evidences.end());
    EXPECT_TRUE(it->isCounter);
}

// ===== Creation: challenge against a relationship (incl. a Challenges rel) =====

TEST(DialecticChallenge, CounterArgumentAgainstRelationship) {
    MiniCase mini = MakeBaseCase();
    std::string new_id, rel_id, err;

    core::ArgumentTarget target{core::ArgumentTarget::Kind::Relationship, "INF1"};
    ASSERT_TRUE(core::AddChallenge(
        mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument, new_id, rel_id, err))
        << err;

    const parser::SacmElement* rel = FindElement(mini.model, rel_id);
    ASSERT_NE(rel, nullptr);
    EXPECT_TRUE(rel->is_counter);
    ASSERT_EQ(rel->target_refs.size(), 1u);
    EXPECT_EQ(rel->target_refs.front(), "INF1");

    // A challenge may itself be challenged (challenge-to-challenge).
    std::string new_id2, rel_id2, err2;
    core::ArgumentTarget counter_target{core::ArgumentTarget::Kind::Relationship, rel_id};
    EXPECT_TRUE(core::AddChallenge(
        mini.model, &mini.package, counter_target, core::ChallengeSourceType::CounterArgument, new_id2, rel_id2, err2))
        << err2;
    const parser::SacmElement* rel2 = FindElement(mini.model, rel_id2);
    ASSERT_NE(rel2, nullptr);
    EXPECT_EQ(rel2->target_refs.front(), rel_id);
}

TEST(DialecticChallenge, RejectsMissingTarget) {
    MiniCase mini = MakeBaseCase();
    std::string new_id, rel_id, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "does-not-exist"};
    EXPECT_FALSE(core::AddChallenge(
        mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument, new_id, rel_id, err));
    EXPECT_FALSE(err.empty());
}

// ===== Tree build: counter source is flagged, not wired as ordinary support =====

TEST(DialecticChallenge, TreeBuildMarksCounterSource) {
    MiniCase mini = MakeBaseCase();
    std::string new_id, rel_id, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "G1"};
    ASSERT_TRUE(core::AddChallenge(
        mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument, new_id, rel_id, err))
        << err;

    core::AssuranceTree tree = core::AssuranceTree::Build(mini.model);
    const core::TreeNode* counter = FindNode(tree, new_id);
    ASSERT_NE(counter, nullptr);
    EXPECT_TRUE(counter->is_counter_source);
    EXPECT_FALSE(counter->challenge_target_is_relationship);
    EXPECT_EQ(counter->challenge_target_id, "G1");
    // A counter is a parentless cluster root, anchored near its target element.
    EXPECT_EQ(counter->parent, nullptr);
    EXPECT_EQ(counter->challenge_anchor_id, "G1");
    EXPECT_EQ(counter->challenge_relationship_id, rel_id);
}

TEST(DialecticChallenge, TreeBuildResolvesRelationshipTargetToElement) {
    MiniCase mini = MakeBaseCase();
    std::string new_id, rel_id, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Relationship, "INF1"};
    ASSERT_TRUE(core::AddChallenge(
        mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument, new_id, rel_id, err))
        << err;

    core::AssuranceTree tree = core::AssuranceTree::Build(mini.model);
    const core::TreeNode* counter = FindNode(tree, new_id);
    ASSERT_NE(counter, nullptr);
    EXPECT_TRUE(counter->is_counter_source);
    EXPECT_TRUE(counter->challenge_target_is_relationship);
    EXPECT_EQ(counter->challenge_target_id, "INF1");
    EXPECT_EQ(counter->parent, nullptr);
    // A relationship challenge anchors on the relationship's SOURCE node (the
    // reference it points to). INF1 is G2 (source) -> G1 (target), so anchor = G2.
    EXPECT_EQ(counter->challenge_anchor_id, "G2");
}

// ===== A counter argument is a full sub-argument root: it accepts children =====

TEST(DialecticChallenge, CounterArgumentAcceptsChildren) {
    MiniCase mini = MakeBaseCase();
    std::string cg, rel, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "G1"};
    ASSERT_TRUE(
        core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument, cg, rel, err))
        << err;

    // A strategy can be added under the counter argument like any Goal.
    std::string child, child_rel, err2;
    ASSERT_TRUE(
        core::AddChildElement(mini.model, &mini.package, cg, core::NewElementKind::Strategy, child, child_rel, err2))
        << err2;

    core::AssuranceTree tree = core::AssuranceTree::Build(mini.model);
    const core::TreeNode* counter = FindNode(tree, cg);
    ASSERT_NE(counter, nullptr);
    EXPECT_TRUE(counter->is_counter_source);
    EXPECT_EQ(counter->parent, nullptr); // still a cluster root

    // The new child is wired as a structural child of the counter.
    const core::TreeNode* child_node = FindNode(tree, child);
    ASSERT_NE(child_node, nullptr);
    ASSERT_NE(child_node->parent, nullptr);
    EXPECT_EQ(child_node->parent->id, cg);
}

// ===== Removal: deleting a counter node drops its challenge, keeps the target =====

TEST(DialecticChallenge, RemovingCounterKeepsTarget) {
    MiniCase mini = MakeBaseCase();
    std::string new_id, rel_id, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "G1"};
    ASSERT_TRUE(core::AddChallenge(
        mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument, new_id, rel_id, err))
        << err;

    std::string remove_err;
    ASSERT_TRUE(core::RemoveElement(mini.model, &mini.package, new_id, core::RemoveMode::NodeOnly, remove_err))
        << remove_err;

    EXPECT_EQ(FindElement(mini.model, new_id), nullptr) << "counter node should be gone";
    EXPECT_EQ(FindElement(mini.model, rel_id), nullptr) << "dangling counter relationship should be dropped";
    EXPECT_NE(FindElement(mini.model, "G1"), nullptr) << "challenged target must survive";
    EXPECT_NE(FindElement(mini.model, "INF1"), nullptr) << "unrelated support must survive";
}

// ===== Round-trip: isCounter survives parse and serialize =====

namespace {
constexpr const char* kCounterXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<SACM:AssuranceCasePackage xmlns:SACM="http://example.org/sacm/2.3" id="acp_x" name="X">
  <argumentPackage id="ap_x" name="AP">
    <Claim id="G1" name="Top"/>
    <Claim id="CG1" name="Counter"/>
    <ArtifactReference id="CSn1" name="CounterEvidence"/>
    <AssertedInference id="CH1" name="Challenge" isCounter="true">
      <source ref="CG1"/>
      <target ref="G1"/>
    </AssertedInference>
    <AssertedEvidence id="CH2" name="ChallengeEvidence" isCounter="true">
      <source ref="CSn1"/>
      <target ref="G1"/>
    </AssertedEvidence>
  </argumentPackage>
</SACM:AssuranceCasePackage>)";
}

TEST(DialecticChallenge, ParserReadsIsCounter) {
    auto parsed = parser::parse_sacm_xml_string(kCounterXml);
    ASSERT_TRUE(parsed.has_value()) << parsed.error();

    const parser::SacmElement* ch1 = FindElement(*parsed, "CH1");
    ASSERT_NE(ch1, nullptr);
    EXPECT_TRUE(ch1->is_counter);

    const parser::SacmElement* ch2 = FindElement(*parsed, "CH2");
    ASSERT_NE(ch2, nullptr);
    EXPECT_TRUE(ch2->is_counter);
}

TEST(DialecticChallenge, SacmRoundTripPreservesIsCounter) {
    auto parsed = sacm::parse_sacm_string(kCounterXml);
    ASSERT_TRUE(parsed.has_value()) << parsed.error();

    std::string serialized = sacm::serialize_sacm(*parsed);
    auto reparsed = sacm::parse_sacm_string(serialized);
    ASSERT_TRUE(reparsed.has_value()) << reparsed.error();

    ASSERT_FALSE(reparsed->argumentPackages.empty());
    const auto& ap = reparsed->argumentPackages.front();

    auto inf = std::find_if(ap.assertedInferences.begin(),
                            ap.assertedInferences.end(),
                            [](const sacm::AssertedInference& i) { return i.id == "CH1"; });
    ASSERT_NE(inf, ap.assertedInferences.end());
    EXPECT_TRUE(inf->isCounter);

    auto ev = std::find_if(ap.assertedEvidences.begin(),
                           ap.assertedEvidences.end(),
                           [](const sacm::AssertedEvidence& e) { return e.id == "CH2"; });
    ASSERT_NE(ev, ap.assertedEvidences.end());
    EXPECT_TRUE(ev->isCounter);
}

// ===== Layout placement (space reservation) =====

namespace {

// Minimal ImGui context so the layout engine can measure node text.
class ScopedImGuiFrame {
public:
    ScopedImGuiFrame() : previous_(ImGui::GetCurrentContext()) {
        context_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(context_);
        ImGui::GetIO().DisplaySize = ImVec2(1600.0f, 1200.0f);
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ImGui::NewFrame();
    }
    ~ScopedImGuiFrame() {
        ImGui::EndFrame();
        ImGui::DestroyContext(context_);
        if (previous_)
            ImGui::SetCurrentContext(previous_);
    }

private:
    ImGuiContext* previous_ = nullptr;
    ImGuiContext* context_ = nullptr;
};

const ui::gsn::LayoutNode* FindLayout(const std::vector<ui::gsn::LayoutNode>& nodes, const std::string& id) {
    for (const ui::gsn::LayoutNode& n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

bool InteriorsOverlap(const ui::gsn::LayoutNode& a, const ui::gsn::LayoutNode& b) {
    constexpr float eps = 1.0f;
    const float ix = std::min(a.position.x + a.size.x, b.position.x + b.size.x) - std::max(a.position.x, b.position.x);
    const float iy = std::min(a.position.y + a.size.y, b.position.y + b.size.y) - std::max(a.position.y, b.position.y);
    return ix > eps && iy > eps;
}

void ExpectNoOverlaps(const std::vector<ui::gsn::LayoutNode>& nodes) {
    for (std::size_t i = 0; i < nodes.size(); ++i)
        for (std::size_t j = i + 1; j < nodes.size(); ++j)
            EXPECT_FALSE(InteriorsOverlap(nodes[i], nodes[j]))
                << "nodes " << nodes[i].id << " and " << nodes[j].id << " overlap";
}

std::vector<ui::gsn::LayoutNode> LayoutOf(const parser::AssuranceCase& model) {
    core::AssuranceTree tree = core::AssuranceTree::Build(model);
    ui::gsn::LayoutEngine engine;
    return engine.ComputeLayout(tree);
}

} // namespace

TEST(DialecticChallengeLayout, ElementChallengeReservesSideSpace) {
    ScopedImGuiFrame frame;
    // G1 with two structural children, then a counter argument against G1.
    MiniCase mini;
    AddClaim(mini, "G1");
    AddClaim(mini, "G2");
    AddClaim(mini, "G3");
    AddInference(mini, "INF2", "G1", "G2");
    AddInference(mini, "INF3", "G1", "G3");
    std::string cg, rel, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "G1"};
    ASSERT_TRUE(
        core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument, cg, rel, err))
        << err;

    const auto layout = LayoutOf(mini.model);
    const ui::gsn::LayoutNode* g1 = FindLayout(layout, "G1");
    const ui::gsn::LayoutNode* counter = FindLayout(layout, cg);
    ASSERT_NE(g1, nullptr);
    ASSERT_NE(counter, nullptr);

    // The counter sits entirely to one side of G1 (G1 is the root → right).
    const bool to_right = counter->position.x >= g1->position.x + g1->size.x - 1.0f;
    const bool to_left = counter->position.x + counter->size.x <= g1->position.x + 1.0f;
    EXPECT_TRUE(to_right || to_left) << "counter should be beside G1, not above/below it";
    ExpectNoOverlaps(layout);
}

TEST(DialecticChallengeLayout, ChallengeStaysNearWideHost) {
    ScopedImGuiFrame frame;
    // A strategy with several child branches, challenged. The counter must sit
    // adjacent to the strategy node (hugging its contour at the top), NOT pushed
    // out beyond the far edge of the wide subtree.
    MiniCase mini;
    AddClaim(mini, "G1");
    AddStrategy(mini, "S1");
    AddInference(mini, "INF_S", "G1", "", "S1"); // strategy under G1
    for (int i = 0; i < 4; ++i) {
        const std::string id = "G" + std::to_string(i + 2);
        AddClaim(mini, id);
        AddInference(mini, "INF" + id, "S1", id); // children under the strategy
    }
    std::string cg, rel, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "S1"};
    ASSERT_TRUE(
        core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument, cg, rel, err))
        << err;

    const auto layout = LayoutOf(mini.model);
    const ui::gsn::LayoutNode* s1 = FindLayout(layout, "S1");
    const ui::gsn::LayoutNode* counter = FindLayout(layout, cg);
    ASSERT_NE(s1, nullptr);
    ASSERT_NE(counter, nullptr);

    // Adjacent to the strategy node itself (within a couple of node widths),
    // on one side — not beyond the whole child row.
    const float near_tolerance = s1->size.x * 2.0f + 200.0f;
    const float gap_to_node = std::min(std::fabs(counter->position.x + counter->size.x - s1->position.x),
                                       std::fabs(counter->position.x - (s1->position.x + s1->size.x)));
    EXPECT_LT(gap_to_node, near_tolerance) << "counter drifted far from the challenged strategy node";
    ExpectNoOverlaps(layout);
}

TEST(DialecticChallengeLayout, SolutionChallengePlacedToSide) {
    ScopedImGuiFrame frame;
    // G1 supported by a Solution Sn1 (SupportedBy/Group1); challenging it places
    // the counter to the SIDE of the solution (growing down), not below it.
    MiniCase mini;
    AddClaim(mini, "G1");
    AddArtifactReference(mini, "Sn1");
    {
        parser::SacmElement rel;
        rel.id = "EV1";
        rel.type = "assertedevidence";
        rel.target_refs.push_back("G1");
        rel.source_refs.push_back("Sn1");
        mini.model.elements.push_back(rel);
        sacm::AssertedEvidence ev;
        ev.id = "EV1";
        ev.targets.push_back("G1");
        ev.sources.push_back("Sn1");
        mini.package.argumentPackages.front().assertedEvidences.push_back(ev);
    }
    std::string cg, rel, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "Sn1"};
    ASSERT_TRUE(
        core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument, cg, rel, err))
        << err;

    const auto layout = LayoutOf(mini.model);
    const ui::gsn::LayoutNode* sn1 = FindLayout(layout, "Sn1");
    const ui::gsn::LayoutNode* counter = FindLayout(layout, cg);
    ASSERT_NE(sn1, nullptr);
    ASSERT_NE(counter, nullptr);

    // The counter sits beside the solution (left or right), starting at its row.
    const bool to_right = counter->position.x >= sn1->position.x + sn1->size.x - 1.0f;
    const bool to_left = counter->position.x + counter->size.x <= sn1->position.x + 1.0f;
    EXPECT_TRUE(to_right || to_left);
    ExpectNoOverlaps(layout);
}

TEST(DialecticChallengeLayout, ContextChallengePlacedBelow) {
    ScopedImGuiFrame frame;
    // G1 with a Context Cx (InContextOf/Group2); challenging the context places
    // the counter directly below it, growing down.
    MiniCase mini;
    AddClaim(mini, "G1");
    AddArtifactReference(mini, "Cx");
    {
        parser::SacmElement rel;
        rel.id = "CX1";
        rel.type = "assertedcontext";
        rel.target_refs.push_back("G1");
        rel.source_refs.push_back("Cx");
        mini.model.elements.push_back(rel);
        sacm::AssertedContext ac;
        ac.id = "CX1";
        ac.targets.push_back("G1");
        ac.sources.push_back("Cx");
        mini.package.argumentPackages.front().assertedContexts.push_back(ac);
    }
    std::string cg, rel, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "Cx"};
    ASSERT_TRUE(
        core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument, cg, rel, err))
        << err;

    const auto layout = LayoutOf(mini.model);
    const ui::gsn::LayoutNode* cx = FindLayout(layout, "Cx");
    const ui::gsn::LayoutNode* counter = FindLayout(layout, cg);
    ASSERT_NE(cx, nullptr);
    ASSERT_NE(counter, nullptr);

    // Directly below the context, roughly aligned horizontally.
    EXPECT_GE(counter->position.y, cx->position.y + cx->size.y - 1.0f);
    EXPECT_NEAR(counter->position.x + counter->size.x * 0.5f, cx->position.x + cx->size.x * 0.5f, 40.0f);
    ExpectNoOverlaps(layout);
}

TEST(DialecticChallengeLayout, InContextOfRelationshipChallengeSitsBelowContext) {
    ScopedImGuiFrame frame;
    // Challenge the InContextOf relationship (Cx -> G1). The counter must sit
    // below the CONTEXT node it points to, not below the parent claim G1.
    MiniCase mini;
    AddClaim(mini, "G1");
    AddArtifactReference(mini, "Cx");
    {
        parser::SacmElement rel;
        rel.id = "CX1";
        rel.type = "assertedcontext";
        rel.target_refs.push_back("G1");
        rel.source_refs.push_back("Cx");
        mini.model.elements.push_back(rel);
        sacm::AssertedContext ac;
        ac.id = "CX1";
        ac.targets.push_back("G1");
        ac.sources.push_back("Cx");
        mini.package.argumentPackages.front().assertedContexts.push_back(ac);
    }
    std::string cg, rel, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Relationship, "CX1"};
    ASSERT_TRUE(
        core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument, cg, rel, err))
        << err;

    const auto layout = LayoutOf(mini.model);
    const ui::gsn::LayoutNode* g1 = FindLayout(layout, "G1");
    const ui::gsn::LayoutNode* cx = FindLayout(layout, "Cx");
    const ui::gsn::LayoutNode* counter = FindLayout(layout, cg);
    ASSERT_NE(g1, nullptr);
    ASSERT_NE(cx, nullptr);
    ASSERT_NE(counter, nullptr);

    // Below the context, aligned with the context's column (not G1's).
    EXPECT_GE(counter->position.y, cx->position.y + cx->size.y - 1.0f);
    const float counter_cx = counter->position.x + counter->size.x * 0.5f;
    const float cx_cx = cx->position.x + cx->size.x * 0.5f;
    const float g1_cx = g1->position.x + g1->size.x * 0.5f;
    EXPECT_LT(std::fabs(counter_cx - cx_cx), std::fabs(counter_cx - g1_cx));
    ExpectNoOverlaps(layout);
}

TEST(DialecticChallengeLayout, AssumptionChallengePlacedToSide) {
    ScopedImGuiFrame frame;
    // An assumption is a side-attached assertion (claim-type), not a reference, so
    // challenging it places the counter BESIDE the assumption (on its outward lane
    // side), not below it.
    MiniCase mini;
    AddClaim(mini, "G1");
    AddAssumption(mini, "A1");
    AddContextLink(mini, "AC1", "G1", "A1");
    std::string cg, rel, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "A1"};
    ASSERT_TRUE(
        core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument, cg, rel, err))
        << err;

    const auto layout = LayoutOf(mini.model);
    const ui::gsn::LayoutNode* a1 = FindLayout(layout, "A1");
    const ui::gsn::LayoutNode* counter = FindLayout(layout, cg);
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(counter, nullptr);

    // Beside the assumption (left or right), roughly on the same row — not below.
    const bool to_right = counter->position.x >= a1->position.x + a1->size.x - 1.0f;
    const bool to_left = counter->position.x + counter->size.x <= a1->position.x + 1.0f;
    EXPECT_TRUE(to_right || to_left) << "counter to an assumption should sit beside it";
    EXPECT_LT(counter->position.y, a1->position.y + a1->size.y) << "counter should not be placed below the assumption";
    ExpectNoOverlaps(layout);
}

TEST(DialecticChallengeLayout, ChallengesToGoalAndAssumptionDoNotOverlap) {
    ScopedImGuiFrame frame;
    // Regression: challenging both a goal and its assumption used to drop both
    // counters into the same side lane at the same row, overlapping exactly.
    MiniCase mini;
    AddClaim(mini, "G1");
    AddAssumption(mini, "A1");
    AddContextLink(mini, "AC1", "G1", "A1");

    std::string cg_goal, rel_goal, err;
    core::ArgumentTarget goal_target{core::ArgumentTarget::Kind::Element, "G1"};
    ASSERT_TRUE(core::AddChallenge(
        mini.model, &mini.package, goal_target, core::ChallengeSourceType::CounterArgument, cg_goal, rel_goal, err))
        << err;
    std::string cg_assumption, rel_assumption, err2;
    core::ArgumentTarget assumption_target{core::ArgumentTarget::Kind::Element, "A1"};
    ASSERT_TRUE(core::AddChallenge(mini.model,
                                   &mini.package,
                                   assumption_target,
                                   core::ChallengeSourceType::CounterArgument,
                                   cg_assumption,
                                   rel_assumption,
                                   err2))
        << err2;

    const auto layout = LayoutOf(mini.model);
    const ui::gsn::LayoutNode* goal_counter = FindLayout(layout, cg_goal);
    const ui::gsn::LayoutNode* assumption_counter = FindLayout(layout, cg_assumption);
    ASSERT_NE(goal_counter, nullptr);
    ASSERT_NE(assumption_counter, nullptr);
    EXPECT_FALSE(InteriorsOverlap(*goal_counter, *assumption_counter))
        << "the goal's counter and the assumption's counter must not overlap";
    ExpectNoOverlaps(layout);
}

TEST(DialecticChallengeLayout, DevelopedChallengeOnNodePushesChildrenDown) {
    ScopedImGuiFrame frame;
    // A developed challenge on a strategy stays next to the strategy, and the
    // strategy's children are pushed DOWN below the challenge's depth (no sideways
    // stretching).
    MiniCase mini;
    AddClaim(mini, "G1");
    AddStrategy(mini, "S1");
    AddInference(mini, "INF_S", "G1", "", "S1");
    for (int i = 0; i < 4; ++i) {
        const std::string id = "G" + std::to_string(i + 2);
        AddClaim(mini, id);
        AddInference(mini, "INF" + id, "S1", id);
    }
    std::string cg, rel, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "S1"};
    ASSERT_TRUE(
        core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument, cg, rel, err))
        << err;
    // Develop the challenge with its own child so it spans more than one row.
    std::string child, child_rel, err2;
    ASSERT_TRUE(
        core::AddChildElement(mini.model, &mini.package, cg, core::NewElementKind::Goal, child, child_rel, err2))
        << err2;

    const auto layout = LayoutOf(mini.model);
    const ui::gsn::LayoutNode* s1 = FindLayout(layout, "S1");
    const ui::gsn::LayoutNode* counter = FindLayout(layout, cg);
    const ui::gsn::LayoutNode* counter_child = FindLayout(layout, child);
    const ui::gsn::LayoutNode* g2 = FindLayout(layout, "G2");
    ASSERT_NE(s1, nullptr);
    ASSERT_NE(counter, nullptr);
    ASSERT_NE(counter_child, nullptr);
    ASSERT_NE(g2, nullptr);

    // Counter sits beside S1 (adjacent, not drifted past the wide child row).
    const float gap_to_node = std::min(std::fabs(counter->position.x + counter->size.x - s1->position.x),
                                       std::fabs(counter->position.x - (s1->position.x + s1->size.x)));
    EXPECT_LT(gap_to_node, s1->size.x + 120.0f);
    // S1's children are pushed below the challenge's own developed depth.
    EXPECT_GE(g2->position.y, counter_child->position.y - 1.0f);
    // S1's child edges run straight down (to clear the challenge tree) before
    // curving out — a positive straight-drop hint.
    EXPECT_GT(s1->child_edge_drop, 0.0f);
    ExpectNoOverlaps(layout);
}

TEST(DialecticChallengeLayout, NoChallengeMeansNoExtraEdgeDrop) {
    ScopedImGuiFrame frame;
    // A plain strategy with children and no side challenge keeps the default edge
    // routing (no extra straight-down drop).
    MiniCase mini;
    AddClaim(mini, "G1");
    AddStrategy(mini, "S1");
    AddInference(mini, "INF_S", "G1", "", "S1");
    for (int i = 0; i < 3; ++i) {
        const std::string id = "G" + std::to_string(i + 2);
        AddClaim(mini, id);
        AddInference(mini, "INF" + id, "S1", id);
    }
    const auto layout = LayoutOf(mini.model);
    const ui::gsn::LayoutNode* s1 = FindLayout(layout, "S1");
    ASSERT_NE(s1, nullptr);
    EXPECT_NEAR(s1->child_edge_drop, 0.0f, 1.0f);
}

TEST(DialecticChallengeLayout, ContextChallengeTreePushesArgumentDown) {
    ScopedImGuiFrame frame;
    // G1 with a context Cx (left) and a structural child S1. The context is
    // challenged by a TREE (CG below Cx, with its own child). The challenge stays
    // in the context column and the main argument (S1) is pushed down so R1 is a
    // longer line — instead of the challenge stretching sideways.
    MiniCase mini;
    AddClaim(mini, "G1");
    AddArtifactReference(mini, "Cx");
    AddClaim(mini, "S1child"); // a structural child of G1 (the "main argument")
    AddInference(mini, "INF1", "G1", "S1child");
    {
        parser::SacmElement rel;
        rel.id = "CX1";
        rel.type = "assertedcontext";
        rel.target_refs.push_back("G1");
        rel.source_refs.push_back("Cx");
        mini.model.elements.push_back(rel);
        sacm::AssertedContext ac;
        ac.id = "CX1";
        ac.targets.push_back("G1");
        ac.sources.push_back("Cx");
        mini.package.argumentPackages.front().assertedContexts.push_back(ac);
    }
    std::string cg, rel, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "Cx"};
    ASSERT_TRUE(
        core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument, cg, rel, err))
        << err;
    // Develop the context's challenge into a tree.
    std::string cgChild, cgChildRel, err2;
    ASSERT_TRUE(
        core::AddChildElement(mini.model, &mini.package, cg, core::NewElementKind::Goal, cgChild, cgChildRel, err2))
        << err2;

    const auto layout = LayoutOf(mini.model);
    const ui::gsn::LayoutNode* g1 = FindLayout(layout, "G1");
    const ui::gsn::LayoutNode* cx = FindLayout(layout, "Cx");
    const ui::gsn::LayoutNode* s1 = FindLayout(layout, "S1child");
    const ui::gsn::LayoutNode* counter = FindLayout(layout, cg);
    ASSERT_NE(g1, nullptr);
    ASSERT_NE(cx, nullptr);
    ASSERT_NE(s1, nullptr);
    ASSERT_NE(counter, nullptr);

    // The main argument is pushed down (R1 longer): S1 sits well below where it
    // would be if it were the immediate child row under G1.
    EXPECT_GT(s1->position.y, cx->position.y + cx->size.y);
    // The challenge stays in the context column (aligned under Cx), not stretched
    // far to the side.
    EXPECT_NEAR(counter->position.x + counter->size.x * 0.5f, cx->position.x + cx->size.x * 0.5f, 60.0f);
    ExpectNoOverlaps(layout);
}

TEST(DialecticChallengeLayout, ChallengedAssumptionStaysSubstackedBelowContext) {
    ScopedImGuiFrame frame;
    // C1, A1, C2 put C1+A1 in the same (left) lane. Challenging A1 must NOT pull it
    // out beside C1 — attachments always substack, so A1 stays below C1 and its
    // counter sits beside A1.
    MiniCase mini;
    AddClaim(mini, "G1");
    AddArtifactReference(mini, "C1");
    AddAssumption(mini, "A1");
    AddArtifactReference(mini, "C2");
    AddContextLink(mini, "AC1", "G1", "C1");
    AddContextLink(mini, "AC2", "G1", "A1");
    AddContextLink(mini, "AC3", "G1", "C2");
    std::string cg, rg, e;
    ASSERT_TRUE(core::AddChallenge(mini.model,
                                   &mini.package,
                                   {core::ArgumentTarget::Kind::Element, "A1"},
                                   core::ChallengeSourceType::CounterArgument,
                                   cg,
                                   rg,
                                   e))
        << e;

    const auto layout = LayoutOf(mini.model);
    const ui::gsn::LayoutNode* c1 = FindLayout(layout, "C1");
    const ui::gsn::LayoutNode* a1 = FindLayout(layout, "A1");
    const ui::gsn::LayoutNode* counter = FindLayout(layout, cg);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(counter, nullptr);

    // A1 sits directly below C1 (same column), not beside it.
    EXPECT_GT(a1->position.y, c1->position.y + c1->size.y - 1.0f) << "A1 should stay substacked below C1";
    EXPECT_NEAR(a1->position.x, c1->position.x, 1.0f) << "A1 should share C1's column, not move to its own";
    // The counter sits beside A1.
    const bool to_right = counter->position.x >= a1->position.x + a1->size.x - 1.0f;
    const bool to_left = counter->position.x + counter->size.x <= a1->position.x + 1.0f;
    EXPECT_TRUE(to_right || to_left) << "the assumption's counter should sit beside it";
    ExpectNoOverlaps(layout);
}

TEST(DialecticChallengeLayout, ChallengesToTwoSameSideAssumptionsDoNotOverlap) {
    ScopedImGuiFrame frame;
    // Three assumptions put two on the same lane; challenging both used to drop
    // their counters onto the same spot (shared lane centre). Each challenged
    // attachment now gets its own column.
    MiniCase mini;
    AddClaim(mini, "G1");
    AddAssumption(mini, "A1");
    AddAssumption(mini, "A2");
    AddAssumption(mini, "A3");
    AddContextLink(mini, "AC1", "G1", "A1");
    AddContextLink(mini, "AC2", "G1", "A2");
    AddContextLink(mini, "AC3", "G1", "A3");
    std::string a, b, ra, rb, e;
    ASSERT_TRUE(core::AddChallenge(mini.model,
                                   &mini.package,
                                   {core::ArgumentTarget::Kind::Element, "A1"},
                                   core::ChallengeSourceType::CounterArgument,
                                   a,
                                   ra,
                                   e))
        << e;
    ASSERT_TRUE(core::AddChallenge(mini.model,
                                   &mini.package,
                                   {core::ArgumentTarget::Kind::Element, "A2"},
                                   core::ChallengeSourceType::CounterArgument,
                                   b,
                                   rb,
                                   e))
        << e;
    ExpectNoOverlaps(LayoutOf(mini.model));
}

TEST(DialecticChallengeLayout, ContextAvoidsHostChallengeLane) {
    ScopedImGuiFrame frame;
    // Regression: a node challenged on one side, with a context attached to the
    // SAME node, used to drop the context into the challenge's lane — between the
    // challenge cluster and the host — so the context overlapped the challenge
    // edge. The context must move to the opposite (free) lane.
    MiniCase mini;
    AddClaim(mini, "G1");
    AddStrategy(mini, "S1");
    AddInference(mini, "INF_S", "G1", "", "S1"); // strategy under the root goal
    AddArtifactReference(mini, "C13");
    AddContextLink(mini, "CTX", "S1", "C13"); // context attached to the strategy
    std::string cg, rel, err;
    ASSERT_TRUE(core::AddChallenge(mini.model,
                                   &mini.package,
                                   {core::ArgumentTarget::Kind::Element, "S1"},
                                   core::ChallengeSourceType::CounterArgument,
                                   cg,
                                   rel,
                                   err))
        << err;

    const auto layout = LayoutOf(mini.model);
    const ui::gsn::LayoutNode* s1 = FindLayout(layout, "S1");
    const ui::gsn::LayoutNode* c13 = FindLayout(layout, "C13");
    const ui::gsn::LayoutNode* counter = FindLayout(layout, cg);
    ASSERT_NE(s1, nullptr);
    ASSERT_NE(c13, nullptr);
    ASSERT_NE(counter, nullptr);

    // The context and the challenge sit on opposite sides of the host.
    const float s1_cx = s1->position.x + s1->size.x * 0.5f;
    const float c13_cx = c13->position.x + c13->size.x * 0.5f;
    const float counter_cx = counter->position.x + counter->size.x * 0.5f;
    EXPECT_LT((c13_cx - s1_cx) * (counter_cx - s1_cx), 0.0f)
        << "context and challenge should be on opposite sides of the host";
    ExpectNoOverlaps(layout);
}

TEST(DialecticChallengeLayout, TallChallengeSitsAloneOppositeContexts) {
    ScopedImGuiFrame frame;
    // Height-balanced lanes: a tall challenge cluster (challenge + a two-deep
    // child chain, height 3) opposite exactly 3 single-row contexts. The contexts
    // should fill the free lane until its height matches the challenge — i.e. all
    // three land on the side opposite the (alone) challenge.
    MiniCase mini;
    AddClaim(mini, "G1");
    for (int i = 1; i <= 3; ++i) {
        const std::string c = "C" + std::to_string(i);
        AddArtifactReference(mini, c);
        AddContextLink(mini, "CTX" + std::to_string(i), "G1", c);
    }
    std::string cg, rel, err;
    ASSERT_TRUE(core::AddChallenge(mini.model,
                                   &mini.package,
                                   {core::ArgumentTarget::Kind::Element, "G1"},
                                   core::ChallengeSourceType::CounterArgument,
                                   cg,
                                   rel,
                                   err))
        << err;
    std::string child1, child2, r1, r2, e1, e2;
    ASSERT_TRUE(core::AddChildElement(mini.model, &mini.package, cg, core::NewElementKind::Goal, child1, r1, e1)) << e1;
    ASSERT_TRUE(core::AddChildElement(mini.model, &mini.package, child1, core::NewElementKind::Goal, child2, r2, e2))
        << e2;

    const auto layout = LayoutOf(mini.model);
    const ui::gsn::LayoutNode* g1 = FindLayout(layout, "G1");
    const ui::gsn::LayoutNode* counter = FindLayout(layout, cg);
    ASSERT_NE(g1, nullptr);
    ASSERT_NE(counter, nullptr);
    const float g1_cx = g1->position.x + g1->size.x * 0.5f;
    const float counter_cx = counter->position.x + counter->size.x * 0.5f;

    for (int i = 1; i <= 3; ++i) {
        const ui::gsn::LayoutNode* c = FindLayout(layout, "C" + std::to_string(i));
        ASSERT_NE(c, nullptr);
        const float c_cx = c->position.x + c->size.x * 0.5f;
        EXPECT_LT((c_cx - g1_cx) * (counter_cx - g1_cx), 0.0f)
            << "C" << i << " should sit opposite the tall challenge cluster";
    }
    ExpectNoOverlaps(layout);
}

TEST(DialecticChallengeLayout, HeightBalancedContextsUseBothLanes) {
    ScopedImGuiFrame frame;
    // When the contexts out-height the challenge, the balancer fills the free lane
    // first and only then doubles up on the challenge's lane. With a height-2
    // challenge and 4 contexts, contexts end up on BOTH sides; the one sharing the
    // challenge's lane stacks ABOVE the challenge (at the host row) in one column,
    // so the challenge edge stays clear.
    MiniCase mini;
    AddClaim(mini, "G1");
    for (int i = 1; i <= 4; ++i) {
        const std::string c = "C" + std::to_string(i);
        AddArtifactReference(mini, c);
        AddContextLink(mini, "CTX" + std::to_string(i), "G1", c);
    }
    std::string cg, rel, err;
    ASSERT_TRUE(core::AddChallenge(mini.model,
                                   &mini.package,
                                   {core::ArgumentTarget::Kind::Element, "G1"},
                                   core::ChallengeSourceType::CounterArgument,
                                   cg,
                                   rel,
                                   err))
        << err;
    std::string child, rc, ec;
    ASSERT_TRUE(core::AddChildElement(mini.model, &mini.package, cg, core::NewElementKind::Goal, child, rc, ec)) << ec;

    const auto layout = LayoutOf(mini.model);
    const ui::gsn::LayoutNode* g1 = FindLayout(layout, "G1");
    const ui::gsn::LayoutNode* counter = FindLayout(layout, cg);
    ASSERT_NE(g1, nullptr);
    ASSERT_NE(counter, nullptr);
    const float g1_cx = g1->position.x + g1->size.x * 0.5f;
    const float counter_cx = counter->position.x + counter->size.x * 0.5f;

    int left_contexts = 0;
    int right_contexts = 0;
    for (int i = 1; i <= 4; ++i) {
        const ui::gsn::LayoutNode* c = FindLayout(layout, "C" + std::to_string(i));
        ASSERT_NE(c, nullptr);
        const float c_cx = c->position.x + c->size.x * 0.5f;
        if (c_cx < g1_cx)
            ++left_contexts;
        else
            ++right_contexts;
        // A context sharing the challenge's lane must sit above it (host row), so
        // the challenge edge fanning up to the host never crosses the context.
        const bool same_side_as_challenge = (c_cx - g1_cx) * (counter_cx - g1_cx) > 0.0f;
        if (same_side_as_challenge) {
            EXPECT_LT(c->position.y, counter->position.y)
                << "C" << i << " shares the challenge lane and must stack above the challenge";
        }
    }
    EXPECT_GT(left_contexts, 0) << "contexts should be balanced across both lanes";
    EXPECT_GT(right_contexts, 0) << "contexts should be balanced across both lanes";
    ExpectNoOverlaps(layout);
}

TEST(DialecticChallengeLayout, ChallengesToTwoSameSideContextsDoNotOverlap) {
    ScopedImGuiFrame frame;
    // Same hazard for contexts: two contexts in one lane, each challenged below,
    // used to drop their counters onto the same spot.
    MiniCase mini;
    AddClaim(mini, "G1");
    AddArtifactReference(mini, "C1");
    AddArtifactReference(mini, "C2");
    AddArtifactReference(mini, "C3");
    AddContextLink(mini, "AC1", "G1", "C1");
    AddContextLink(mini, "AC2", "G1", "C2");
    AddContextLink(mini, "AC3", "G1", "C3");
    std::string a, b, ra, rb, e;
    ASSERT_TRUE(core::AddChallenge(mini.model,
                                   &mini.package,
                                   {core::ArgumentTarget::Kind::Element, "C1"},
                                   core::ChallengeSourceType::CounterArgument,
                                   a,
                                   ra,
                                   e))
        << e;
    ASSERT_TRUE(core::AddChallenge(mini.model,
                                   &mini.package,
                                   {core::ArgumentTarget::Kind::Element, "C2"},
                                   core::ChallengeSourceType::CounterArgument,
                                   b,
                                   rb,
                                   e))
        << e;
    ExpectNoOverlaps(LayoutOf(mini.model));
}

TEST(DialecticChallengeLayout, GoalAndSameSideAssumptionChallengesDoNotOverlap) {
    ScopedImGuiFrame frame;
    // A sub-goal whose outward side is the same lane as its assumption: the goal's
    // own counter and the assumption's counter must share the lane without
    // overlapping (separate columns).
    MiniCase mini;
    AddClaim(mini, "G0");
    AddClaim(mini, "G1");
    AddInference(mini, "INF1", "G0", "G1");
    AddAssumption(mini, "A1");
    AddContextLink(mini, "AC1", "G1", "A1");
    std::string cg, cga, rg, rga, e;
    ASSERT_TRUE(core::AddChallenge(mini.model,
                                   &mini.package,
                                   {core::ArgumentTarget::Kind::Element, "G1"},
                                   core::ChallengeSourceType::CounterArgument,
                                   cg,
                                   rg,
                                   e))
        << e;
    ASSERT_TRUE(core::AddChallenge(mini.model,
                                   &mini.package,
                                   {core::ArgumentTarget::Kind::Element, "A1"},
                                   core::ChallengeSourceType::CounterArgument,
                                   cga,
                                   rga,
                                   e))
        << e;
    ExpectNoOverlaps(LayoutOf(mini.model));
}

TEST(DialecticChallengeLayout, TwoChallengesToSameNodeDoNotOverlap) {
    ScopedImGuiFrame frame;
    // Two counters against the same node land on the same side; each gets its own
    // column instead of stacking onto the same point.
    MiniCase mini;
    AddClaim(mini, "G1");
    std::string a, b, ra, rb, e;
    ASSERT_TRUE(core::AddChallenge(mini.model,
                                   &mini.package,
                                   {core::ArgumentTarget::Kind::Element, "G1"},
                                   core::ChallengeSourceType::CounterArgument,
                                   a,
                                   ra,
                                   e))
        << e;
    ASSERT_TRUE(core::AddChallenge(mini.model,
                                   &mini.package,
                                   {core::ArgumentTarget::Kind::Element, "G1"},
                                   core::ChallengeSourceType::CounterArgument,
                                   b,
                                   rb,
                                   e))
        << e;
    ExpectNoOverlaps(LayoutOf(mini.model));
}

TEST(DialecticChallengeLayout, DevelopedChallengeStaysClear) {
    ScopedImGuiFrame frame;
    MiniCase mini = MakeBaseCase(); // G1 <- G2
    std::string cg, rel, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "G1"};
    ASSERT_TRUE(
        core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument, cg, rel, err))
        << err;
    // Develop the counter with its own sub-claim.
    std::string child, child_rel, err2;
    ASSERT_TRUE(
        core::AddChildElement(mini.model, &mini.package, cg, core::NewElementKind::Goal, child, child_rel, err2))
        << err2;

    const auto layout = LayoutOf(mini.model);
    const ui::gsn::LayoutNode* counter = FindLayout(layout, cg);
    const ui::gsn::LayoutNode* counter_child = FindLayout(layout, child);
    ASSERT_NE(counter, nullptr);
    ASSERT_NE(counter_child, nullptr);

    // The counter's own child is laid out below it, and nothing overlaps.
    EXPECT_GE(counter_child->position.y, counter->position.y + counter->size.y - 1.0f);
    ExpectNoOverlaps(layout);
}
