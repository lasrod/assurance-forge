#include "core/argument_package_projection.h"
#include "core/assurance_tree.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"
#include "sacm_adapter/gsn_role_tag.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace {

sacm::ArgumentPackage MakePackage(const std::string& id, std::vector<std::string> claim_ids) {
    sacm::ArgumentPackage pkg;
    pkg.id = id;
    pkg.name = id;
    for (const auto& cid : claim_ids) {
        sacm::Claim c;
        c.id = cid;
        c.gid = cid + ".gid";
        pkg.claims.push_back(std::move(c));
    }
    return pkg;
}

parser::SacmElement MakeElement(const std::string& id, const std::string& type) {
    parser::SacmElement e;
    e.id = id;
    e.gid = id + ".gid";
    e.type = type;
    return e;
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& model, const std::string& id) {
    const auto it = std::find_if(model.elements.begin(), model.elements.end(), [&](const parser::SacmElement& element) {
        return element.id == id;
    });
    return it == model.elements.end() ? nullptr : &*it;
}

// A goal G1 plus a bare strategy S1 (an ArgumentReasoning carrying the
// `strategyTarget` tag and NO inference, which is how a just-added strategy is
// stored until its first sub-goal materializes one).
sacm::ArgumentPackage MakePackageWithBareStrategy() {
    sacm::ArgumentPackage pkg = MakePackage("AP-A", {"G1"});
    sacm::ArgumentReasoning reasoning;
    reasoning.id = "S1";
    reasoning.gid = "S1.gid";
    reasoning.taggedValues.push_back(
        sacm::TaggedValue{.id = "S1__strategyTarget", .key = sacm_adapter::kGsnStrategyTargetTagKey, .value = "G1"});
    pkg.argumentReasonings.push_back(std::move(reasoning));
    return pkg;
}

parser::AssuranceCase MakeSourceWithBareStrategy() {
    parser::AssuranceCase source;
    source.id = "case-1";
    source.name = "Case 1";
    source.elements.push_back(MakeElement("G1", "claim"));
    source.elements.push_back(MakeElement("S1", "argumentreasoning"));
    // The render-only placement the full model carries (SynthesizeBareStrategy-
    // Placements) — never present in the SACM package, so the projection's
    // ownership filter always drops it.
    parser::SacmElement placeholder;
    placeholder.id = "S1__pending_inference";
    placeholder.type = "assertedinference";
    placeholder.reasoning_ref = "S1";
    placeholder.target_refs.push_back("G1");
    source.elements.push_back(std::move(placeholder));
    return source;
}

} // namespace

TEST(ArgumentPackageProjection, FindByIdMatchesIdField) {
    sacm::AssuranceCasePackage acp;
    acp.argumentPackages.push_back(MakePackage("AP1", {"G1"}));
    acp.argumentPackages.push_back(MakePackage("AP2", {"G2"}));

    const sacm::ArgumentPackage* found = core::FindArgumentPackageByIdentity(acp, "AP2", "");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, "AP2");
}

TEST(ArgumentPackageProjection, FindByGidWhenIdEmpty) {
    sacm::AssuranceCasePackage acp;
    sacm::ArgumentPackage pkg;
    pkg.id = "AP1";
    pkg.gid = "AP1.gid";
    acp.argumentPackages.push_back(std::move(pkg));

    EXPECT_EQ(core::FindArgumentPackageByIdentity(acp, "", "AP1.gid"), &acp.argumentPackages.front());
    EXPECT_EQ(core::FindArgumentPackageByIdentity(acp, "", ""), nullptr);
    EXPECT_EQ(core::FindArgumentPackageByIdentity(acp, "AP9", "AP9.gid"), nullptr);
}

TEST(ArgumentPackageProjection, BuildProjectionKeepsOnlyOwnedElements) {
    parser::AssuranceCase source;
    source.id = "case-1";
    source.name = "Case 1";
    source.elements.push_back(MakeElement("G1", "claim"));
    source.elements.push_back(MakeElement("G2", "claim"));
    source.elements.push_back(MakeElement("S1", "argumentreasoning"));

    auto pkg = MakePackage("AP-A", {"G1"});
    sacm::ArgumentReasoning reasoning;
    reasoning.id = "S1";
    reasoning.gid = "S1.gid";
    pkg.argumentReasonings.push_back(reasoning);

    const parser::AssuranceCase projection = core::BuildArgumentPackageProjection(source, pkg, "fallback");
    EXPECT_EQ(projection.id, "AP-A");
    ASSERT_EQ(projection.elements.size(), 2u);
    EXPECT_EQ(projection.elements[0].id, "G1");
    EXPECT_EQ(projection.elements[1].id, "S1");
}

// The per-package canvas projection must keep a bare strategy attached to the
// goal it was added to. Its placement is a render-only placeholder inference
// that is deliberately absent from the SACM package, so the ownership filter
// drops it and the strategy renders as a detached cluster outside the argument.
TEST(ArgumentPackageProjection, BuildProjectionKeepsBareStrategyPlacedUnderItsGoal) {
    const parser::AssuranceCase source = MakeSourceWithBareStrategy();
    const sacm::ArgumentPackage pkg = MakePackageWithBareStrategy();

    const parser::AssuranceCase projection = core::BuildArgumentPackageProjection(source, pkg, "fallback");

    const parser::SacmElement* placement = FindElement(projection, "S1__pending_inference");
    ASSERT_NE(placement, nullptr) << "bare strategy lost its placement in the package projection";
    EXPECT_EQ(placement->type, "assertedinference");
    EXPECT_EQ(placement->reasoning_ref, "S1");
    ASSERT_EQ(placement->target_refs.size(), 1u);
    EXPECT_EQ(placement->target_refs.front(), "G1");

    const core::AssuranceTree tree = core::AssuranceTree::Build(projection, "ja");
    ASSERT_NE(tree.root, nullptr);
    EXPECT_EQ(tree.root->id, "G1");
    EXPECT_TRUE(tree.orphans.empty()) << "the strategy is rendering outside the argument";
    const core::TreeNode* strategy = core::FindTreeNode(tree, "S1");
    ASSERT_NE(strategy, nullptr);
    ASSERT_NE(strategy->parent, nullptr);
    EXPECT_EQ(strategy->parent->id, "G1");
}

// The placement pass is idempotent: a strategy that already has an inference
// (real or synthesized) gets no second placeholder, or it would render twice.
TEST(ArgumentPackageProjection, BuildProjectionDoesNotDuplicateExistingPlacement) {
    const parser::AssuranceCase source = MakeSourceWithBareStrategy();
    const sacm::ArgumentPackage pkg = MakePackageWithBareStrategy();

    const parser::AssuranceCase once = core::BuildArgumentPackageProjection(source, pkg, "fallback");
    const parser::AssuranceCase twice = core::BuildArgumentPackageProjection(once, pkg, "fallback");

    const auto count_placements = [](const parser::AssuranceCase& model) {
        return std::count_if(model.elements.begin(), model.elements.end(), [](const parser::SacmElement& element) {
            return element.id == "S1__pending_inference";
        });
    };
    EXPECT_EQ(count_placements(once), 1);
    EXPECT_EQ(count_placements(twice), 1);
}

// A materialized strategy (its inference exists in the package) needs no
// placeholder — synthesizing one would draw the strategy a second time.
TEST(ArgumentPackageProjection, BuildProjectionSynthesizesNothingForMaterializedStrategy) {
    parser::AssuranceCase source;
    source.elements.push_back(MakeElement("G1", "claim"));
    source.elements.push_back(MakeElement("G2", "claim"));
    source.elements.push_back(MakeElement("S1", "argumentreasoning"));
    parser::SacmElement inference = MakeElement("R1", "assertedinference");
    inference.reasoning_ref = "S1";
    inference.target_refs.push_back("G1");
    inference.source_refs.push_back("G2");
    source.elements.push_back(std::move(inference));

    sacm::ArgumentPackage pkg = MakePackageWithBareStrategy();
    sacm::Claim sub_goal;
    sub_goal.id = "G2";
    sub_goal.gid = "G2.gid";
    pkg.claims.push_back(std::move(sub_goal));
    sacm::AssertedInference package_inference;
    package_inference.id = "R1";
    package_inference.gid = "R1.gid";
    package_inference.reasoning = "S1";
    package_inference.targets.push_back("G1");
    package_inference.sources.push_back("G2");
    pkg.assertedInferences.push_back(std::move(package_inference));

    const parser::AssuranceCase projection = core::BuildArgumentPackageProjection(source, pkg, "fallback");
    EXPECT_EQ(FindElement(projection, "S1__pending_inference"), nullptr);
    EXPECT_EQ(projection.elements.size(), 4u);
}

// The canvas projects an argument package through the ids that package holds,
// and an element an agent has staged is in no package at all -- nothing has been
// applied, which is the point of a preview. Reported from live use: eighty
// staged elements appeared in the Argument Navigator and none on the canvas.
// Feeding the canvas the preview model is only half the fix; without this the
// ownership filter drops every addition and it draws the committed argument.
TEST(ArgumentPackageProjection, PreviewProjectionKeepsStagedAdditionsAttachedToThePackage) {
    parser::AssuranceCase preview;
    preview.elements.push_back(MakeElement("G1", "claim"));
    // What a staged CreateStrategy + AddSupportedBy + CreateClaim leaves behind:
    // new elements with no gid, wired by new relationships.
    preview.elements.push_back(MakeElement("S1", "argumentreasoning"));
    parser::SacmElement to_strategy = MakeElement("R1", "assertedinference");
    to_strategy.reasoning_ref = "S1";
    to_strategy.target_refs.push_back("G1");
    preview.elements.push_back(std::move(to_strategy));
    preview.elements.push_back(MakeElement("G2", "claim"));
    parser::SacmElement to_sub_goal = MakeElement("R2", "assertedinference");
    to_sub_goal.source_refs.push_back("G2");
    to_sub_goal.target_refs.push_back("G1");
    preview.elements.push_back(std::move(to_sub_goal));

    sacm::AssuranceCasePackage package;
    package.argumentPackages.push_back(MakePackage("AP-A", {"G1"}));

    const std::vector<std::string> added{"S1", "R1", "G2", "R2"};
    const parser::AssuranceCase projection = core::BuildArgumentPackagePreviewProjection(
        preview, package, package.argumentPackages.front(), added, "fallback");

    for (const std::string& id : added) {
        EXPECT_NE(FindElement(projection, id), nullptr)
            << id << " was staged onto this package and is missing from its canvas";
    }
    EXPECT_NE(FindElement(projection, "G1"), nullptr);
}

// One package's additions must not leak onto another's canvas. A project with a
// confidence argument beside its main one has two, and a change set staged into
// one of them belongs on one canvas.
TEST(ArgumentPackageProjection, PreviewProjectionLeavesAnotherPackagesAdditionsAlone) {
    parser::AssuranceCase preview;
    preview.elements.push_back(MakeElement("G1", "claim"));
    preview.elements.push_back(MakeElement("C1", "claim"));
    preview.elements.push_back(MakeElement("G9", "claim"));
    parser::SacmElement wiring = MakeElement("R9", "assertedinference");
    wiring.source_refs.push_back("G9");
    wiring.target_refs.push_back("C1"); // staged under the SECOND package's claim
    preview.elements.push_back(std::move(wiring));

    sacm::AssuranceCasePackage package;
    package.argumentPackages.push_back(MakePackage("AP-A", {"G1"}));
    package.argumentPackages.push_back(MakePackage("AP-B", {"C1"}));

    const std::vector<std::string> added{"G9", "R9"};
    const parser::AssuranceCase first = core::BuildArgumentPackagePreviewProjection(
        preview, package, package.argumentPackages.front(), added, "fallback");
    const parser::AssuranceCase second = core::BuildArgumentPackagePreviewProjection(
        preview, package, package.argumentPackages.back(), added, "fallback");

    EXPECT_EQ(FindElement(first, "G9"), nullptr) << "an addition staged elsewhere leaked in";
    EXPECT_NE(FindElement(second, "G9"), nullptr);
    EXPECT_NE(FindElement(second, "R9"), nullptr);
}

// An agent's first goal in an empty argument attaches to nothing, so there is no
// connection to follow. It goes where applying the change set would put it,
// rather than being drawn nowhere at all.
TEST(ArgumentPackageProjection, PreviewProjectionShowsAnAdditionThatAttachesToNothing) {
    parser::AssuranceCase preview;
    preview.elements.push_back(MakeElement("G1", "claim"));

    sacm::AssuranceCasePackage package;
    package.argumentPackages.push_back(MakePackage("AP-A", {}));

    const parser::AssuranceCase projection = core::BuildArgumentPackagePreviewProjection(
        preview, package, package.argumentPackages.front(), {"G1"}, "fallback");

    EXPECT_NE(FindElement(projection, "G1"), nullptr);
}

TEST(ArgumentPackageProjection, BuildProjectionUsesFallbackNameWhenPackageEmpty) {
    parser::AssuranceCase source;
    sacm::ArgumentPackage pkg;
    pkg.id = "AP-X";
    // pkg.name intentionally empty
    const parser::AssuranceCase projection = core::BuildArgumentPackageProjection(source, pkg, "Fallback Title");
    EXPECT_EQ(projection.name, "Fallback Title");
}
