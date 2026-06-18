#include "core/pattern_model.h"
#include "sacm/pattern_keys.h"
#include "sacm/sacm_package_tree.h"

#include <gtest/gtest.h>

namespace keys = sacm::pattern_keys;

// ===== Generic tagged-value helpers =====

TEST(PatternTaggedValues, SetUpsertsSingleEntry) {
    sacm::Claim element;
    core::SetTaggedValue(element, keys::kIntent, "first");
    core::SetTaggedValue(element, keys::kIntent, "second");
    ASSERT_EQ(element.taggedValues.size(), 1u);
    EXPECT_EQ(element.taggedValues[0].value, "second");
    EXPECT_EQ(core::GetTaggedValue(element, keys::kIntent), std::optional<std::string>("second"));
}

TEST(PatternTaggedValues, RemoveReportsWhetherPresent) {
    sacm::Claim element;
    core::SetTaggedValue(element, keys::kIntent, "x");
    EXPECT_TRUE(core::RemoveTaggedValue(element, keys::kIntent));
    EXPECT_FALSE(core::RemoveTaggedValue(element, keys::kIntent));
    EXPECT_FALSE(core::HasTaggedValue(element, keys::kIntent));
}

// ===== Package classification =====

TEST(PatternClassification, RequiresAbstractAndViewKind) {
    sacm::ArgumentPackage pkg;
    EXPECT_FALSE(core::IsPatternPackage(pkg));

    pkg.isAbstract = true;
    EXPECT_FALSE(core::IsPatternPackage(pkg)) << "abstract alone is not a pattern";

    core::SetTaggedValue(pkg, keys::kViewKind, std::string(keys::kViewKindPatternValue));
    EXPECT_TRUE(core::IsPatternPackage(pkg));

    pkg.isAbstract = false;
    EXPECT_FALSE(core::IsPatternPackage(pkg)) << "view.kind without abstract is not a pattern";
}

TEST(PatternClassification, PackageTreeFlagsPatternNode) {
    const char* xml = R"(<?xml version="1.0"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="P" name="P">
  <argumentPackage id="NORMAL" name="Normal" />
  <argumentPackage id="PAT" name="Pattern" isAbstract="true">
    <taggedValue key="assuranceforge.view.kind" value="gsn-pattern" />
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    const sacm::SacmPackageTreeResult result = sacm::build_sacm_package_tree_from_string(xml);
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.root.children.size(), 1u);
    const sacm::SacmPackageTreeNode& acp = result.root.children[0];
    ASSERT_EQ(acp.children.size(), 2u);

    const sacm::SacmPackageTreeNode& normal = acp.children[0];
    const sacm::SacmPackageTreeNode& pattern = acp.children[1];
    EXPECT_FALSE(normal.isPattern);
    EXPECT_FALSE(normal.isAbstract);
    EXPECT_TRUE(pattern.isAbstract);
    EXPECT_TRUE(pattern.isPattern);
}

// ===== Element abstraction =====

TEST(PatternElementAbstraction, UninstantiatedToggle) {
    sacm::Claim element;
    EXPECT_FALSE(core::IsElementUninstantiated(element));
    core::SetElementUninstantiated(element, true);
    EXPECT_TRUE(core::IsElementUninstantiated(element));
    core::SetElementUninstantiated(element, false);
    EXPECT_FALSE(core::IsElementUninstantiated(element));
    EXPECT_TRUE(element.taggedValues.empty());
}

// ===== Cardinality =====

TEST(PatternCardinality, BoundTokenRoundTrip) {
    using Kind = core::PatternBound::Kind;

    core::PatternBound integer{Kind::Integer, 5, ""};
    EXPECT_EQ(core::PatternBoundToToken(integer), "5");
    EXPECT_EQ(core::PatternBoundFromToken("5").kind, Kind::Integer);
    EXPECT_EQ(core::PatternBoundFromToken("5").integerValue, 5);

    core::PatternBound unbounded{Kind::Unbounded, 0, ""};
    EXPECT_EQ(core::PatternBoundToToken(unbounded), "unbounded");
    EXPECT_EQ(core::PatternBoundFromToken("unbounded").kind, Kind::Unbounded);
    EXPECT_EQ(core::PatternBoundFromToken("*").kind, Kind::Unbounded);

    core::PatternBound param{Kind::Parameter, 1, "n"};
    EXPECT_EQ(core::PatternBoundToToken(param), "n");
    EXPECT_EQ(core::PatternBoundFromToken("n").kind, Kind::Parameter);
    EXPECT_EQ(core::PatternBoundFromToken("n").parameterName, "n");
}

TEST(PatternCardinality, Validation) {
    using Kind = core::PatternBound::Kind;
    std::string err;

    core::PatternCardinality ok{{Kind::Integer, 1, ""}, {Kind::Unbounded, 0, ""}, "1..*"};
    EXPECT_TRUE(core::ValidateCardinality(ok, err)) << err;

    core::PatternCardinality min_gt_max{{Kind::Integer, 5, ""}, {Kind::Integer, 2, ""}, "5..2"};
    EXPECT_FALSE(core::ValidateCardinality(min_gt_max, err));

    core::PatternCardinality negative{{Kind::Integer, -1, ""}, {Kind::Integer, 2, ""}, ""};
    EXPECT_FALSE(core::ValidateCardinality(negative, err));

    // A parameter upper bound suppresses the numeric ordering check.
    core::PatternCardinality parametric{{Kind::Integer, 3, ""}, {Kind::Parameter, 1, "n"}, "3..n"};
    EXPECT_TRUE(core::ValidateCardinality(parametric, err)) << err;
}

// ===== Relationship abstraction =====

TEST(PatternRelationshipData, MultiplicityRoundTrip) {
    using Kind = core::PatternBound::Kind;
    sacm::AssertedInference rel;

    core::PatternRelationshipData data;
    data.relationOperator = core::PatternRelationOperator::Multiplicity;
    data.multiplicity = core::PatternCardinality{{Kind::Integer, 1, ""}, {Kind::Unbounded, 0, ""}, "1..*"};
    core::WritePatternRelationshipData(rel, data);

    const core::PatternRelationshipData read = core::ReadPatternRelationshipData(rel);
    EXPECT_EQ(read.relationOperator, core::PatternRelationOperator::Multiplicity);
    ASSERT_TRUE(read.multiplicity.has_value());
    EXPECT_EQ(read.multiplicity->minimum.kind, Kind::Integer);
    EXPECT_EQ(read.multiplicity->minimum.integerValue, 1);
    EXPECT_EQ(read.multiplicity->maximum.kind, Kind::Unbounded);
    EXPECT_EQ(read.multiplicity->displayExpression, "1..*");
}

TEST(PatternRelationshipData, OptionalClearsCardinality) {
    sacm::AssertedInference rel;

    core::PatternRelationshipData multiplicity;
    multiplicity.relationOperator = core::PatternRelationOperator::Multiplicity;
    multiplicity.multiplicity = core::PatternCardinality{};
    core::WritePatternRelationshipData(rel, multiplicity);

    core::PatternRelationshipData optional;
    optional.relationOperator = core::PatternRelationOperator::Optional;
    core::WritePatternRelationshipData(rel, optional);

    const core::PatternRelationshipData read = core::ReadPatternRelationshipData(rel);
    EXPECT_EQ(read.relationOperator, core::PatternRelationOperator::Optional);
    EXPECT_FALSE(read.multiplicity.has_value());
    EXPECT_FALSE(core::HasTaggedValue(rel, keys::kCardinalityMinimum));
}

TEST(PatternRelationshipData, ChoiceGroupMembership) {
    sacm::AssertedInference rel;
    core::PatternRelationshipData data;
    data.choiceGroupId = "group-1";
    core::WritePatternRelationshipData(rel, data);

    EXPECT_EQ(core::ReadPatternRelationshipData(rel).choiceGroupId, std::optional<std::string>("group-1"));

    data.choiceGroupId.reset();
    core::WritePatternRelationshipData(rel, data);
    EXPECT_FALSE(core::ReadPatternRelationshipData(rel).choiceGroupId.has_value());
}

// ===== Choice groups =====

TEST(PatternChoiceGroup, Invariants) {
    std::string err;

    core::PatternChoiceGroup valid;
    valid.id = "g1";
    valid.sourceElement = "G1";
    valid.relationshipType = "assertedinference";
    valid.alternatives = {"R1", "R2"};
    EXPECT_TRUE(core::ValidateChoiceGroup(valid, err)) << err;

    core::PatternChoiceGroup too_few = valid;
    too_few.alternatives = {"R1"};
    EXPECT_FALSE(core::ValidateChoiceGroup(too_few, err));

    core::PatternChoiceGroup duplicate = valid;
    duplicate.alternatives = {"R1", "R1"};
    EXPECT_FALSE(core::ValidateChoiceGroup(duplicate, err));

    core::PatternChoiceGroup no_source = valid;
    no_source.sourceElement.clear();
    EXPECT_FALSE(core::ValidateChoiceGroup(no_source, err));
}

// ===== Pattern definition metadata =====

TEST(PatternDefinition, RoundTripThroughPackage) {
    sacm::ArgumentPackage pkg;
    core::PatternDefinition def;
    def.identifier = "HAZ-DECOMP";
    def.name = "Hazard Decomposition";
    def.summary = "Decompose over hazards";
    def.intent = "Argue safety by decomposition";
    def.applicability = "When a hazard log exists";
    core::WritePatternDefinition(pkg, def);

    EXPECT_EQ(pkg.name, "Hazard Decomposition");
    EXPECT_EQ(pkg.description, "Decompose over hazards");

    const core::PatternDefinition read = core::ReadPatternDefinition(pkg);
    EXPECT_EQ(read.identifier, "HAZ-DECOMP");
    EXPECT_EQ(read.intent, "Argue safety by decomposition");
    EXPECT_EQ(read.applicability, "When a hazard log exists");
    EXPECT_TRUE(read.motivation.empty());

    // Empty sections must not leave stale tagged values behind.
    EXPECT_FALSE(core::HasTaggedValue(pkg, keys::kMotivation));
}
