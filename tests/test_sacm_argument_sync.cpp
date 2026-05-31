#include "core/sacm_argument_sync.h"

#include "sacm/sacm_model.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

// Intent of RebuildSacmArgumentPackageFromParser (from the header contract and
// the round-trip-integrity rule in CLAUDE.md): the first SACM argument package
// must be made to mirror the parser model's elements, mapping each element kind
// to the matching SACM type, while user-authored terminology artifact
// references and their contexts that still target a live element survive the
// rebuild. The rebuild must be idempotent (no duplication, stale content
// dropped). Assertions encode that intent; failures are findings.

namespace {

parser::SacmElement Element(std::string id, std::string type) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = std::move(type);
    return element;
}

template <typename Range>
bool ContainsId(const Range& range, const std::string& id) {
    return std::any_of(range.begin(), range.end(), [&](const auto& item) { return item.id == id; });
}

} // namespace

TEST(SacmArgumentSyncTest, MirrorsClaimsAndReasoningsIntoFirstArgumentPackage) {
    parser::AssuranceCase model;
    parser::SacmElement goal = Element("G1", "claim");
    goal.content = "Top goal content.";
    model.elements.push_back(goal);
    model.elements.push_back(Element("S1", "argumentreasoning"));

    sacm::AssuranceCasePackage package;
    core::RebuildSacmArgumentPackageFromParser(model, package);

    ASSERT_EQ(package.argumentPackages.size(), 1u);
    const sacm::ArgumentPackage& ap = package.argumentPackages.front();
    ASSERT_EQ(ap.claims.size(), 1u);
    EXPECT_EQ(ap.claims.front().id, "G1");
    EXPECT_EQ(ap.claims.front().content, "Top goal content.");
    ASSERT_EQ(ap.argumentReasonings.size(), 1u);
    EXPECT_EQ(ap.argumentReasonings.front().id, "S1");
}

TEST(SacmArgumentSyncTest, MapsRelationshipElementsToAssertedTypes) {
    parser::AssuranceCase model;
    parser::SacmElement inference = Element("I1", "assertedinference");
    inference.source_refs = {"S1"};
    inference.target_refs = {"G1"};
    inference.reasoning_ref = "S1";
    model.elements.push_back(inference);
    parser::SacmElement context = Element("C1", "assertedcontext");
    context.source_refs = {"X1"};
    context.target_refs = {"G1"};
    model.elements.push_back(context);
    parser::SacmElement evidence = Element("E1", "assertedevidence");
    evidence.source_refs = {"Sn1"};
    evidence.target_refs = {"G1"};
    model.elements.push_back(evidence);

    sacm::AssuranceCasePackage package;
    core::RebuildSacmArgumentPackageFromParser(model, package);

    ASSERT_EQ(package.argumentPackages.size(), 1u);
    const sacm::ArgumentPackage& ap = package.argumentPackages.front();
    ASSERT_EQ(ap.assertedInferences.size(), 1u);
    EXPECT_EQ(ap.assertedInferences.front().id, "I1");
    EXPECT_EQ(ap.assertedInferences.front().targets, std::vector<std::string>{"G1"});
    EXPECT_EQ(ap.assertedInferences.front().reasoning, "S1");
    ASSERT_EQ(ap.assertedContexts.size(), 1u);
    EXPECT_EQ(ap.assertedContexts.front().id, "C1");
    ASSERT_EQ(ap.assertedEvidences.size(), 1u);
    EXPECT_EQ(ap.assertedEvidences.front().id, "E1");
}

TEST(SacmArgumentSyncTest, CreatesArgumentPackageWhenNoneExists) {
    parser::AssuranceCase model;
    model.elements.push_back(Element("G1", "claim"));

    sacm::AssuranceCasePackage package; // no argument packages
    core::RebuildSacmArgumentPackageFromParser(model, package);

    ASSERT_EQ(package.argumentPackages.size(), 1u);
    EXPECT_TRUE(ContainsId(package.argumentPackages.front().claims, "G1"));
}

TEST(SacmArgumentSyncTest, RebuildIsIdempotentAndDropsStaleContent) {
    parser::AssuranceCase model;
    model.elements.push_back(Element("G1", "claim"));

    sacm::AssuranceCasePackage package;
    sacm::ArgumentPackage argument_package;
    // Stale claim that no longer exists in the model must be removed on rebuild.
    sacm::Claim stale;
    stale.id = "OLD";
    argument_package.claims.push_back(stale);
    package.argumentPackages.push_back(argument_package);

    core::RebuildSacmArgumentPackageFromParser(model, package);
    const sacm::ArgumentPackage& after_first = package.argumentPackages.front();
    EXPECT_FALSE(ContainsId(after_first.claims, "OLD"));
    ASSERT_EQ(after_first.claims.size(), 1u);
    EXPECT_EQ(after_first.claims.front().id, "G1");

    // Rebuilding again with the same model must not duplicate elements.
    core::RebuildSacmArgumentPackageFromParser(model, package);
    EXPECT_EQ(package.argumentPackages.front().claims.size(), 1u);
}

TEST(SacmArgumentSyncTest, PreservesReferencedArtifactTargetForRebuiltArtifactReference) {
    parser::AssuranceCase model;
    model.elements.push_back(Element("AR1", "artifactreference"));

    sacm::AssuranceCasePackage package;
    sacm::ArgumentPackage argument_package;
    sacm::ArtifactReference existing;
    existing.id = "AR1";
    existing.referencedArtifact = "T1";
    argument_package.artifactReferences.push_back(existing);
    package.argumentPackages.push_back(argument_package);

    core::RebuildSacmArgumentPackageFromParser(model, package);

    const sacm::ArgumentPackage& ap = package.argumentPackages.front();
    auto found = std::find_if(ap.artifactReferences.begin(), ap.artifactReferences.end(),
                              [](const sacm::ArtifactReference& ref) { return ref.id == "AR1"; });
    ASSERT_NE(found, ap.artifactReferences.end());
    // The referenced-term target recorded before the rebuild must be restored.
    EXPECT_EQ(found->referencedArtifact, "T1");
}

TEST(SacmArgumentSyncTest, PreservesUserAuthoredTermContextWhenTargetElementSurvives) {
    parser::AssuranceCase model;
    model.elements.push_back(Element("G1", "claim"));

    sacm::AssuranceCasePackage package;
    // A terminology package defines term T1 so the artifact reference targets a known term.
    sacm::TerminologyPackage terminology_package;
    terminology_package.id = "TP1";
    terminology_package.gid = "gid-TP1";
    sacm::Term term;
    term.id = "T1";
    term.gid = "gid-T1";
    term.value = "hazard";
    terminology_package.terms.push_back(term);
    package.terminologyPackages.push_back(terminology_package);

    sacm::ArgumentPackage argument_package;
    sacm::ArtifactReference term_reference; // user-authored, NOT a visible-term projection
    term_reference.id = "AR1";
    term_reference.referencedArtifact = "T1";
    argument_package.artifactReferences.push_back(term_reference);
    sacm::AssertedContext context;
    context.id = "AC1";
    context.sources = {"AR1"};
    context.targets = {"G1"}; // targets a live model element
    argument_package.assertedContexts.push_back(context);
    package.argumentPackages.push_back(argument_package);

    core::RebuildSacmArgumentPackageFromParser(model, package);

    const sacm::ArgumentPackage& ap = package.argumentPackages.front();
    EXPECT_TRUE(ContainsId(ap.artifactReferences, "AR1"))
        << "User-authored term artifact reference should survive the rebuild.";
    const bool context_preserved =
        std::any_of(ap.assertedContexts.begin(), ap.assertedContexts.end(),
                    [](const sacm::AssertedContext& c) {
                        return c.sources == std::vector<std::string>{"AR1"} &&
                               c.targets == std::vector<std::string>{"G1"};
                    });
    EXPECT_TRUE(context_preserved) << "Asserted context for a surviving target should be preserved.";
}

TEST(SacmArgumentSyncTest, DropsTermContextWhenTargetElementRemoved) {
    // Same preserved term reference, but the target element G1 is NOT in the
    // model. The context should not be re-added because its target is gone.
    parser::AssuranceCase model; // empty: G1 removed

    sacm::AssuranceCasePackage package;
    sacm::TerminologyPackage terminology_package;
    terminology_package.id = "TP1";
    sacm::Term term;
    term.id = "T1";
    term.gid = "gid-T1";
    term.value = "hazard";
    terminology_package.terms.push_back(term);
    package.terminologyPackages.push_back(terminology_package);

    sacm::ArgumentPackage argument_package;
    sacm::ArtifactReference term_reference;
    term_reference.id = "AR1";
    term_reference.referencedArtifact = "T1";
    argument_package.artifactReferences.push_back(term_reference);
    sacm::AssertedContext context;
    context.id = "AC1";
    context.sources = {"AR1"};
    context.targets = {"G1"};
    argument_package.assertedContexts.push_back(context);
    package.argumentPackages.push_back(argument_package);

    core::RebuildSacmArgumentPackageFromParser(model, package);

    const sacm::ArgumentPackage& ap = package.argumentPackages.front();
    const bool context_present =
        std::any_of(ap.assertedContexts.begin(), ap.assertedContexts.end(),
                    [](const sacm::AssertedContext& c) { return c.targets == std::vector<std::string>{"G1"}; });
    EXPECT_FALSE(context_present) << "Context targeting a removed element should be dropped.";
}
