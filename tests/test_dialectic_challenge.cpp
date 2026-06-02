// Tests for GSN v3 dialectic challenge support: counter argument / counter
// evidence creation (core::AddChallenge), counter-aware tree building, removal,
// and isCounter round-trip through the parser and SACM serializer.

#include "core/assurance_tree.h"
#include "core/element_factory.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"
#include "sacm/sacm_parser.h"
#include "sacm/sacm_serializer.h"

#include <algorithm>
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

void AddInference(MiniCase& mini_case,
                  const std::string& id,
                  const std::string& target,
                  const std::string& source) {
    parser::SacmElement relationship;
    relationship.id = id;
    relationship.type = "assertedinference";
    relationship.target_refs.push_back(target);
    relationship.source_refs.push_back(source);
    mini_case.model.elements.push_back(relationship);

    if (mini_case.package.argumentPackages.empty())
        mini_case.package.argumentPackages.emplace_back();
    sacm::AssertedInference inference;
    inference.id = id;
    inference.targets.push_back(target);
    inference.sources.push_back(source);
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
    ASSERT_TRUE(core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument,
                                   /*create_as_undeveloped=*/true, new_id, rel_id, err))
        << err;

    EXPECT_EQ(new_id.rfind("CG", 0), 0u) << "counter argument id should use CG prefix, got " << new_id;

    const parser::SacmElement* counter = FindElement(mini.model, new_id);
    ASSERT_NE(counter, nullptr);
    EXPECT_EQ(counter->type, "claim");
    EXPECT_TRUE(counter->undeveloped);

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
    auto it = std::find_if(inferences.begin(), inferences.end(),
                           [&](const sacm::AssertedInference& i) { return i.id == rel_id; });
    ASSERT_NE(it, inferences.end());
    EXPECT_TRUE(it->isCounter);
}

// ===== Creation: counter evidence against an element =====

TEST(DialecticChallenge, CounterEvidenceAgainstElement) {
    MiniCase mini = MakeBaseCase();
    std::string new_id, rel_id, err;

    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "G2"};
    ASSERT_TRUE(core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterEvidence,
                                   /*create_as_undeveloped=*/false, new_id, rel_id, err))
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
    auto it = std::find_if(evidences.begin(), evidences.end(),
                           [&](const sacm::AssertedEvidence& e) { return e.id == rel_id; });
    ASSERT_NE(it, evidences.end());
    EXPECT_TRUE(it->isCounter);
}

// ===== Creation: challenge against a relationship (incl. a Challenges rel) =====

TEST(DialecticChallenge, CounterArgumentAgainstRelationship) {
    MiniCase mini = MakeBaseCase();
    std::string new_id, rel_id, err;

    core::ArgumentTarget target{core::ArgumentTarget::Kind::Relationship, "INF1"};
    ASSERT_TRUE(core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument,
                                   true, new_id, rel_id, err))
        << err;

    const parser::SacmElement* rel = FindElement(mini.model, rel_id);
    ASSERT_NE(rel, nullptr);
    EXPECT_TRUE(rel->is_counter);
    ASSERT_EQ(rel->target_refs.size(), 1u);
    EXPECT_EQ(rel->target_refs.front(), "INF1");

    // A challenge may itself be challenged (challenge-to-challenge).
    std::string new_id2, rel_id2, err2;
    core::ArgumentTarget counter_target{core::ArgumentTarget::Kind::Relationship, rel_id};
    EXPECT_TRUE(core::AddChallenge(mini.model, &mini.package, counter_target,
                                   core::ChallengeSourceType::CounterArgument, true, new_id2, rel_id2, err2))
        << err2;
    const parser::SacmElement* rel2 = FindElement(mini.model, rel_id2);
    ASSERT_NE(rel2, nullptr);
    EXPECT_EQ(rel2->target_refs.front(), rel_id);
}

TEST(DialecticChallenge, RejectsMissingTarget) {
    MiniCase mini = MakeBaseCase();
    std::string new_id, rel_id, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "does-not-exist"};
    EXPECT_FALSE(core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument,
                                    true, new_id, rel_id, err));
    EXPECT_FALSE(err.empty());
}

// ===== Tree build: counter source is flagged, not wired as ordinary support =====

TEST(DialecticChallenge, TreeBuildMarksCounterSource) {
    MiniCase mini = MakeBaseCase();
    std::string new_id, rel_id, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "G1"};
    ASSERT_TRUE(core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument,
                                   true, new_id, rel_id, err))
        << err;

    core::AssuranceTree tree = core::AssuranceTree::Build(mini.model);
    const core::TreeNode* counter = FindNode(tree, new_id);
    ASSERT_NE(counter, nullptr);
    EXPECT_TRUE(counter->is_counter_source);
    EXPECT_FALSE(counter->challenge_target_is_relationship);
    EXPECT_EQ(counter->challenge_target_id, "G1");
    // Wired under the target element for layout, but flagged as a challenge.
    ASSERT_NE(counter->parent, nullptr);
    EXPECT_EQ(counter->parent->id, "G1");
}

TEST(DialecticChallenge, TreeBuildResolvesRelationshipTargetToElement) {
    MiniCase mini = MakeBaseCase();
    std::string new_id, rel_id, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Relationship, "INF1"};
    ASSERT_TRUE(core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument,
                                   true, new_id, rel_id, err))
        << err;

    core::AssuranceTree tree = core::AssuranceTree::Build(mini.model);
    const core::TreeNode* counter = FindNode(tree, new_id);
    ASSERT_NE(counter, nullptr);
    EXPECT_TRUE(counter->is_counter_source);
    EXPECT_TRUE(counter->challenge_target_is_relationship);
    EXPECT_EQ(counter->challenge_target_id, "INF1");
    // INF1 targets G1, so the counter anchors under G1 for layout.
    ASSERT_NE(counter->parent, nullptr);
    EXPECT_EQ(counter->parent->id, "G1");
}

// ===== Removal: deleting a counter node drops its challenge, keeps the target =====

TEST(DialecticChallenge, RemovingCounterKeepsTarget) {
    MiniCase mini = MakeBaseCase();
    std::string new_id, rel_id, err;
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, "G1"};
    ASSERT_TRUE(core::AddChallenge(mini.model, &mini.package, target, core::ChallengeSourceType::CounterArgument,
                                   true, new_id, rel_id, err))
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

    auto inf = std::find_if(ap.assertedInferences.begin(), ap.assertedInferences.end(),
                            [](const sacm::AssertedInference& i) { return i.id == "CH1"; });
    ASSERT_NE(inf, ap.assertedInferences.end());
    EXPECT_TRUE(inf->isCounter);

    auto ev = std::find_if(ap.assertedEvidences.begin(), ap.assertedEvidences.end(),
                           [](const sacm::AssertedEvidence& e) { return e.id == "CH2"; });
    ASSERT_NE(ev, ap.assertedEvidences.end());
    EXPECT_TRUE(ev->isCounter);
}
