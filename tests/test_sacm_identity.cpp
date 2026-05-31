#include "core/sacm_identity.h"

#include "sacm/sacm_model.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

// Intent of sacm_identity: GenerateSacmGid produces a fresh UUID-shaped value;
// EnsureElementGid assigns a unique generated gid to a parser element that
// lacks one and keeps the SACM package element in sync, reporting
// AlreadyPresent when a gid exists and Failed when the package has no matching
// element to update. Assertions encode that intent; failures are findings.

TEST(SacmIdentityTest, GenerateSacmGidProducesDistinctUuidShapedValues) {
    const std::string a = core::GenerateSacmGid();
    const std::string b = core::GenerateSacmGid();

    EXPECT_FALSE(a.empty());
    // UUID canonical form has four '-' separators.
    EXPECT_EQ(std::count(a.begin(), a.end(), '-'), 4);
    EXPECT_NE(a, b) << "consecutive gids should be unique";
}

TEST(SacmIdentityTest, EnsureElementGidLeavesExistingGidUntouched) {
    parser::AssuranceCase model;
    parser::SacmElement element;
    element.id = "G1";
    element.gid = "existing-gid";
    model.elements.push_back(element);

    std::string error;
    const core::EnsureGidResult result =
        core::EnsureElementGid(model, nullptr, model.elements.front(), error);

    EXPECT_EQ(result, core::EnsureGidResult::AlreadyPresent);
    EXPECT_EQ(model.elements.front().gid, "existing-gid");
    EXPECT_TRUE(error.empty());
}

TEST(SacmIdentityTest, EnsureElementGidGeneratesGidWhenNoPackageProvided) {
    parser::AssuranceCase model;
    parser::SacmElement element;
    element.id = "G1";
    model.elements.push_back(element);

    std::string error;
    const core::EnsureGidResult result =
        core::EnsureElementGid(model, nullptr, model.elements.front(), error);

    EXPECT_EQ(result, core::EnsureGidResult::Generated);
    EXPECT_FALSE(model.elements.front().gid.empty());
    EXPECT_TRUE(error.empty());
}

TEST(SacmIdentityTest, EnsureElementGidSyncsParserAndPackageElement) {
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

    std::string error;
    const core::EnsureGidResult result =
        core::EnsureElementGid(model, &package, model.elements.front(), error);

    ASSERT_EQ(result, core::EnsureGidResult::Generated) << error;
    const std::string& generated = model.elements.front().gid;
    EXPECT_FALSE(generated.empty());
    // The matching package element must receive the same generated gid.
    EXPECT_EQ(package.argumentPackages.front().claims.front().gid, generated);
}

TEST(SacmIdentityTest, EnsureElementGidFailsWhenPackageHasNoMatchingElement) {
    parser::AssuranceCase model;
    parser::SacmElement element;
    element.id = "G1";
    model.elements.push_back(element);

    sacm::AssuranceCasePackage package; // no element with id "G1"

    std::string error;
    const core::EnsureGidResult result =
        core::EnsureElementGid(model, &package, model.elements.front(), error);

    EXPECT_EQ(result, core::EnsureGidResult::Failed);
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(model.elements.front().gid.empty()) << "no gid should be assigned on failure";
}
