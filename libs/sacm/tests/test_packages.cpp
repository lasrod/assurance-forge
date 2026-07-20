#include "sacm/compare/semantic_compare.h"
#include "sacm/io/xmi.h"
#include "sacm/model/document.h"
#include "sacm/validation/codes.h"
#include "sacm/validation/validate.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

namespace {

using sacm::commands::ChangeRecord;
using sacm::commands::CreateAssuranceCasePackage;
using sacm::commands::CrossPackageReferencePolicy;
using sacm::commands::DeleteElement;
using sacm::commands::MutationResult;
using sacm::commands::OperationPreview;
using sacm::commands::PackageDeletePolicy;
using sacm::commands::ReferenceDeletePolicy;
using sacm::io::LoadOptions;
using sacm::io::LoadResult;
using sacm::io::Mode;
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

LoadResult load_nested_fixture() {
    return sacm::io::load_xmi_file(fixture("package-nested-interfaces-valid.sacm.xmi"),
                                   LoadOptions{.mode = Mode::Strict});
}

TEST(Sacm23Packages, SACM23_PKG_001_NestedPackagesRoundTrip) {
    const LoadResult first = load_nested_fixture();
    ASSERT_TRUE(first.ok) << (first.diagnostics.empty() ? "" : first.diagnostics.front().message);
    const auto& document = *first.document;

    const auto& root = *document.roots().front();
    ASSERT_EQ(root.assurance_case_packages().size(), 1u);
    const auto& nested = *root.assurance_case_packages().front();
    EXPECT_EQ(nested.argument_packages().size(), 4u);  // A, A-interface, B, binding
    EXPECT_TRUE(sacm::validation::validate(document).empty());

    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult second = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(second.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *second.document).empty());
}

TEST(Sacm23Packages, SACM23_PKG_002_InterfacesAndBindingsPreserveParticipants) {
    const LoadResult result = load_nested_fixture();
    ASSERT_TRUE(result.ok);
    const auto& document = *result.document;

    const auto* interface_pkg =
        document.find_as<sacm::model::ArgumentPackageInterface>(ElementId{"argpkg_a_if"});
    ASSERT_NE(interface_pkg, nullptr);
    ASSERT_TRUE(interface_pkg->implements().has_value());
    EXPECT_EQ(interface_pkg->implements()->value(), "argpkg_a");

    const auto* package = document.find_as<sacm::model::ArgumentPackage>(ElementId{"argpkg_a"});
    ASSERT_NE(package, nullptr);
    ASSERT_EQ(package->interfaces().size(), 1u);
    EXPECT_EQ(package->interfaces().front().value(), "argpkg_a_if");

    const auto* binding =
        document.find_as<sacm::model::ArgumentPackageBinding>(ElementId{"argpkg_binding"});
    ASSERT_NE(binding, nullptr);
    ASSERT_EQ(binding->participant_packages().size(), 2u);
}

TEST(Sacm23Packages, SACM23_PKG_002_BindingWithOneParticipantIsInvalid) {
    const LoadResult result = sacm::io::load_xmi_file(
        fixture("invalid/binding-participants-invalid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(result.document.has_value());
    const auto diagnostics = sacm::validation::validate(*result.document);
    EXPECT_TRUE(has_code(diagnostics, sacm::validation::codes::kMultiplicityViolation));
}

TEST(Sacm23Packages, SACM23_PKG_003_RecursiveDeletePreviewListsNestedContent) {
    const LoadResult result = load_nested_fixture();
    ASSERT_TRUE(result.ok);
    const auto& document = *result.document;

    const OperationPreview preview = document.preview(DeleteElement{
        .target = ElementId{"acp_nested"},
        .reference_policy = ReferenceDeletePolicy::DeleteReferencingRelationships,
        .package_policy = PackageDeletePolicy::DeleteRecursively,
        .cross_package_policy = CrossPackageReferencePolicy::DeleteExternalReferencingRelationships,
    });
    EXPECT_TRUE(preview.can_apply);
    const auto listed = [&preview](std::string_view id) {
        return std::ranges::any_of(preview.effects, [&](const ChangeRecord& record) {
            return record.id.value() == id;
        });
    };
    for (const std::string_view id :
         {"acp_nested", "argpkg_a", "argpkg_a_if", "argpkg_b", "argpkg_binding", "claim_a",
          "claim_b", "ctx_1"}) {
        EXPECT_TRUE(listed(id)) << "preview should list " << id;
    }
}

TEST(Sacm23Packages, SACM23_PKG_003_CrossPackageReferencePoliciesAreExplicit) {
    LoadResult loaded = load_nested_fixture();
    ASSERT_TRUE(loaded.ok);
    ASSERT_TRUE(loaded.document.has_value());
    Document document = std::move(*loaded.document);

    // ctx_1 lives in argpkg_b and targets claim_a in argpkg_a: deleting
    // claim_a is a cross-package effect and must be explicit.
    const MutationResult rejected = document.apply(DeleteElement{
        .target = ElementId{"claim_a"},
        .reference_policy = ReferenceDeletePolicy::DeleteReferencingRelationships,
    });
    EXPECT_FALSE(rejected.applied);
    ASSERT_FALSE(rejected.diagnostics.empty());
    EXPECT_EQ(rejected.diagnostics.front().code, sacm::validation::codes::kCmdExternalReferences);
    EXPECT_NE(document.find(ElementId{"claim_a"}), nullptr);

    const MutationResult applied = document.apply(DeleteElement{
        .target = ElementId{"claim_a"},
        .reference_policy = ReferenceDeletePolicy::DeleteReferencingRelationships,
        .cross_package_policy = CrossPackageReferencePolicy::DeleteExternalReferencingRelationships,
    });
    ASSERT_TRUE(applied.applied);
    EXPECT_EQ(document.find(ElementId{"claim_a"}), nullptr);
    EXPECT_EQ(document.find(ElementId{"ctx_1"}), nullptr);  // cascaded relationship
    EXPECT_NE(document.find(ElementId{"claim_b"}), nullptr);
    EXPECT_TRUE(sacm::validation::validate(document).empty());
    EXPECT_TRUE(std::ranges::any_of(applied.changes, [](const ChangeRecord& record) {
        return record.id.value() == "ctx_1" &&
               record.change == ChangeRecord::Change::RelationshipDeleted;
    }));
}

TEST(Sacm23Packages, SACM23_PKG_001_CreatesNestedAssuranceCasePackages) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_root"}, .name = "R"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateAssuranceCasePackage{.id = ElementId{"acp_child"},
                                                      .name = "C",
                                                      .parent = ElementId{"acp_root"}})
                    .applied);
    const auto* child = document.find_as<sacm::model::AssuranceCasePackage>(ElementId{"acp_child"});
    ASSERT_NE(child, nullptr);
    ASSERT_NE(child->parent(), nullptr);
    EXPECT_EQ(child->parent()->id().value(), "acp_root");
    EXPECT_EQ(document.roots().size(), 1u);
}

}  // namespace
