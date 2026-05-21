#include "core/argument_package_projection.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <gtest/gtest.h>

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

} // namespace

TEST(ArgumentPackageProjection, FindByIdMatchesIdField) {
    sacm::AssuranceCasePackage acp;
    acp.argumentPackages.push_back(MakePackage("AP1", {"G1"}));
    acp.argumentPackages.push_back(MakePackage("AP2", {"G2"}));

    const sacm::ArgumentPackage* found =
        core::FindArgumentPackageByIdentity(acp, "AP2", "");
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

    const parser::AssuranceCase projection =
        core::BuildArgumentPackageProjection(source, pkg, "fallback");
    EXPECT_EQ(projection.id, "AP-A");
    ASSERT_EQ(projection.elements.size(), 2u);
    EXPECT_EQ(projection.elements[0].id, "G1");
    EXPECT_EQ(projection.elements[1].id, "S1");
}

TEST(ArgumentPackageProjection, BuildProjectionUsesFallbackNameWhenPackageEmpty) {
    parser::AssuranceCase source;
    sacm::ArgumentPackage pkg;
    pkg.id = "AP-X";
    // pkg.name intentionally empty
    const parser::AssuranceCase projection =
        core::BuildArgumentPackageProjection(source, pkg, "Fallback Title");
    EXPECT_EQ(projection.name, "Fallback Title");
}
