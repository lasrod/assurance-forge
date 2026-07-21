#include "core/app_state.h"
#include "core/assurance_tree.h"
#include "core/terminology_package_service.h"
#include "core/acp/assurance_claim_point.h"
#include "sacm_adapter/library_load.h"

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

// When the library cannot read a file, load_file falls back to the legacy
// parser and must keep that visible in the status message -- otherwise a
// library gap goes silent whenever the save-support parse then succeeds. A
// DOCTYPE triggers the library's XXE rejection while the legacy parser still
// reads the file.
TEST(AppStateTest, LoadFileFallbackToLegacyParserStaysVisibleInStatus) {
    TempDir temp(MakeTempDir());
    const std::filesystem::path sacm_path = temp.path / "doctype.sacm";
    std::ofstream(sacm_path) << R"(<?xml version="1.0"?>
<!DOCTYPE AssuranceCasePackage>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" id="D" name="Doctype Case">
  <argumentPackage id="AP" name="AP">
    <claim id="G1" name="Goal"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    core::AppState state;
    ASSERT_TRUE(state.load_file(sacm_path.string())) << state.status_message;

    // The library rejected it (XXE), so the fallback loaded it and no library
    // document is retained.
    EXPECT_EQ(state.library_document, nullptr);
    ASSERT_TRUE(state.loaded_case.has_value());
    EXPECT_NE(FindElement(state.loaded_case.value(), "G1"), nullptr);
    // The fallback is not silent, even though save-support parsing succeeded.
    EXPECT_NE(state.status_message.find("legacy parser"), std::string::npos)
        << "fallback note was dropped: " << state.status_message;
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

// Regression (Phase 9 Stage 6, #208): saved files are library XMI, which the
// legacy sacm::parse_sacm reads as a near-empty package. Deriving sacm_package
// from the library instead must reconstruct it fully -- including vendor tags
// (ACP) -- or terminology display, ACP, and re-save (data loss) break.
TEST(AppStateTest, LoadFileFromLibraryXmiPopulatesSacmPackageWithTags) {
    TempDir temp(MakeTempDir());

    // Save fixture_acp_parity as library XMI (the format Stage 6 writes).
    const std::filesystem::path src =
        std::filesystem::path(AF_REPO_ROOT) / "tests" / "data" / "fixture_acp_parity.sacm.xml";
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(src);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::SaveOutcome saved = sacm_adapter::save_document(*loaded.document);
    ASSERT_TRUE(saved.ok);
    const std::filesystem::path xmi = temp.path / "reloaded.sacm";
    {
        std::ofstream out(xmi, std::ios::binary);
        out << saved.xml;
    }

    // Reload it through the application's load path.
    core::AppState state;
    ASSERT_TRUE(state.load_file(xmi.string())) << state.status_message;

    ASSERT_TRUE(state.sacm_package.has_value());
    ASSERT_FALSE(state.sacm_package->argumentPackages.empty());
    std::size_t claims = 0;
    std::size_t tags = 0;
    for (const sacm::ArgumentPackage& ap : state.sacm_package->argumentPackages) {
        claims += ap.claims.size();
        for (const sacm::Claim& claim : ap.claims) {
            tags += claim.taggedValues.size();
        }
        for (const sacm::AssertedInference& inference : ap.assertedInferences) {
            tags += inference.taggedValues.size();
        }
    }
    EXPECT_EQ(claims, 2u) << "sacm_package lost its claims (near-empty regression)";
    EXPECT_GT(tags, 0u) << "ACP tags dropped from the library-derived sacm_package";

    // The ACPs also survive in the rendered model.
    ASSERT_TRUE(state.loaded_case.has_value());
    EXPECT_EQ(state.loaded_case->acps.size(), 2u);
}

// Regression: ACP confidence arguments live in their own argument package. The
// POD projection flattens every package into one element list, so a library-XMI
// reload must NOT collapse multiple argument packages into one (which would drop
// the confidence package's identity and its purpose tag). A two-package document
// round-tripped through save + reload must come back as two argument packages,
// with the confidence marker intact.
TEST(AppStateTest, LoadFileFromLibraryXmiPreservesMultipleArgumentPackages) {
    TempDir temp(MakeTempDir());
    const std::filesystem::path source_path = temp.path / "two_packages.sacm";
    std::ofstream(source_path) << R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" id="CASE" name="Case">
  <argumentPackage id="MAIN" name="Main Argument">
    <claim id="G1" name="Top Goal" content="The system is acceptably safe."/>
  </argumentPackage>
  <argumentPackage id="CONF" name="Confidence Argument">
    <taggedValue id="TV1" key="assuranceForge.argumentPackage.purpose" value="confidence"/>
    <claim id="CG1" name="Confidence Goal" content="Confidence is sufficient."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    // Load through the library, then save as library XMI (the Stage 6 format).
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(source_path);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::SaveOutcome saved = sacm_adapter::save_document(*loaded.document);
    ASSERT_TRUE(saved.ok);
    const std::filesystem::path xmi = temp.path / "reloaded.sacm";
    {
        std::ofstream out(xmi, std::ios::binary);
        out << saved.xml;
    }

    core::AppState state;
    ASSERT_TRUE(state.load_file(xmi.string())) << state.status_message;
    ASSERT_TRUE(state.sacm_package.has_value());

    ASSERT_EQ(state.sacm_package->argumentPackages.size(), 2u)
        << "argument packages collapsed on reload";
    const sacm::ArgumentPackage* main_pkg = nullptr;
    const sacm::ArgumentPackage* conf_pkg = nullptr;
    for (const sacm::ArgumentPackage& ap : state.sacm_package->argumentPackages) {
        if (ap.id == "MAIN") main_pkg = &ap;
        if (ap.id == "CONF") conf_pkg = &ap;
    }
    ASSERT_NE(main_pkg, nullptr) << "MAIN argument package lost";
    ASSERT_NE(conf_pkg, nullptr) << "CONF argument package lost";
    EXPECT_EQ(main_pkg->claims.size(), 1u);
    EXPECT_EQ(conf_pkg->claims.size(), 1u);
    EXPECT_TRUE(core::acp::IsConfidenceArgumentPackage(*conf_pkg))
        << "confidence purpose tag dropped from its package on reload";
    EXPECT_FALSE(core::acp::IsConfidenceArgumentPackage(*main_pkg));
}
