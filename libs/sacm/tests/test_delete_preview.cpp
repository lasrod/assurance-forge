#include "sacm/model/document.h"

#include "sacm/validation/codes.h"
#include "sacm/validation/validate.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace {

using sacm::commands::ChangeRecord;
using sacm::commands::CreateArgumentPackage;
using sacm::commands::CreateAssuranceCasePackage;
using sacm::commands::CreateClaim;
using sacm::commands::CrossPackageReferencePolicy;
using sacm::commands::DeleteElement;
using sacm::commands::MutationResult;
using sacm::commands::OperationPreview;
using sacm::commands::PackageDeletePolicy;
using sacm::commands::ReferenceDeletePolicy;
using sacm::commands::SetCitation;
using sacm::model::Document;
using sacm::model::ElementId;

Document make_minimal_case() {
    Document document;
    EXPECT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "Case"})
                    .applied);
    EXPECT_TRUE(document
                    .apply(CreateArgumentPackage{
                        .parent = ElementId{"acp_1"}, .id = ElementId{"argpkg_1"}, .name = "Arg"})
                    .applied);
    EXPECT_TRUE(document
                    .apply(CreateClaim{.parent = ElementId{"argpkg_1"},
                                       .id = ElementId{"claim_1"},
                                       .name = "C1",
                                       .description = "claim one"})
                    .applied);
    EXPECT_TRUE(document
                    .apply(CreateClaim{.parent = ElementId{"argpkg_1"},
                                       .id = ElementId{"claim_2"},
                                       .name = "C2"})
                    .applied);
    return document;
}

bool lists_deletion_of(const std::vector<ChangeRecord>& effects, std::string_view id) {
    return std::ranges::any_of(effects, [&](const ChangeRecord& record) {
        return record.id.value() == id && (record.change == ChangeRecord::Change::Deleted ||
                                           record.change == ChangeRecord::Change::RelationshipDeleted);
    });
}

TEST(Sacm23Commands, SACM23_CMD_004_PreviewsClaimDelete) {
    const Document document = make_minimal_case();

    const OperationPreview preview = document.preview(DeleteElement{
        .target = ElementId{"claim_1"},
        .package_policy = PackageDeletePolicy::DeleteRecursively,
    });

    EXPECT_TRUE(preview.can_apply);
    EXPECT_EQ(preview.operation, "DeleteElement");
    // The claim and its contained description are both listed before any
    // mutation happens.
    EXPECT_TRUE(lists_deletion_of(preview.effects, "claim_1"));
    EXPECT_TRUE(lists_deletion_of(preview.effects, "description_1"));
    EXPECT_NE(document.find(ElementId{"claim_1"}), nullptr);
}

TEST(Sacm23Commands, SACM23_CMD_004_PreviewsPackageDeleteConsequences) {
    const Document document = make_minimal_case();

    // Default policy: non-empty package delete is not applicable.
    const OperationPreview rejected =
        document.preview(DeleteElement{.target = ElementId{"argpkg_1"}});
    EXPECT_FALSE(rejected.can_apply);
    ASSERT_FALSE(rejected.diagnostics.empty());
    EXPECT_EQ(rejected.diagnostics.front().code, sacm::validation::codes::kCmdPackageNotEmpty);

    // Explicit recursive policy: preview lists every contained element.
    const OperationPreview recursive = document.preview(DeleteElement{
        .target = ElementId{"argpkg_1"},
        .package_policy = PackageDeletePolicy::DeleteRecursively,
    });
    EXPECT_TRUE(recursive.can_apply);
    EXPECT_TRUE(lists_deletion_of(recursive.effects, "argpkg_1"));
    EXPECT_TRUE(lists_deletion_of(recursive.effects, "claim_1"));
    EXPECT_TRUE(lists_deletion_of(recursive.effects, "claim_2"));
    EXPECT_TRUE(lists_deletion_of(recursive.effects, "description_1"));
}

TEST(Sacm23Commands, SACM23_CMD_005_AppliesClaimDeleteWithExplicitPolicy) {
    Document document = make_minimal_case();
    const OperationPreview preview = document.preview(DeleteElement{
        .target = ElementId{"claim_2"},
    });
    ASSERT_TRUE(preview.can_apply);

    const MutationResult result = document.apply(
        DeleteElement{.target = ElementId{"claim_2"}}, preview.document_revision);

    ASSERT_TRUE(result.applied);
    EXPECT_EQ(document.find(ElementId{"claim_2"}), nullptr);
    const auto* package = document.find_as<sacm::model::ArgumentPackage>(ElementId{"argpkg_1"});
    ASSERT_NE(package, nullptr);
    EXPECT_EQ(package->argument_elements().size(), 1u);
    EXPECT_TRUE(sacm::validation::validate(document).empty());
}

TEST(Sacm23Commands, SACM23_CMD_005_RejectsDeleteWhenPolicyDisallowsAffectedReferences) {
    Document document = make_minimal_case();
    // claim_1 cites claim_2; deleting claim_2 is then policy-controlled.
    ASSERT_TRUE(document
                    .apply(SetCitation{.element = ElementId{"claim_1"},
                                       .cited = ElementId{"claim_2"}})
                    .applied);

    const MutationResult rejected = document.apply(DeleteElement{.target = ElementId{"claim_2"}});
    EXPECT_FALSE(rejected.applied);
    ASSERT_FALSE(rejected.diagnostics.empty());
    EXPECT_EQ(rejected.diagnostics.front().code, sacm::validation::codes::kCmdDeleteReferenced);
    EXPECT_NE(document.find(ElementId{"claim_2"}), nullptr);

    // Explicit cascade policy removes the citation and deletes the claim,
    // leaving no dangling reference behind.
    const MutationResult cascaded = document.apply(DeleteElement{
        .target = ElementId{"claim_2"},
        .reference_policy = ReferenceDeletePolicy::DeleteReferencingRelationships,
    });
    ASSERT_TRUE(cascaded.applied);
    EXPECT_EQ(document.find(ElementId{"claim_2"}), nullptr);
    const auto* claim_1 = document.find(ElementId{"claim_1"});
    ASSERT_NE(claim_1, nullptr);
    EXPECT_FALSE(claim_1->cited_element().has_value());
    EXPECT_TRUE(sacm::validation::validate(document).empty());
    EXPECT_TRUE(std::ranges::any_of(cascaded.changes, [](const ChangeRecord& record) {
        return record.change == ChangeRecord::Change::Modified &&
               record.id.value() == "claim_1";
    }));
}

TEST(Sacm23Commands, SACM23_CMD_004_PreviewExpiresAfterInterveningMutation) {
    Document document = make_minimal_case();
    const OperationPreview preview =
        document.preview(DeleteElement{.target = ElementId{"claim_2"}});
    ASSERT_TRUE(preview.can_apply);

    // Intervening mutation invalidates the preview.
    ASSERT_TRUE(document
                    .apply(CreateClaim{.parent = ElementId{"argpkg_1"},
                                       .id = ElementId{"claim_3"},
                                       .name = "C3"})
                    .applied);

    const MutationResult stale = document.apply(
        DeleteElement{.target = ElementId{"claim_2"}}, preview.document_revision);
    EXPECT_FALSE(stale.applied);
    ASSERT_FALSE(stale.diagnostics.empty());
    EXPECT_EQ(stale.diagnostics.front().code, sacm::validation::codes::kCmdPreviewExpired);
    EXPECT_NE(document.find(ElementId{"claim_2"}), nullptr);
}

}  // namespace
