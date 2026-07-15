#include "sacm/compare/semantic_compare.h"
#include "sacm/io/xmi.h"
#include "sacm/model/document.h"
#include "sacm/validation/codes.h"
#include "sacm/validation/validate.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

namespace {

using sacm::commands::AddMetaClaim;
using sacm::commands::ChangeRecord;
using sacm::commands::CreateArgumentPackage;
using sacm::commands::CreateArgumentReasoning;
using sacm::commands::CreateArtifactReference;
using sacm::commands::CreateAssertedRelationship;
using sacm::commands::CreateAssuranceCasePackage;
using sacm::commands::CreateClaim;
using sacm::commands::DeleteElement;
using sacm::commands::OperationPreview;
using sacm::commands::ReferenceDeletePolicy;
using sacm::commands::SetAssertionDeclaration;
using sacm::io::LoadOptions;
using sacm::io::LoadResult;
using sacm::io::Mode;
using sacm::metadata::ElementKind;
using sacm::model::AssertionDeclaration;
using sacm::model::Document;
using sacm::model::ElementId;

std::filesystem::path fixture(std::string_view name) {
    return std::filesystem::path(SACM_TEST_DATA_DIR) / "sacm23" / name;
}

bool has_code(const std::vector<sacm::validation::Diagnostic>& diagnostics,
              std::string_view code) {
    return std::ranges::any_of(diagnostics, [&](const auto& diagnostic) {
        return diagnostic.code == code;
    });
}

TEST(Sacm23Argumentation, SACM23_ARG_001_FullArgumentationFixtureRoundTrips) {
    const LoadResult first = sacm::io::load_xmi_file(fixture("argumentation-full-valid.sacm.xmi"),
                                                     LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(first.ok) << (first.diagnostics.empty() ? "" : first.diagnostics.front().message);
    const auto& document = *first.document;

    // Assertion declarations.
    const auto* claim_top = document.find_as<sacm::model::Claim>(ElementId{"claim_top"});
    ASSERT_NE(claim_top, nullptr);
    EXPECT_EQ(claim_top->assertion_declaration(), AssertionDeclaration::NeedsSupport);
    const auto* claim_counter =
        document.find_as<sacm::model::Claim>(ElementId{"claim_counter"});
    ASSERT_NE(claim_counter, nullptr);
    EXPECT_EQ(claim_counter->assertion_declaration(), AssertionDeclaration::Defeated);

    // Inference with reasoning, multiple sources, and a meta-claim.
    const auto* inference = document.find_as<sacm::model::AssertedInference>(ElementId{"inf_1"});
    ASSERT_NE(inference, nullptr);
    EXPECT_EQ(inference->sources().size(), 2u);
    ASSERT_EQ(inference->meta_claims().size(), 1u);
    EXPECT_EQ(inference->meta_claims().front().value(), "claim_meta");
    ASSERT_TRUE(inference->reasoning().has_value());

    // Counter-argumentation.
    const auto* counter = document.find_as<sacm::model::AssertedInference>(ElementId{"counter_1"});
    ASSERT_NE(counter, nullptr);
    EXPECT_TRUE(counter->is_counter());

    // Artifact support/context families and the reasoning structure ref.
    EXPECT_NE(document.find_as<sacm::model::AssertedArtifactSupport>(ElementId{"support_1"}),
              nullptr);
    EXPECT_NE(document.find_as<sacm::model::AssertedArtifactContext>(ElementId{"actx_1"}), nullptr);
    const auto* reasoning =
        document.find_as<sacm::model::ArgumentReasoning>(ElementId{"ar_decompose"});
    ASSERT_NE(reasoning, nullptr);
    ASSERT_TRUE(reasoning->structure().has_value());
    EXPECT_EQ(reasoning->structure()->value(), "argpkg_detail");

    // Group membership and evidence reference into the artifact package.
    const auto* group = document.find_as<sacm::model::ArgumentGroup>(ElementId{"group_core"});
    ASSERT_NE(group, nullptr);
    EXPECT_EQ(group->argument_elements().size(), 3u);
    const auto* evidence = document.find_as<sacm::model::ArtifactReference>(ElementId{"ev_fmea"});
    ASSERT_NE(evidence, nullptr);
    ASSERT_EQ(evidence->referenced_artifact_elements().size(), 1u);

    EXPECT_TRUE(sacm::validation::validate(document).empty());
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult second = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(second.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *second.document).empty());
}

TEST(Sacm23Argumentation, SACM23_ARG_001_InvalidAssertionDeclarationIsDiagnosed) {
    const LoadResult result =
        sacm::io::load_xmi_file(fixture("invalid/assertion-declaration-invalid.sacm.xmi"));
    EXPECT_TRUE(has_code(result.diagnostics, sacm::validation::codes::kEnumInvalidLiteral));
}

TEST(Sacm23Argumentation, SACM23_ARG_002_RelationshipTargetTypingIsValidated) {
    const LoadResult result = sacm::io::load_xmi_file(
        fixture("invalid/relationship-target-wrong-type-invalid.sacm.xmi"),
        LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(result.document.has_value());
    const auto diagnostics = sacm::validation::validate(*result.document);
    EXPECT_TRUE(has_code(diagnostics, sacm::validation::codes::kRefWrongType));
}

Document build_argument_case() {
    Document document;
    EXPECT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "Case"})
                    .applied);
    EXPECT_TRUE(document
                    .apply(CreateArgumentPackage{
                        .parent = ElementId{"acp_1"}, .id = ElementId{"argpkg_1"}, .name = "Arg"})
                    .applied);
    EXPECT_TRUE(document
                    .apply(CreateClaim{.parent = ElementId{"argpkg_1"},
                                       .id = ElementId{"claim_top"},
                                       .name = "Top"})
                    .applied);
    EXPECT_TRUE(document
                    .apply(CreateClaim{.parent = ElementId{"argpkg_1"},
                                       .id = ElementId{"claim_sub"},
                                       .name = "Sub"})
                    .applied);
    return document;
}

TEST(Sacm23Argumentation, SACM23_ARG_001_CreatesRelationshipsWithCommands) {
    Document document = build_argument_case();
    ASSERT_TRUE(document
                    .apply(CreateArgumentReasoning{.parent = ElementId{"argpkg_1"},
                                                   .id = ElementId{"ar_1"},
                                                   .name = "Decomposition"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateAssertedRelationship{
                        .parent = ElementId{"argpkg_1"},
                        .kind = ElementKind::AssertedInference,
                        .id = ElementId{"inf_1"},
                        .name = "Inference",
                        .sources = {ElementId{"claim_sub"}},
                        .targets = {ElementId{"claim_top"}},
                        .reasoning = ElementId{"ar_1"},
                    })
                    .applied);
    ASSERT_TRUE(document
                    .apply(SetAssertionDeclaration{
                        .element = ElementId{"claim_top"},
                        .declaration = AssertionDeclaration::NeedsSupport})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateClaim{.parent = ElementId{"argpkg_1"},
                                       .id = ElementId{"claim_meta"},
                                       .name = "Meta"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(AddMetaClaim{.element = ElementId{"inf_1"},
                                        .meta_claim = ElementId{"claim_meta"}})
                    .applied);

    EXPECT_TRUE(sacm::validation::validate(document).empty());
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded =
        sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *reloaded.document).empty());
}

TEST(Sacm23Argumentation, SACM23_ARG_002_RejectsRelationshipWithWrongTargetKind) {
    Document document = build_argument_case();
    const auto result = document.apply(CreateAssertedRelationship{
        .parent = ElementId{"argpkg_1"},
        .kind = ElementKind::AssertedInference,
        .sources = {ElementId{"claim_sub"}},
        .targets = {ElementId{"argpkg_1"}},  // a package is not an ArgumentAsset
    });
    EXPECT_FALSE(result.applied);
    EXPECT_TRUE(has_code(result.diagnostics, sacm::validation::codes::kRefWrongType));

    const auto empty_sources = document.apply(CreateAssertedRelationship{
        .parent = ElementId{"argpkg_1"},
        .kind = ElementKind::AssertedEvidence,
        .targets = {ElementId{"claim_top"}},
    });
    EXPECT_FALSE(empty_sources.applied);
    EXPECT_TRUE(has_code(empty_sources.diagnostics,
                         sacm::validation::codes::kMultiplicityViolation));
}

TEST(Sacm23Argumentation, SACM23_ARG_003_ClaimDeletePreviewListsAffectedRelationships) {
    Document document = build_argument_case();
    ASSERT_TRUE(document
                    .apply(CreateAssertedRelationship{
                        .parent = ElementId{"argpkg_1"},
                        .kind = ElementKind::AssertedInference,
                        .id = ElementId{"inf_1"},
                        .sources = {ElementId{"claim_sub"}},
                        .targets = {ElementId{"claim_top"}},
                    })
                    .applied);

    // Preview names the affected relationship before any mutation.
    const OperationPreview preview = document.preview(DeleteElement{
        .target = ElementId{"claim_sub"},
        .reference_policy = ReferenceDeletePolicy::DeleteReferencingRelationships,
    });
    ASSERT_TRUE(preview.can_apply);
    EXPECT_TRUE(std::ranges::any_of(preview.effects, [](const ChangeRecord& record) {
        return record.id.value() == "inf_1" &&
               record.change == ChangeRecord::Change::RelationshipDeleted;
    }));

    // Applying leaves no dangling references.
    const auto result = document.apply(
        DeleteElement{.target = ElementId{"claim_sub"},
                      .reference_policy = ReferenceDeletePolicy::DeleteReferencingRelationships},
        preview.document_revision);
    ASSERT_TRUE(result.applied);
    EXPECT_EQ(document.find(ElementId{"inf_1"}), nullptr);
    EXPECT_TRUE(sacm::validation::validate(document).empty());
}

}  // namespace
