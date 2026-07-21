#include "core/app_state.h"
#include "core/assurance_tree.h"
#include "core/terminology_package_service.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

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

const core::ProjectFileEntry* FindProjectFileWithRole(const core::AssuranceProject& project,
                                                      core::ProjectFileRole role) {
    for (const core::ProjectFileEntry& entry : project.files) {
        if (entry.role == role)
            return &entry;
    }
    return nullptr;
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
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
    <artifactReference id="EV_GID_NAME_COLLISION" name="gid-term-ctx"/>
    <artifactReference id="TERM_CTX" gid="gid-term-ctx" name="ODD Context" referencedArtifact="TERM_ODD"/>
    <assertedEvidence id="AE1" name="Evidence relation">
      <source ref="EV1"/>
      <target ref="G1"/>
    </assertedEvidence>
    <assertedEvidence id="AE2" name="Evidence relation with colliding display name">
      <source ref="EV_GID_NAME_COLLISION"/>
      <target ref="G1"/>
    </assertedEvidence>
    <assertedContext id="AC_TERM" name="Terminology relation">
      <source ref="gid-term-ctx"/>
      <target ref="G1"/>
    </assertedContext>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    core::AppState state;
    ASSERT_TRUE(state.load_file(sacm_path.string())) << state.status_message;
    ASSERT_TRUE(state.loaded_case.has_value());
    ASSERT_TRUE(state.sacm_package.has_value());

    EXPECT_NE(FindElement(state.loaded_case.value(), "EV1"), nullptr);
    EXPECT_NE(FindElement(state.loaded_case.value(), "EV_GID_NAME_COLLISION"), nullptr);
    EXPECT_NE(FindElement(state.loaded_case.value(), "AE1"), nullptr);
    EXPECT_NE(FindElement(state.loaded_case.value(), "AE2"), nullptr);
    EXPECT_EQ(FindElement(state.loaded_case.value(), "TERM_CTX"), nullptr);
    EXPECT_EQ(FindElement(state.loaded_case.value(), "AC_TERM"), nullptr);
    ASSERT_EQ(state.sacm_package->argumentPackages.size(), 1u);
    EXPECT_EQ(state.sacm_package->argumentPackages.front().artifactReferences.size(), 3u);
    EXPECT_EQ(state.sacm_package->argumentPackages.front().assertedContexts.size(), 1u);
}

// Phase 9 Stage 4: load_file builds loaded_case by projecting the SACM library
// document, not by running the legacy parser. This proves the library path is
// taken (the document is retained) and that the projection renders the claim,
// including the statement surfacing in `content` (slice 1b: statement =
// Description).
TEST(AppStateTest, LoadFileUsesTheLibraryDocumentAsTheSourceOfTruth) {
    TempDir temp(MakeTempDir());
    const std::filesystem::path sacm_path = temp.path / "case.sacm";
    std::ofstream(sacm_path) << R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" id="CASE" name="Case">
  <argumentPackage id="AP" name="Argument">
    <claim id="G1" name="Top Goal" content="The system is acceptably safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    core::AppState state;
    ASSERT_TRUE(state.load_file(sacm_path.string())) << state.status_message;

    // The library document is retained -- loaded_case is projected from it, not
    // parsed by the legacy fallback.
    ASSERT_NE(state.library_document, nullptr)
        << "load fell back to the legacy parser: " << state.status_message;
    ASSERT_TRUE(state.loaded_case.has_value());

    const parser::SacmElement* goal = FindElement(state.loaded_case.value(), "G1");
    ASSERT_NE(goal, nullptr);
    EXPECT_EQ(goal->name, "Top Goal");
    // The statement, stored in the library as the Description, surfaces in the
    // POD content field.
    EXPECT_EQ(goal->content, "The system is acceptably safe.");
}

TEST(AppStateTest, OpenProjectSacmFilePreservesActiveProjectFile) {
    TempDir temp(MakeTempDir());
    std::filesystem::create_directories(temp.path / "arguments");
    const std::filesystem::path relative_path = std::filesystem::path("arguments") / "main.sacm";
    const std::filesystem::path sacm_path = temp.path / relative_path;
    std::ofstream(sacm_path) << R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="G1" name="Goal" assertionDeclaration="asserted"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    core::AppState state;
    core::AssuranceProject project;
    project.name = "Project";
    project.rootPath = temp.path;
    core::ProjectFileEntry entry;
    entry.relativePath = relative_path;
    entry.role = core::ProjectFileRole::SacmArgument;
    project.files.push_back(entry);
    state.current_project = project;

    ASSERT_TRUE(state.open_project_file(entry)) << state.status_message;
    EXPECT_EQ(state.active_project_file_role, core::ProjectFileRole::SacmArgument);
    EXPECT_EQ(state.active_project_file_path, sacm_path);
    EXPECT_EQ(state.loaded_file_path, sacm_path);
    EXPECT_TRUE(state.loaded_case.has_value());
    EXPECT_TRUE(state.sacm_package.has_value());
}

TEST(AppStateTest, FailedProjectSacmOpenPreservesCurrentDocument) {
    TempDir temp(MakeTempDir());
    core::AppState state;
    ASSERT_TRUE(state.create_empty_project("Project", temp.path.string())) << state.status_message;
    ASSERT_TRUE(state.current_project.has_value());

    const core::ProjectFileEntry* main_entry =
        FindProjectFileWithRole(state.current_project.value(), core::ProjectFileRole::SacmArgument);
    ASSERT_NE(main_entry, nullptr);
    const std::filesystem::path sacm_path = state.current_project->rootPath / main_entry->relativePath;

    ASSERT_TRUE(state.open_project_file(*main_entry)) << state.status_message;
    state.mark_dirty();

    core::ProjectFileEntry missing_entry;
    missing_entry.relativePath = std::filesystem::path("arguments") / "missing.sacm";
    missing_entry.role = core::ProjectFileRole::SacmArgument;

    ASSERT_FALSE(state.open_project_file(missing_entry));
    EXPECT_EQ(state.active_project_file_role, core::ProjectFileRole::SacmArgument);
    EXPECT_EQ(state.active_project_file_path, sacm_path);
    EXPECT_EQ(state.loaded_file_path, sacm_path);
    EXPECT_TRUE(state.has_unsaved_changes);
    EXPECT_TRUE(state.loaded_case.has_value());
    EXPECT_TRUE(state.sacm_package.has_value());
}

TEST(AppStateTest, SaveProjectKeepsSacmTargetAfterOpeningNonSacmFile) {
    TempDir temp(MakeTempDir());
    core::AppState state;
    ASSERT_TRUE(state.create_empty_project("Project", temp.path.string())) << state.status_message;
    ASSERT_TRUE(state.current_project.has_value());

    core::ProjectFileEntry evidence_entry;
    ASSERT_TRUE(state.create_project_evidence_register("evidence-register.af.json", &evidence_entry))
        << state.status_message;

    const core::ProjectFileEntry* main_entry =
        FindProjectFileWithRole(state.current_project.value(), core::ProjectFileRole::SacmArgument);
    ASSERT_NE(main_entry, nullptr);
    const std::filesystem::path sacm_path = state.current_project->rootPath / main_entry->relativePath;
    const std::filesystem::path evidence_path = state.current_project->rootPath / evidence_entry.relativePath;
    const std::string evidence_before = ReadTextFile(evidence_path);

    ASSERT_TRUE(state.open_project_file(*main_entry)) << state.status_message;
    ASSERT_TRUE(state.sacm_package.has_value());
    state.sacm_package->name = "Updated Project";
    state.mark_dirty();

    ASSERT_TRUE(state.open_project_file(evidence_entry)) << state.status_message;
    EXPECT_EQ(state.active_project_file_role, core::ProjectFileRole::EvidenceRegister);
    EXPECT_EQ(state.active_project_file_path, evidence_path);

    ASSERT_TRUE(state.save_project()) << state.status_message;
    EXPECT_EQ(ReadTextFile(evidence_path), evidence_before);
    EXPECT_NE(ReadTextFile(sacm_path).find("Updated Project"), std::string::npos);
    EXPECT_EQ(state.loaded_file_path, sacm_path);
    EXPECT_FALSE(state.has_unsaved_changes);
}

TEST(AppStateTest, LoadFileKeepsVisibleTerminologyContextOnCanvas) {
    TempDir temp(MakeTempDir());
    const std::filesystem::path sacm_path = temp.path / "case.sacm";
    std::ofstream(sacm_path) << R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <terminologyPackage id="TP" name="Glossary">
    <term id="TERM_ODD" name="Operational Design Domain" value="ODD">
      <description><content lang="en">The intended operating conditions.</content></description>
    </term>
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
    const parser::SacmElement* visible_context = FindElement(state.loaded_case.value(), "TERM_VISIBLE");
    ASSERT_NE(visible_context, nullptr);
    EXPECT_EQ(visible_context->name, "ODD: Operational Design Domain");
    EXPECT_EQ(visible_context->description, "The intended operating conditions.");
    EXPECT_NE(FindElement(state.loaded_case.value(), "AC_VISIBLE"), nullptr);

    core::AssuranceTree tree = core::AssuranceTree::Build(state.loaded_case.value());
    ASSERT_NE(tree.root, nullptr);
    ASSERT_EQ(tree.root->group2_attachments.size(), 1u);
    EXPECT_EQ(tree.root->group2_attachments.front()->id, "TERM_VISIBLE");
    EXPECT_EQ(tree.root->group2_attachments.front()->role, core::NodeRole::Context);
}

TEST(AppStateTest, LoadFileKeepsBrokenVisibleTerminologyContextRepairable) {
    TempDir temp(MakeTempDir());
    const std::filesystem::path sacm_path = temp.path / "case.sacm";
    std::ofstream(sacm_path) << R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="G1" name="Goal" assertionDeclaration="asserted"/>
    <artifactReference id="TERM_BROKEN" name="Missing term" referencedArtifact="TERM_MISSING"/>
    <assertedContext id="AC_BROKEN" name="Broken terminology relation">
      <description><content lang="en">assurance-forge:visible-term-context</content></description>
      <source ref="TERM_BROKEN"/>
      <target ref="G1"/>
    </assertedContext>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    core::AppState state;
    ASSERT_TRUE(state.load_file(sacm_path.string())) << state.status_message;
    ASSERT_TRUE(state.loaded_case.has_value());
    ASSERT_TRUE(state.sacm_package.has_value());

    EXPECT_NE(FindElement(state.loaded_case.value(), "TERM_BROKEN"), nullptr);
    EXPECT_NE(FindElement(state.loaded_case.value(), "AC_BROKEN"), nullptr);
    std::vector<core::TerminologyContextReferenceIssue> issues =
        core::ValidateTerminologyContextReferences(state.sacm_package.value());
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues.front().kind, core::TerminologyContextReferenceIssueKind::MissingTerm);

    core::AssuranceTree tree = core::AssuranceTree::Build(state.loaded_case.value());
    ASSERT_NE(tree.root, nullptr);
    ASSERT_EQ(tree.root->group2_attachments.size(), 1u);
    EXPECT_EQ(tree.root->group2_attachments.front()->id, "TERM_BROKEN");
}
