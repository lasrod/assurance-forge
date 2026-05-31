#include "core/terminology_context_projection.h"

#include "core/terminology_package_service.h" // kVisibleTerminologyContextMarker, TerminologyContextAssociationResult
#include "core/terminology_text_utils.h"      // TermContextDisplayLabel
#include "parser/model_utils.h"
#include "sacm/sacm_model.h"

#include <gtest/gtest.h>

#include <string>

// Intent of terminology_context_projection: keep the parser-side model's
// projection of a *visible* terminology context in sync with the SACM package.
//  - RefreshVisibleTerminologyContextProjection pulls the referenced term's
//    name/description onto the matching parser element and reports whether
//    anything changed (so it is a no-op on a steady state).
//  - SyncVisibleTerminologyContextToParser materializes parser elements for a
//    newly-associated visible context, and is a no-op when the association does
//    not resolve to a genuine visible-term context.
// Assertions encode this intent; failures are findings, not tuning targets.

namespace {

// Build a package whose argument package contains a visible-terminology context
// (marker description) sourced from artifact reference AR1, which references
// term T1 defined in a case-level terminology package.
sacm::AssuranceCasePackage MakeVisibleTermContextPackage() {
    sacm::AssuranceCasePackage package;

    sacm::TerminologyPackage terminology_package;
    terminology_package.id = "TP1";
    terminology_package.gid = "gid-TP1";
    sacm::Term term;
    term.id = "T1";
    term.gid = "gid-T1";
    term.value = "ODD";
    term.name = "Operational Design Domain";
    term.description = "The operating conditions for the system.";
    terminology_package.terms.push_back(term);
    package.terminologyPackages.push_back(terminology_package);

    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    sacm::ArtifactReference artifact_reference;
    artifact_reference.id = "AR1";
    artifact_reference.gid = "gid-AR1";
    artifact_reference.referencedArtifact = "T1";
    argument_package.artifactReferences.push_back(artifact_reference);
    sacm::AssertedContext context;
    context.id = "AC1";
    context.gid = "gid-AC1";
    context.sources = {"AR1"};
    context.targets = {"G1"};
    context.description = core::kVisibleTerminologyContextMarker; // marks it visible
    argument_package.assertedContexts.push_back(context);
    package.argumentPackages.push_back(argument_package);

    return package;
}

const sacm::Term& TheTerm(const sacm::AssuranceCasePackage& package) {
    return package.terminologyPackages.front().terms.front();
}

} // namespace

TEST(TerminologyContextProjectionTest, RefreshProjectsTermNameAndDescriptionOntoElement) {
    sacm::AssuranceCasePackage package = MakeVisibleTermContextPackage();

    parser::AssuranceCase model;
    parser::SacmElement element;
    element.id = "AR1";
    element.gid = "gid-AR1";
    element.type = "artifactreference";
    element.name = "stale name";
    element.description = "stale description";
    model.elements.push_back(element);

    const bool changed = core::RefreshVisibleTerminologyContextProjection(model, package);

    EXPECT_TRUE(changed);
    const parser::SacmElement* projected = parser::FindElementById(model, "AR1");
    ASSERT_NE(projected, nullptr);
    EXPECT_EQ(projected->name, core::TermContextDisplayLabel(TheTerm(package)));
    EXPECT_EQ(projected->description, TheTerm(package).description);
}

TEST(TerminologyContextProjectionTest, RefreshIsNoOpOnSteadyState) {
    sacm::AssuranceCasePackage package = MakeVisibleTermContextPackage();

    parser::AssuranceCase model;
    parser::SacmElement element;
    element.id = "AR1";
    element.gid = "gid-AR1";
    element.type = "artifactreference";
    model.elements.push_back(element);

    ASSERT_TRUE(core::RefreshVisibleTerminologyContextProjection(model, package));
    // A second refresh with no intervening change must report no change.
    EXPECT_FALSE(core::RefreshVisibleTerminologyContextProjection(model, package));
}

TEST(TerminologyContextProjectionTest, SyncMaterializesParserElementsForNewVisibleContext) {
    sacm::AssuranceCasePackage package = MakeVisibleTermContextPackage();
    parser::AssuranceCase model; // empty — nothing projected yet

    core::TerminologyContextAssociationResult result;
    result.success = true;
    result.created_artifact_reference = true;
    result.created_asserted_context = true;
    result.artifact_reference_id = "AR1";
    result.asserted_context_id = "AC1";

    const bool changed = core::SyncVisibleTerminologyContextToParser(model, package, result);

    EXPECT_TRUE(changed);
    const parser::SacmElement* artifact_reference = parser::FindElementById(model, "AR1");
    const parser::SacmElement* context = parser::FindElementById(model, "AC1");
    ASSERT_NE(artifact_reference, nullptr);
    EXPECT_EQ(artifact_reference->type, "artifactreference");
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->type, "assertedcontext");
    // The newly materialized artifact-reference element should already carry the
    // projected term description.
    EXPECT_EQ(artifact_reference->description, TheTerm(package).description);
}

TEST(TerminologyContextProjectionTest, SyncIsNoOpWhenContextIsNotVisibleTerminology) {
    sacm::AssuranceCasePackage package = MakeVisibleTermContextPackage();
    // Make the context an ordinary (non-visible) context.
    package.argumentPackages.front().assertedContexts.front().description = "ordinary context";

    parser::AssuranceCase model;
    core::TerminologyContextAssociationResult result;
    result.artifact_reference_id = "AR1";
    result.asserted_context_id = "AC1";

    EXPECT_FALSE(core::SyncVisibleTerminologyContextToParser(model, package, result));
    EXPECT_TRUE(model.elements.empty());
}

TEST(TerminologyContextProjectionTest, SyncIsNoOpWhenReferencesDoNotResolve) {
    sacm::AssuranceCasePackage package = MakeVisibleTermContextPackage();
    parser::AssuranceCase model;

    core::TerminologyContextAssociationResult result;
    result.artifact_reference_id = "MISSING_AR";
    result.asserted_context_id = "MISSING_AC";

    EXPECT_FALSE(core::SyncVisibleTerminologyContextToParser(model, package, result));
    EXPECT_TRUE(model.elements.empty());
}
