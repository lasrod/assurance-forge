#include "core/sacm_identity.h"

#include "legacy_sacm/sacm_model.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

// Intent of sacm_identity: GenerateSacmGid produces a fresh UUID-shaped value;
// GenerateUniqueElementGid produces one that does not collide with an existing
// element gid in the model; SetElementGid assigns a supplied gid to a parser
// element (looked up by id) and keeps the matching SACM package element in sync,
// leaving the model unchanged and reporting an error when the package has no
// matching element to update. Generation is split from application so an audited
// command can mint the gid once and force the same value on replay. Assertions
// encode that intent; failures are findings.

TEST(SacmIdentityTest, GenerateSacmGidProducesDistinctUuidShapedValues) {
    const std::string a = core::GenerateSacmGid();
    const std::string b = core::GenerateSacmGid();

    EXPECT_FALSE(a.empty());
    // UUID canonical form has four '-' separators.
    EXPECT_EQ(std::count(a.begin(), a.end(), '-'), 4);
    EXPECT_NE(a, b) << "consecutive gids should be unique";
}

TEST(SacmIdentityTest, GenerateUniqueElementGidAvoidsExistingGid) {
    parser::AssuranceCase model;
    parser::SacmElement existing;
    existing.id = "G0";
    existing.gid = "keep-this-gid";
    model.elements.push_back(existing);

    const std::string generated = core::GenerateUniqueElementGid(model);

    EXPECT_FALSE(generated.empty());
    EXPECT_EQ(std::count(generated.begin(), generated.end(), '-'), 4);
    EXPECT_NE(generated, "keep-this-gid") << "generated gid must not collide with an existing element";
}

TEST(SacmIdentityTest, SetElementGidSetsModelElementWhenNoPackage) {
    parser::AssuranceCase model;
    parser::SacmElement element;
    element.id = "G1";
    model.elements.push_back(element);

    const std::string gid = core::GenerateUniqueElementGid(model);
    std::string error;
    ASSERT_TRUE(core::SetElementGid(model, nullptr, "G1", gid, error)) << error;

    EXPECT_EQ(model.elements.front().gid, gid);
    EXPECT_TRUE(error.empty());
}

TEST(SacmIdentityTest, SetElementGidSyncsParserAndPackageElement) {
    parser::AssuranceCase model;
    parser::SacmElement element;
    element.id = "G1";
    model.elements.push_back(element);

    sacm::AssuranceCasePackage package;
    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    sacm::Claim claim;
    claim.id = "G1"; // same id as the parser element
    argument_package.claims.push_back(claim);
    package.argumentPackages.push_back(argument_package);

    const std::string gid = core::GenerateUniqueElementGid(model);
    std::string error;
    ASSERT_TRUE(core::SetElementGid(model, &package, "G1", gid, error)) << error;

    EXPECT_EQ(model.elements.front().gid, gid);
    // The matching package element must receive the same gid.
    EXPECT_EQ(package.argumentPackages.front().claims.front().gid, gid);
}

TEST(SacmIdentityTest, SetElementGidFailsWhenPackageHasNoMatchingElement) {
    parser::AssuranceCase model;
    parser::SacmElement element;
    element.id = "G1";
    model.elements.push_back(element);

    sacm::AssuranceCasePackage package; // no element with id "G1"

    const std::string gid = core::GenerateUniqueElementGid(model);
    std::string error;
    const bool ok = core::SetElementGid(model, &package, "G1", gid, error);

    EXPECT_FALSE(ok);
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(model.elements.front().gid.empty()) << "no gid should be assigned on failure";
}

TEST(SacmIdentityTest, SetElementGidFailsWhenModelHasNoMatchingElement) {
    parser::AssuranceCase model; // no element with id "G1"

    std::string error;
    const bool ok = core::SetElementGid(model, nullptr, "G1", "any-gid", error);

    EXPECT_FALSE(ok);
    EXPECT_FALSE(error.empty());
}
