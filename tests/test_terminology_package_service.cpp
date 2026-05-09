#include "core/terminology_package_service.h"
#include "sacm/sacm_parser.h"
#include "sacm/sacm_serializer.h"

#include <gtest/gtest.h>

namespace {

sacm::AssuranceCasePackage MakePackage() {
    sacm::AssuranceCasePackage package;
    package.id = "ACP1";
    package.name = "Assurance Case";
    package.name_ml.set("en", package.name);
    return package;
}

} // namespace

TEST(TerminologyPackageService, CreateAssignsUniqueIdGidAndEditableText) {
    sacm::AssuranceCasePackage package = MakePackage();
    sacm::TerminologyPackage existing;
    existing.id = "TP1";
    existing.gid = "gid-TP1";
    package.terminologyPackages.push_back(existing);

    core::TerminologyPackageCreateResult result =
        core::CreateTerminologyPackage(package, "Project Terms", "Terms used by this safety case.");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.package_ref.id, "TP2");
    EXPECT_EQ(result.package_ref.gid, "gid-TP2");
    ASSERT_EQ(package.terminologyPackages.size(), 2u);

    const sacm::TerminologyPackage* created = core::FindTerminologyPackage(package, result.package_ref);
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created->name, "Project Terms");
    EXPECT_EQ(created->name_ml.get_primary(), "Project Terms");
    EXPECT_EQ(created->description, "Terms used by this safety case.");
    EXPECT_EQ(created->description_ml.get_primary(), "Terms used by this safety case.");
}

TEST(TerminologyPackageService, UpdateEditsNameAndDescription) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult created = core::CreateTerminologyPackage(package, "Old", "Old description");
    ASSERT_TRUE(created.success);

    std::string error;
    ASSERT_TRUE(core::UpdateTerminologyPackage(package, created.package_ref, "New", "New description", error))
        << error;

    const sacm::TerminologyPackage* updated = core::FindTerminologyPackage(package, created.package_ref);
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->name, "New");
    EXPECT_EQ(updated->name_ml.get_primary(), "New");
    EXPECT_EQ(updated->description, "New description");
    EXPECT_EQ(updated->description_ml.get_primary(), "New description");
}

TEST(TerminologyPackageService, DeleteOnlyAllowsEmptyPackages) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult created = core::CreateTerminologyPackage(package, "Terms", "");
    ASSERT_TRUE(created.success);
    package.terminologyPackages.front().expressions.push_back(sacm::Expression{});

    std::string error;
    EXPECT_FALSE(core::DeleteTerminologyPackage(package, created.package_ref, error));
    EXPECT_EQ(package.terminologyPackages.size(), 1u);
    EXPECT_FALSE(error.empty());

    package.terminologyPackages.front().expressions.clear();
    ASSERT_TRUE(core::DeleteTerminologyPackage(package, created.package_ref, error)) << error;
    EXPECT_TRUE(package.terminologyPackages.empty());
}

TEST(TerminologyPackageService, CreatedPackageSerializesAndParses) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult created =
        core::CreateTerminologyPackage(package, "Vocabulary", "Shared definitions.");
    ASSERT_TRUE(created.success);

    std::string xml = sacm::serialize_sacm(package);
    ASSERT_FALSE(xml.empty());

    sacm::SacmParseResult parsed = sacm::parse_sacm_string(xml);
    ASSERT_TRUE(parsed.success) << parsed.error_message;

    const sacm::TerminologyPackage* reparsed = core::FindTerminologyPackage(parsed.package, created.package_ref);
    ASSERT_NE(reparsed, nullptr);
    EXPECT_EQ(reparsed->name, "Vocabulary");
    EXPECT_EQ(reparsed->description, "Shared definitions.");
}
