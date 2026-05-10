#include "sacm/sacm_package_tree.h"
#include "sacm/sacm_parser.h"

#include <gtest/gtest.h>

namespace {

const sacm::SacmPackageTreeNode& OnlyChild(const sacm::SacmPackageTreeNode& node) {
    if (node.children.size() != 1u) {
        ADD_FAILURE() << "Expected exactly one child, found " << node.children.size();
        static const sacm::SacmPackageTreeNode empty_node;
        return node.children.empty() ? empty_node : node.children.front();
    }
    return node.children.front();
}

} // namespace

TEST(SacmPackageTree, ExtractsKnownPackagesInterfacesBindingsAndUnknownPackages) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    id="ACP1" gid="gid-acp" name="Main Safety Case">
    <argumentPackage id="AP1" name="Main Argument">
        <terminologyPackage id="NESTED_TP" name="Nested Terms" />
    </argumentPackage>
    <artifactPackage id="ARTP1" name="Evidence" />
    <terminologyPackage id="TP1" name="Project Glossary" />
    <assuranceCasePackageInterface id="ACPI1" name="Case Interface" />
    <argumentPackageInterface id="API1" name="Argument Interface" />
    <artifactPackageInterface id="ARPI1" name="Artifact Interface" />
    <terminologyPackageInterface id="TPI1" name="Terminology Interface" />
    <assuranceCasePackageBinding id="ACPB1" name="Case Binding" />
    <argumentPackageBinding id="APB1" name="Argument Binding" />
    <artifactPackageBinding id="ARPB1" name="Artifact Binding" />
    <terminologyPackageBinding id="TPB1" name="Terminology Binding" />
    <customPackage id="CUSTOM1" name="Custom Package" />
</sacm:AssuranceCasePackage>)";

    auto result = sacm::build_sacm_package_tree_from_string(xml, "safety-case.sacm");

    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.root.type, sacm::SacmPackageNodeType::SacmFile);
    const auto& root_package = OnlyChild(result.root);
    EXPECT_EQ(root_package.type, sacm::SacmPackageNodeType::AssuranceCasePackage);
    EXPECT_EQ(root_package.id, "ACP1");
    EXPECT_EQ(root_package.gid, "gid-acp");
    EXPECT_EQ(root_package.displayName, "Main Safety Case");
    ASSERT_EQ(root_package.children.size(), 12u);

    EXPECT_EQ(root_package.children[0].type, sacm::SacmPackageNodeType::ArgumentPackage);
    ASSERT_EQ(root_package.children[0].children.size(), 1u);
    EXPECT_EQ(root_package.children[0].children[0].type, sacm::SacmPackageNodeType::TerminologyPackage);
    EXPECT_EQ(root_package.children[1].type, sacm::SacmPackageNodeType::ArtifactPackage);
    EXPECT_EQ(root_package.children[2].type, sacm::SacmPackageNodeType::TerminologyPackage);
    EXPECT_EQ(root_package.children[3].type, sacm::SacmPackageNodeType::AssuranceCasePackageInterface);
    EXPECT_EQ(root_package.children[4].type, sacm::SacmPackageNodeType::ArgumentPackageInterface);
    EXPECT_EQ(root_package.children[5].type, sacm::SacmPackageNodeType::ArtifactPackageInterface);
    EXPECT_EQ(root_package.children[6].type, sacm::SacmPackageNodeType::TerminologyPackageInterface);
    EXPECT_EQ(root_package.children[7].type, sacm::SacmPackageNodeType::AssuranceCasePackageBinding);
    EXPECT_EQ(root_package.children[8].type, sacm::SacmPackageNodeType::ArgumentPackageBinding);
    EXPECT_EQ(root_package.children[9].type, sacm::SacmPackageNodeType::ArtifactPackageBinding);
    EXPECT_EQ(root_package.children[10].type, sacm::SacmPackageNodeType::TerminologyPackageBinding);
    EXPECT_EQ(root_package.children[11].type, sacm::SacmPackageNodeType::UnknownPackage);
}

TEST(SacmPackageTree, SupportsStandaloneTerminologyPackageRoot) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<terminologyPackage id="TP1" name="Project Glossary">
    <expression id="TERM1" value="ODD" />
</terminologyPackage>)";

    auto result = sacm::build_sacm_package_tree_from_string(xml, "terms.sacm");

    ASSERT_TRUE(result.success) << result.error_message;
    const auto& package = OnlyChild(result.root);
    EXPECT_EQ(package.type, sacm::SacmPackageNodeType::TerminologyPackage);
    EXPECT_EQ(package.id, "TP1");
    EXPECT_EQ(package.displayName, "Project Glossary");
    EXPECT_TRUE(package.children.empty());
}

TEST(SacmPackageTree, SupportsStandaloneArgumentPackageRoot) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<argumentPackage id="AP1" name="Main Argument">
    <claim id="G1" name="Goal" content="Safe" />
</argumentPackage>)";

    auto result = sacm::build_sacm_package_tree_from_string(xml, "argument.sacm");

    ASSERT_TRUE(result.success) << result.error_message;
    const auto& package = OnlyChild(result.root);
    EXPECT_EQ(package.type, sacm::SacmPackageNodeType::ArgumentPackage);
    EXPECT_EQ(package.id, "AP1");
    EXPECT_EQ(package.displayName, "Main Argument");
}

TEST(SacmPackageTree, SacmParserWrapsStandalonePackageRoots) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<argumentPackage id="AP1" name="Main Argument">
    <claim id="G1" name="Goal" content="Safe" />
</argumentPackage>)";

    auto result = sacm::parse_sacm_string(xml);

    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.package.argumentPackages.size(), 1u);
    EXPECT_EQ(result.package.argumentPackages[0].id, "AP1");
    ASSERT_EQ(result.package.argumentPackages[0].claims.size(), 1u);
    EXPECT_EQ(result.package.argumentPackages[0].claims[0].id, "G1");
}
