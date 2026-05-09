#include "core/app_state.h"
#include "core/assurance_tree.h"
#include "core/terminology_package_service.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(std::filesystem::path value) : path(std::move(value)) {}
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::filesystem::path MakeTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("assurance_forge_app_state_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& model, const std::string& id) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

} // namespace

TEST(AppStateTest, LoadFileHidesTerminologyArtifactReferencesButKeepsEvidenceSolutions) {
    TempDir temp(MakeTempDir());
    const std::filesystem::path sacm_path = temp.path / "case.sacm";
    std::ofstream(sacm_path) << R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <terminologyPackage id="TP" name="Glossary">
    <term id="TERM_ODD" name="Operational Design Domain" value="ODD"/>
  </terminologyPackage>
  <argumentPackage id="AP" name="AP">
    <claim id="G1" name="Goal" assertionDeclaration="asserted"/>
    <artifactReference id="EV1" name="Evidence"/>
    <artifactReference id="TERM_CTX" name="ODD Context" referencedArtifact="TERM_ODD"/>
    <assertedEvidence id="AE1" name="Evidence relation">
      <source ref="EV1"/>
      <target ref="G1"/>
    </assertedEvidence>
    <assertedContext id="AC_TERM" name="Terminology relation">
      <source ref="TERM_CTX"/>
      <target ref="G1"/>
    </assertedContext>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    core::AppState state;
    ASSERT_TRUE(state.load_file(sacm_path.string())) << state.status_message;
    ASSERT_TRUE(state.loaded_case.has_value());
    ASSERT_TRUE(state.sacm_package.has_value());

    EXPECT_NE(FindElement(state.loaded_case.value(), "EV1"), nullptr);
    EXPECT_NE(FindElement(state.loaded_case.value(), "AE1"), nullptr);
    EXPECT_EQ(FindElement(state.loaded_case.value(), "TERM_CTX"), nullptr);
    EXPECT_EQ(FindElement(state.loaded_case.value(), "AC_TERM"), nullptr);
    ASSERT_EQ(state.sacm_package->argumentPackages.size(), 1u);
    EXPECT_EQ(state.sacm_package->argumentPackages.front().artifactReferences.size(), 2u);
    EXPECT_EQ(state.sacm_package->argumentPackages.front().assertedContexts.size(), 1u);
}

TEST(AppStateTest, LoadFileKeepsVisibleTerminologyContextOnCanvas) {
    TempDir temp(MakeTempDir());
    const std::filesystem::path sacm_path = temp.path / "case.sacm";
    std::ofstream(sacm_path) << R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <terminologyPackage id="TP" name="Glossary">
    <term id="TERM_ODD" name="Operational Design Domain" value="ODD"/>
  </terminologyPackage>
  <argumentPackage id="AP" name="AP">
    <claim id="G1" name="Goal" assertionDeclaration="asserted"/>
    <artifactReference id="TERM_HIDDEN" name="Hidden ODD" referencedArtifact="TERM_ODD"/>
    <artifactReference id="TERM_VISIBLE" name="ODD" referencedArtifact="TERM_ODD"/>
    <assertedContext id="AC_HIDDEN" name="Hidden terminology relation">
      <source ref="TERM_HIDDEN"/>
      <target ref="G1"/>
    </assertedContext>
    <assertedContext id="AC_VISIBLE" name="Visible terminology relation">
      <description><content lang="en">assurance-forge:visible-term-context</content></description>
      <source ref="TERM_VISIBLE"/>
      <target ref="G1"/>
    </assertedContext>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    core::AppState state;
    ASSERT_TRUE(state.load_file(sacm_path.string())) << state.status_message;
    ASSERT_TRUE(state.loaded_case.has_value());
    ASSERT_TRUE(state.sacm_package.has_value());

    EXPECT_EQ(FindElement(state.loaded_case.value(), "TERM_HIDDEN"), nullptr);
    EXPECT_EQ(FindElement(state.loaded_case.value(), "AC_HIDDEN"), nullptr);
    EXPECT_NE(FindElement(state.loaded_case.value(), "TERM_VISIBLE"), nullptr);
    EXPECT_NE(FindElement(state.loaded_case.value(), "AC_VISIBLE"), nullptr);

    core::AssuranceTree tree = core::AssuranceTree::Build(state.loaded_case.value());
    ASSERT_NE(tree.root, nullptr);
    ASSERT_EQ(tree.root->group2_attachments.size(), 1u);
    EXPECT_EQ(tree.root->group2_attachments.front()->id, "TERM_VISIBLE");
    EXPECT_EQ(tree.root->group2_attachments.front()->role, core::NodeRole::Context);
}