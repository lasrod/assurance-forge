#include <gtest/gtest.h>

#include "core/project_service.h"
#include "parser/xml_parser.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(std::filesystem::path p) : path(std::move(p)) {}
    ~TempDir() { std::filesystem::remove_all(path); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

std::filesystem::path MakeTempParent() {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 ("assurance_forge_project_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

bool ContainsFileWithRole(const core::AssuranceProject& project,
                          const char* relative_path,
                          core::ProjectFileRole role) {
    for (const auto& file : project.files) {
        if (file.relativePath.generic_string() == relative_path && file.role == role) return true;
    }
    return false;
}

std::string ReportSummary(const core::ProjectLoadReport& report) {
    std::string summary;
    for (const auto& step : report.steps) {
        summary += step.label + ":" + std::to_string(static_cast<int>(step.status)) + ":" + step.message + "\n";
    }
    for (const auto& warning : report.warnings) {
        summary += "warning:" + warning + "\n";
    }
    return summary;
}

}  // namespace

TEST(ProjectServiceTest, CreateEmptyProjectCreatesRequiredStructureAndManifest) {
    TempDir tmp(MakeTempParent());
    auto& parent = tmp.path;
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;

    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", parent, project, report, error)) << error;

    auto root = parent / "MySafetyCase";
    EXPECT_EQ(project.rootPath, root);
    EXPECT_TRUE(std::filesystem::exists(root / "af.proj"));
    EXPECT_TRUE(std::filesystem::is_directory(root / "arguments"));
    EXPECT_TRUE(std::filesystem::is_directory(root / "registers"));
    EXPECT_TRUE(std::filesystem::is_directory(root / "conformance"));
    EXPECT_TRUE(std::filesystem::is_directory(root / "exports"));
    EXPECT_TRUE(std::filesystem::is_directory(root / ".af" / "cache"));
    EXPECT_TRUE(std::filesystem::is_directory(root / ".af" / "backups"));
    EXPECT_TRUE(std::filesystem::is_directory(root / ".af" / "snapshots"));
    EXPECT_TRUE(std::filesystem::is_directory(root / ".af" / "history"));
    EXPECT_TRUE(std::filesystem::exists(root / "arguments" / "main.sacm"));
    EXPECT_TRUE(parser::parse_sacm_xml((root / "arguments" / "main.sacm").string()).success);
    EXPECT_TRUE(ContainsFileWithRole(project, "arguments/main.sacm", core::ProjectFileRole::SacmArgument));
    EXPECT_FALSE(report.steps.empty());
    EXPECT_FALSE(report.showPopup);

    core::AssuranceProject reopened;
    core::ProjectLoadReport open_report;
    ASSERT_TRUE(core::ProjectService::OpenProject(root, reopened, open_report, error)) << error;
    EXPECT_FALSE(open_report.showPopup) << ReportSummary(open_report);
}

TEST(ProjectServiceTest, AddProjectFilesNormalizesNamesAndTracksManifestEntries) {
    TempDir tmp(MakeTempParent());
    auto& parent = tmp.path;
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    core::ProjectFileEntry entry;
    std::string error;

    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", parent, project, report, error)) << error;
    ASSERT_TRUE(core::ProjectService::AddSacmFile(project, "safety-core", entry, error)) << error;
    EXPECT_EQ(entry.relativePath.generic_string(), "arguments/safety-core.sacm");
    EXPECT_TRUE(std::filesystem::exists(project.rootPath / entry.relativePath));
    EXPECT_TRUE(parser::parse_sacm_xml((project.rootPath / entry.relativePath).string()).success);

    ASSERT_TRUE(core::ProjectService::AddEvidenceRegister(project, "", entry, error)) << error;
    EXPECT_EQ(entry.relativePath.generic_string(), "registers/evidence-register.af.json");

    ASSERT_TRUE(core::ProjectService::AddJ3377CaeRegister(project, "j3377-cae-register.af.json", entry, error)) << error;
    EXPECT_EQ(entry.relativePath.generic_string(), "registers/j3377-cae-register.af.json");

    core::AssuranceProject reopened;
    core::ProjectLoadReport open_report;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, reopened, open_report, error)) << error;
    EXPECT_TRUE(ContainsFileWithRole(reopened, "arguments/main.sacm", core::ProjectFileRole::SacmArgument));
    EXPECT_TRUE(ContainsFileWithRole(reopened, "arguments/safety-core.sacm", core::ProjectFileRole::SacmArgument));
    EXPECT_TRUE(ContainsFileWithRole(reopened, "registers/evidence-register.af.json", core::ProjectFileRole::EvidenceRegister));
    EXPECT_TRUE(ContainsFileWithRole(reopened, "registers/j3377-cae-register.af.json", core::ProjectFileRole::J3377CaeRegister));
}

TEST(ProjectServiceTest, OpenProjectReportsExternallyModifiedAndMissingFiles) {
    TempDir tmp(MakeTempParent());
    auto& parent = tmp.path;
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    core::ProjectFileEntry entry;
    std::string error;

    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", parent, project, report, error)) << error;
    ASSERT_TRUE(core::ProjectService::AddEvidenceRegister(project, "", entry, error)) << error;

    {
        std::ofstream file(project.rootPath / entry.relativePath, std::ios::app | std::ios::binary);
        file << "\n";
    }

    core::AssuranceProject reopened;
    core::ProjectLoadReport open_report;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, reopened, open_report, error)) << error;
    ASSERT_EQ(reopened.files.size(), 2u);
    auto evidence_it = std::find_if(reopened.files.begin(), reopened.files.end(),
        [](const core::ProjectFileEntry& file) {
            return file.role == core::ProjectFileRole::EvidenceRegister;
        });
    ASSERT_NE(evidence_it, reopened.files.end());
    EXPECT_EQ(evidence_it->state, core::ProjectFileState::ModifiedOutsideAssuranceForge);
    EXPECT_FALSE(open_report.warnings.empty());
    EXPECT_TRUE(open_report.showPopup);

    std::filesystem::remove(project.rootPath / entry.relativePath);
    core::ProjectLoadReport missing_report = core::ProjectService::RefreshFileStatus(reopened);
    evidence_it = std::find_if(reopened.files.begin(), reopened.files.end(),
        [](const core::ProjectFileEntry& file) {
            return file.role == core::ProjectFileRole::EvidenceRegister;
        });
    ASSERT_NE(evidence_it, reopened.files.end());
    EXPECT_EQ(evidence_it->state, core::ProjectFileState::Missing);
    EXPECT_TRUE(missing_report.has_failures());
    EXPECT_TRUE(missing_report.showPopup);
}