#include "sacm/model/document.h"

#include "sacm/validation/codes.h"
#include "sacm/validation/validate.h"

#include <gtest/gtest.h>

#include <set>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using sacm::commands::CreateArgumentPackage;
using sacm::commands::CreateAssuranceCasePackage;
using sacm::commands::CreateClaim;
using sacm::commands::MutationResult;
using sacm::model::Document;
using sacm::model::ElementId;

// Every operation the public variant exposes must carry a stable, SACM-native
// name. Iterating the variant rather than a hand-listed set means an operation
// added later is covered automatically instead of silently escaping the check.
template <std::size_t... I>
std::vector<std::string_view> all_operation_names(std::index_sequence<I...>) {
    return {sacm::commands::operation_name(
        sacm::commands::Operation{std::in_place_index<I>,
                                  std::variant_alternative_t<I, sacm::commands::Operation>{}})...};
}

TEST(Sacm23Commands, SACM23_CMD_001_OperationsAreSacmNativeWithStructuredResults) {
    constexpr std::size_t kOperationCount = std::variant_size_v<sacm::commands::Operation>;
    const std::vector<std::string_view> names =
        all_operation_names(std::make_index_sequence<kOperationCount>{});
    ASSERT_EQ(names.size(), kOperationCount);

    // GSN/UI vocabulary must not appear in the SACM-native command surface;
    // Goal/Strategy/Solution belong to the Assurance Forge adapter, not here.
    for (const std::string_view name : names) {
        EXPECT_FALSE(name.empty()) << "operation_name returned an empty name";
        for (const std::string_view banned :
             {"Goal", "Strategy", "Solution", "Node", "Canvas", "Tree", "Layout"}) {
            EXPECT_EQ(name.find(banned), std::string_view::npos)
                << "operation " << name << " leaks GSN/UI terminology: " << banned;
        }
    }
    // Names are the stable identity used in previews, results, and diagnostics,
    // so they must be distinct.
    const std::set<std::string_view> unique(names.begin(), names.end());
    EXPECT_EQ(unique.size(), names.size()) << "operation_name is not injective";

    // Structured mutation results: a create reports the operation and a change
    // record naming what was created, not just a success flag.
    Document document;
    const MutationResult result = document.apply(
        CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "Case"});
    ASSERT_TRUE(result.applied);
    EXPECT_EQ(result.operation, "CreateAssuranceCasePackage");
    ASSERT_FALSE(result.changes.empty());
    EXPECT_EQ(result.created_ids().size(), 1u);
    EXPECT_EQ(result.created_ids().front().value(), "acp_1");
}

TEST(Sacm23Commands, SACM23_CMD_002_CreatesDocumentWithAssuranceCasePackage) {
    Document document;
    EXPECT_EQ(document.roots().size(), 0u);
    EXPECT_EQ(document.revision(), 0u);

    const MutationResult result = document.apply(CreateAssuranceCasePackage{
        .id = ElementId{"acp_1"},
        .name = "Vehicle Safety Case",
    });

    ASSERT_TRUE(result.applied);
    EXPECT_EQ(result.operation, "CreateAssuranceCasePackage");
    EXPECT_EQ(document.revision(), 1u);
    ASSERT_EQ(document.roots().size(), 1u);
    const auto& package = *document.roots().front();
    EXPECT_EQ(package.id().value(), "acp_1");
    EXPECT_EQ(package.name().content, "Vehicle Safety Case");
    EXPECT_EQ(document.find(ElementId{"acp_1"}), &package);
    EXPECT_TRUE(sacm::validation::validate(document).empty());
}

TEST(Sacm23Commands, SACM23_CMD_003_CreatesArgumentPackageAndClaim) {
    Document document;
    ASSERT_TRUE(document
                    .apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "Case"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateArgumentPackage{
                        .parent = ElementId{"acp_1"},
                        .id = ElementId{"argpkg_1"},
                        .name = "Braking Argument",
                    })
                    .applied);
    const MutationResult claim_result = document.apply(CreateClaim{
        .parent = ElementId{"argpkg_1"},
        .id = ElementId{"claim_top"},
        .name = "TopClaim",
        .description = "The braking function is acceptably safe.",
        .language = "en",
    });

    ASSERT_TRUE(claim_result.applied);
    const auto* claim = document.find_as<sacm::model::Claim>(ElementId{"claim_top"});
    ASSERT_NE(claim, nullptr);
    EXPECT_EQ(claim->name().content, "TopClaim");
    EXPECT_EQ(claim->description().primary(), "The braking function is acceptably safe.");
    ASSERT_NE(claim->parent(), nullptr);
    EXPECT_EQ(claim->parent()->id().value(), "argpkg_1");

    const auto* package = document.find_as<sacm::model::ArgumentPackage>(ElementId{"argpkg_1"});
    ASSERT_NE(package, nullptr);
    ASSERT_EQ(package->argument_elements().size(), 1u);
    EXPECT_EQ(package->argument_elements().front()->id().value(), "claim_top");
    EXPECT_TRUE(sacm::validation::validate(document).empty());
}

TEST(Sacm23Commands, SACM23_CMD_002_SupportsCallerProvidedAndGeneratedIds) {
    Document document;
    // Generated ids are deterministic and stable.
    const MutationResult first = document.apply(CreateAssuranceCasePackage{.name = "A"});
    ASSERT_TRUE(first.applied);
    ASSERT_EQ(first.created_ids().size(), 1u);
    EXPECT_EQ(first.created_ids().front().value(), "assurance_case_package_1");

    const MutationResult second = document.apply(CreateAssuranceCasePackage{.name = "B"});
    ASSERT_TRUE(second.applied);
    EXPECT_EQ(second.created_ids().front().value(), "assurance_case_package_2");

    // Generated ids never collide with caller-provided ones.
    ASSERT_TRUE(document
                    .apply(CreateAssuranceCasePackage{
                        .id = ElementId{"assurance_case_package_3"}, .name = "C"})
                    .applied);
    const MutationResult fourth = document.apply(CreateAssuranceCasePackage{.name = "D"});
    ASSERT_TRUE(fourth.applied);
    EXPECT_EQ(fourth.created_ids().front().value(), "assurance_case_package_4");
}

TEST(Sacm23Commands, SACM23_CMD_002_RejectsDuplicateCallerProvidedId) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "A"})
                    .applied);
    const MutationResult result =
        document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "B"});

    EXPECT_FALSE(result.applied);
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(result.diagnostics.front().code, sacm::validation::codes::kCmdDuplicateId);
    EXPECT_EQ(document.roots().size(), 1u);
    EXPECT_EQ(document.revision(), 1u);
}

TEST(Sacm23Commands, SACM23_VAL_002_MutationsLeaveDocumentValidOrUnchanged) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "A"})
                    .applied);
    const std::uint64_t revision_before = document.revision();
    const std::size_t count_before = document.element_count();

    // Claim under a non-ArgumentPackage parent must fail without mutating.
    const MutationResult bad_parent = document.apply(CreateClaim{
        .parent = ElementId{"acp_1"},
        .name = "Claim",
    });
    EXPECT_FALSE(bad_parent.applied);
    EXPECT_EQ(bad_parent.diagnostics.front().code, sacm::validation::codes::kCmdInvalidParent);

    // Unknown parent must fail without mutating.
    const MutationResult missing_parent = document.apply(CreateClaim{
        .parent = ElementId{"nope"},
        .name = "Claim",
    });
    EXPECT_FALSE(missing_parent.applied);
    EXPECT_EQ(missing_parent.diagnostics.front().code,
              sacm::validation::codes::kCmdTargetNotFound);

    EXPECT_EQ(document.revision(), revision_before);
    EXPECT_EQ(document.element_count(), count_before);
    EXPECT_TRUE(sacm::validation::validate(document).empty());
}

TEST(Sacm23Commands, SACM23_CMD_006_MutationResultCarriesAuditData) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "A"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateArgumentPackage{
                        .parent = ElementId{"acp_1"}, .id = ElementId{"argpkg_1"}, .name = "Arg"})
                    .applied);
    const MutationResult result = document.apply(CreateClaim{
        .parent = ElementId{"argpkg_1"},
        .id = ElementId{"claim_1"},
        .name = "C1",
        .description = "text",
    });

    ASSERT_TRUE(result.applied);
    // Claim plus its Description child, each with kind, parent, and value.
    ASSERT_EQ(result.changes.size(), 2u);
    const auto& claim_change = result.changes[0];
    EXPECT_EQ(claim_change.id.value(), "claim_1");
    EXPECT_EQ(claim_change.kind, sacm::metadata::ElementKind::Claim);
    EXPECT_EQ(claim_change.change, sacm::commands::ChangeRecord::Change::Created);
    ASSERT_TRUE(claim_change.parent.has_value());
    EXPECT_EQ(claim_change.parent->value(), "argpkg_1");
    ASSERT_TRUE(claim_change.after.has_value());

    const auto& description_change = result.changes[1];
    EXPECT_EQ(description_change.kind, sacm::metadata::ElementKind::Description);
    ASSERT_TRUE(description_change.parent.has_value());
    EXPECT_EQ(description_change.parent->value(), "claim_1");
    EXPECT_EQ(result.document_revision, document.revision());
}

}  // namespace
