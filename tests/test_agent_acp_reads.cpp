#include "agent/operations.h"

#include "core/app_state.h"

#include <gtest/gtest.h>

#include <string>

// Assurance claim points over the agent read surface. Before this, MCP exposed
// a bare ACP count and nothing else: an agent could not see where the
// argument's own assurance is questioned, which is exactly the place it should
// read before proposing changes. Read-only by design -- the patch vocabulary
// has no ACP operation, and extending it is an ADR 0009 decision with its own
// GSN review.

namespace {

parser::SacmElement Element(const std::string& id, const std::string& type, const std::string& name) {
    parser::SacmElement element;
    element.id = id;
    element.type = type;
    element.name = name;
    return element;
}

parser::AssuranceCase CaseWithClaimPoints() {
    parser::AssuranceCase model;
    model.id = "case-acp";
    model.name = "Case with claim points";

    model.elements.push_back(Element("G1", "claim", "Top goal"));
    model.elements.push_back(Element("Sn1", "artifactreference", "Test evidence"));
    parser::SacmElement support = Element("R1", "assertedinference", "");
    support.source_refs = {"Sn1"};
    support.target_refs = {"G1"};
    model.elements.push_back(support);

    // An instantiated element ACP: resolved by a separate confidence argument.
    parser::AcpRecord on_solution;
    on_solution.id = "ACP1";
    on_solution.name = "Evidence sufficiency";
    on_solution.target_kind = "element";
    on_solution.target_id = "Sn1";
    on_solution.resolution_kind = "topGoalReference";
    on_solution.confidence_claim_id = "CC1";
    on_solution.argument_package_id = "AP_CONF";
    on_solution.top_goal_id = "CG1";
    model.acps.push_back(on_solution);

    // An uninstantiated relationship ACP: declared, not yet developed.
    parser::AcpRecord on_support;
    on_support.id = "ACP2";
    on_support.target_kind = "relationship";
    on_support.target_id = "R1";
    on_support.resolution_kind = "none";
    model.acps.push_back(on_support);

    return model;
}

struct Fixture {
    core::AppState state;
    Fixture() {
        state.loaded_case = CaseWithClaimPoints();
        state.loaded_file_path = "arguments/main.sacm";
    }
    agent::ReadContext Context() const {
        return agent::ReadContext{state, "project"};
    }
};

const nlohmann::json* FindById(const nlohmann::json& array, const std::string& id) {
    for (const nlohmann::json& entry : array) {
        if (entry.value("id", "") == id) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

TEST(AgentAcpReads, ListsEveryClaimPointWithItsTargetAndResolution) {
    Fixture fixture;

    const agent::Result result = agent::ListAssuranceClaimPoints(fixture.Context());
    ASSERT_FALSE(result.is_error) << result.payload.dump();
    EXPECT_EQ(result.payload["count"], 2);
    EXPECT_EQ(result.payload["view"], "accepted");

    const nlohmann::json& points = result.payload["assurance_claim_points"];
    const nlohmann::json* resolved = FindById(points, "ACP1");
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ((*resolved)["name"], "Evidence sufficiency");
    EXPECT_EQ((*resolved)["target_kind"], "element");
    EXPECT_EQ((*resolved)["target_id"], "Sn1");
    EXPECT_EQ((*resolved)["resolution_kind"], "topGoalReference");
    EXPECT_TRUE((*resolved)["instantiated"].get<bool>());
    // The confidence-argument link an agent can follow with get_element.
    EXPECT_EQ((*resolved)["confidence_claim_id"], "CC1");
    EXPECT_EQ((*resolved)["confidence_argument_package_id"], "AP_CONF");
    EXPECT_EQ((*resolved)["confidence_top_goal_id"], "CG1");

    const nlohmann::json* declared = FindById(points, "ACP2");
    ASSERT_NE(declared, nullptr);
    EXPECT_EQ((*declared)["target_kind"], "relationship");
    EXPECT_EQ((*declared)["target_id"], "R1");
    EXPECT_FALSE((*declared)["instantiated"].get<bool>());
    // Absent, not empty: "no confidence argument yet" reads from absence.
    EXPECT_FALSE(declared->contains("confidence_claim_id"));
    EXPECT_FALSE(declared->contains("text"));
}

// An agent asking about the solution must see both the ACP on the solution
// itself and the one riding on its SupportedBy, because "is this evidence's
// contribution assured?" is answered by either.
TEST(AgentAcpReads, GetElementCarriesDirectAndRelationshipBorneClaimPoints) {
    Fixture fixture;

    const agent::Result result = agent::GetElement(fixture.Context(), nlohmann::json{{"id", "Sn1"}});
    ASSERT_FALSE(result.is_error) << result.payload.dump();
    ASSERT_TRUE(result.payload.contains("assurance_claim_points")) << result.payload.dump();

    const nlohmann::json& points = result.payload["assurance_claim_points"];
    const nlohmann::json* direct = FindById(points, "ACP1");
    ASSERT_NE(direct, nullptr);
    EXPECT_FALSE(direct->contains("via_relationship_id")) << "a direct ACP must not claim to ride a relationship";

    const nlohmann::json* via_support = FindById(points, "ACP2");
    ASSERT_NE(via_support, nullptr);
    EXPECT_EQ((*via_support)["via_relationship_id"], "R1");
}

TEST(AgentAcpReads, GetElementDoesNotAttachOtherElementsClaimPoints) {
    Fixture fixture;

    const agent::Result result = agent::GetElement(fixture.Context(), nlohmann::json{{"id", "G1"}});
    ASSERT_FALSE(result.is_error) << result.payload.dump();
    ASSERT_TRUE(result.payload.contains("assurance_claim_points")) << result.payload.dump();

    const nlohmann::json& points = result.payload["assurance_claim_points"];
    // G1 touches R1, so the relationship ACP appears -- but ACP1 belongs to
    // Sn1 and must not leak here.
    EXPECT_NE(FindById(points, "ACP2"), nullptr);
    EXPECT_EQ(FindById(points, "ACP1"), nullptr);
}

TEST(AgentAcpReads, ElementWithoutClaimPointsOmitsTheField) {
    Fixture fixture;
    fixture.state.loaded_case->acps.clear();

    const agent::Result result = agent::GetElement(fixture.Context(), nlohmann::json{{"id", "G1"}});
    ASSERT_FALSE(result.is_error) << result.payload.dump();
    EXPECT_FALSE(result.payload.contains("assurance_claim_points"));

    const agent::Result list = agent::ListAssuranceClaimPoints(fixture.Context());
    ASSERT_FALSE(list.is_error);
    EXPECT_EQ(list.payload["count"], 0);
}
