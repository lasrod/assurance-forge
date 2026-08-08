#include "core/project_service.h"

#include "core/project_file_io.h"
#include "parser/xml_parser.h"
#include "sacm/io/xmi.h"
#include "sacm/metadata/namespaces.h"
#include "sacm/model/document.h"
#include "sacm/validation/validate.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <gtest/gtest.h>

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(std::filesystem::path p) : path(std::move(p)) {}
    ~TempDir() {
        std::filesystem::remove_all(path);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

std::filesystem::path MakeTempParent() {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("assurance_forge_project_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

bool ContainsFileWithRole(const core::AssuranceProject& project,
                          const char* relative_path,
                          core::ProjectFileRole role) {
    for (const auto& file : project.files) {
        if (file.relativePath.generic_string() == relative_path && file.role == role)
            return true;
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

} // namespace

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
    EXPECT_TRUE(std::filesystem::is_directory(root / "reviews"));
    EXPECT_TRUE(std::filesystem::is_directory(root / "reviews" / "proposals"));
    EXPECT_TRUE(std::filesystem::is_directory(root / "conformance"));
    EXPECT_TRUE(std::filesystem::is_directory(root / "exports"));
    EXPECT_TRUE(std::filesystem::is_directory(root / ".af" / "cache"));
    EXPECT_TRUE(std::filesystem::is_directory(root / ".af" / "backups"));
    EXPECT_TRUE(std::filesystem::is_directory(root / ".af" / "snapshots"));
    EXPECT_TRUE(std::filesystem::is_directory(root / ".af" / "history"));
    EXPECT_TRUE(std::filesystem::exists(root / "arguments" / "main.sacm"));
    EXPECT_TRUE(std::filesystem::exists(root / "reviews" / "review-items.af.json"));
    EXPECT_TRUE(parser::parse_sacm_xml((root / "arguments" / "main.sacm").string()).has_value());
    EXPECT_TRUE(ContainsFileWithRole(project, "arguments/main.sacm", core::ProjectFileRole::SacmArgument));
    EXPECT_TRUE(ContainsFileWithRole(project, "reviews/review-items.af.json", core::ProjectFileRole::ReviewItems));
    EXPECT_FALSE(report.steps.empty());
    EXPECT_FALSE(report.showPopup);

    core::AssuranceProject reopened;
    core::ProjectLoadReport open_report;
    ASSERT_TRUE(core::ProjectService::OpenProject(root, reopened, open_report, error)) << error;
    EXPECT_FALSE(open_report.showPopup) << ReportSummary(open_report);
}

// The seed a new project starts from is a real SACM 2.3 document, not a
// hand-written approximation. It used to be a literal in the SACM 2.2
// namespace using `id=` instead of `xmi:id`: the tolerant reader accepted it,
// so nothing looked wrong, but every new project began as a document no strict
// consumer would take and the first save quietly replaced it with a different
// dialect. Asserted through a STRICT load, which is what makes the difference
// visible -- a tolerant load passes either way.
TEST(ProjectServiceTest, SACM23_LIB_002_NewProjectSeedIsStrictSacm23Xmi) {
    TempDir tmp(MakeTempParent());
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", tmp.path, project, report, error)) << error;

    const std::filesystem::path seed = project.rootPath / "arguments" / "main.sacm";
    const sacm::io::LoadResult strict =
        sacm::io::load_xmi_file(seed, sacm::io::LoadOptions{.mode = sacm::io::Mode::Strict});
    ASSERT_TRUE(strict.ok) << "the seed a new project starts from is not strict SACM 2.3: "
                           << (strict.diagnostics.empty() ? "" : strict.diagnostics.front().message);
    EXPECT_EQ(strict.source_version, sacm::metadata::namespaces::StandardVersion::V2_3);
    EXPECT_TRUE(sacm::validation::validate(*strict.document).empty());

    // The project name reaches the document, and the seed carries the one goal
    // the app expects to render.
    ASSERT_EQ(strict.document->roots().size(), 1u);
    EXPECT_EQ(strict.document->roots().front()->name().content, "MySafetyCase");
    int claims = 0;
    strict.document->for_each_element([&](const sacm::model::SACMElement& element) {
        if (element.kind() == sacm::metadata::ElementKind::Claim) {
            ++claims;
        }
    });
    EXPECT_EQ(claims, 1);

    // Round-tripping the seed through a save must not rewrite it: a new file
    // and a saved file are the same dialect.
    const sacm::io::SaveResult resaved = sacm::io::save_xmi_string(*strict.document);
    ASSERT_TRUE(resaved.ok);
    std::ifstream stream(seed, std::ios::binary);
    std::ostringstream on_disk;
    on_disk << stream.rdbuf();
    EXPECT_EQ(on_disk.str(), resaved.xml)
        << "the seed on disk is not what the writer would produce for the same model, so the "
           "first save silently rewrites a brand-new file";
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
    EXPECT_TRUE(parser::parse_sacm_xml((project.rootPath / entry.relativePath).string()).has_value());

    ASSERT_TRUE(core::ProjectService::AddEvidenceRegister(project, "", entry, error)) << error;
    EXPECT_EQ(entry.relativePath.generic_string(), "registers/evidence-register.af.json");

    ASSERT_TRUE(core::ProjectService::AddJ3377CaeRegister(project, "j3377-cae-register.af.json", entry, error))
        << error;
    EXPECT_EQ(entry.relativePath.generic_string(), "registers/j3377-cae-register.af.json");

    core::AssuranceProject reopened;
    core::ProjectLoadReport open_report;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, reopened, open_report, error)) << error;
    EXPECT_TRUE(ContainsFileWithRole(reopened, "arguments/main.sacm", core::ProjectFileRole::SacmArgument));
    EXPECT_TRUE(ContainsFileWithRole(reopened, "arguments/safety-core.sacm", core::ProjectFileRole::SacmArgument));
    EXPECT_TRUE(ContainsFileWithRole(reopened, "reviews/review-items.af.json", core::ProjectFileRole::ReviewItems));
    EXPECT_TRUE(
        ContainsFileWithRole(reopened, "registers/evidence-register.af.json", core::ProjectFileRole::EvidenceRegister));
    EXPECT_TRUE(ContainsFileWithRole(
        reopened, "registers/j3377-cae-register.af.json", core::ProjectFileRole::J3377CaeRegister));
}

TEST(ProjectServiceTest, AddAndRemoveReviewProposalTracksManifestEntry) {
    TempDir tmp(MakeTempParent());
    auto& parent = tmp.path;
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    core::ProjectFileEntry entry;
    std::string error;

    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", parent, project, report, error)) << error;

    const std::string proposal_json = "{\n"
                                      "  \"schema\": \"assurance-forge.review-proposal.v1\",\n"
                                      "  \"id\": \"proposal-0001\",\n"
                                      "  \"anchor_element_id\": \"G1\",\n"
                                      "  \"operations\": []\n"
                                      "}\n";
    ASSERT_TRUE(core::ProjectService::AddReviewProposalFile(project, "proposal-0001", proposal_json, entry, error))
        << error;
    EXPECT_EQ(entry.relativePath.generic_string(), "reviews/proposals/proposal-0001.afpatch.json");
    EXPECT_EQ(entry.role, core::ProjectFileRole::ReviewProposal);
    EXPECT_TRUE(std::filesystem::exists(project.rootPath / entry.relativePath));
    EXPECT_TRUE(ContainsFileWithRole(
        project, "reviews/proposals/proposal-0001.afpatch.json", core::ProjectFileRole::ReviewProposal));

    ASSERT_TRUE(core::ProjectService::RemoveTrackedFile(project, entry.relativePath, true, error)) << error;
    EXPECT_FALSE(std::filesystem::exists(project.rootPath / entry.relativePath));
    EXPECT_FALSE(ContainsFileWithRole(
        project, "reviews/proposals/proposal-0001.afpatch.json", core::ProjectFileRole::ReviewProposal));
}

TEST(ProjectServiceTest, TrackExistingExportedReportAddsExportsManifestEntry) {
    TempDir tmp(MakeTempParent());
    auto& parent = tmp.path;
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    core::ProjectFileEntry entry;
    std::string error;

    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", parent, project, report, error)) << error;
    const std::filesystem::path relative_path = std::filesystem::path("exports") / "main_gsn.svg";
    {
        std::ofstream file(project.rootPath / relative_path, std::ios::binary);
        file << "<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>\n";
    }

    ASSERT_TRUE(core::ProjectService::TrackExistingFile(
        project, relative_path, core::ProjectFileRole::ExportedReport, entry, error))
        << error;
    EXPECT_EQ(entry.relativePath.generic_string(), "exports/main_gsn.svg");
    EXPECT_EQ(entry.role, core::ProjectFileRole::ExportedReport);
    EXPECT_TRUE(ContainsFileWithRole(project, "exports/main_gsn.svg", core::ProjectFileRole::ExportedReport));

    core::AssuranceProject reopened;
    core::ProjectLoadReport open_report;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, reopened, open_report, error)) << error;
    EXPECT_TRUE(ContainsFileWithRole(reopened, "exports/main_gsn.svg", core::ProjectFileRole::ExportedReport));
}

TEST(ProjectServiceTest, SaveReviewProposalFileRefreshesTrackedHash) {
    TempDir tmp(MakeTempParent());
    auto& parent = tmp.path;
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    core::ProjectFileEntry entry;
    std::string error;

    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", parent, project, report, error)) << error;

    const std::string first_json = "{\n"
                                   "  \"schema\": \"assurance-forge.review-proposal.v1\",\n"
                                   "  \"id\": \"proposal-0001\",\n"
                                   "  \"anchor_element_id\": \"G1\",\n"
                                   "  \"operations\": []\n"
                                   "}\n";
    ASSERT_TRUE(core::ProjectService::SaveReviewProposalFile(project, "proposal-0001", first_json, entry, error))
        << error;
    const std::string first_hash = entry.rawHash;

    const std::string second_json =
        "{\n"
        "  \"schema\": \"assurance-forge.review-proposal.v1\",\n"
        "  \"id\": \"proposal-0001\",\n"
        "  \"anchor_element_id\": \"G1\",\n"
        "  \"operations\": [\n"
        "    {\"type\": \"CreateClaim\", \"create_ref\": \"$new_claim_1\", \"text\": \"Draft claim\"}\n"
        "  ]\n"
        "}\n";
    ASSERT_TRUE(core::ProjectService::SaveReviewProposalFile(project, "proposal-0001", second_json, entry, error))
        << error;
    EXPECT_EQ(entry.relativePath.generic_string(), "reviews/proposals/proposal-0001.afpatch.json");
    EXPECT_NE(entry.rawHash, first_hash);

    core::ProjectLoadReport refresh_report = core::ProjectService::RefreshFileStatus(project);
    EXPECT_FALSE(refresh_report.showPopup) << ReportSummary(refresh_report);
    EXPECT_TRUE(refresh_report.warnings.empty()) << ReportSummary(refresh_report);

    {
        std::ofstream file(project.rootPath / entry.relativePath, std::ios::app | std::ios::binary);
        file << "\n";
    }

    refresh_report = core::ProjectService::RefreshFileStatus(project);
    EXPECT_FALSE(refresh_report.warnings.empty()) << ReportSummary(refresh_report);
}

TEST(ProjectServiceTest, SaveReviewItemsFileRefreshesTrackedHash) {
    TempDir tmp(MakeTempParent());
    auto& parent = tmp.path;
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    core::ProjectFileEntry entry;
    std::string error;

    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", parent, project, report, error)) << error;

    const std::string first_json = "{\n  \"schema\": \"assurance-forge.review-items.v1\",\n  \"items\": []\n}\n";
    ASSERT_TRUE(core::ProjectService::SaveReviewItemsFile(project, "review-items.af.json", first_json, entry, error))
        << error;
    const std::string first_hash = entry.rawHash;

    const std::string second_json = "{\n"
                                    "  \"schema\": \"assurance-forge.review-items.v1\",\n"
                                    "  \"items\": [\n"
                                    "    {\"id\": \"review-1\", \"element_id\": \"G1\", \"title\": \"Review comment\", "
                                    "\"message\": \"Check this\", \"status\": \"open\"}\n"
                                    "  ]\n"
                                    "}\n";
    ASSERT_TRUE(core::ProjectService::SaveReviewItemsFile(project, "review-items.af.json", second_json, entry, error))
        << error;
    EXPECT_EQ(entry.relativePath.generic_string(), "reviews/review-items.af.json");
    EXPECT_EQ(entry.role, core::ProjectFileRole::ReviewItems);
    EXPECT_NE(entry.rawHash, first_hash);

    core::ProjectLoadReport refresh_report = core::ProjectService::RefreshFileStatus(project);
    EXPECT_FALSE(refresh_report.showPopup) << ReportSummary(refresh_report);
    EXPECT_TRUE(refresh_report.warnings.empty()) << ReportSummary(refresh_report);
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
    ASSERT_EQ(reopened.files.size(), 3u);
    auto evidence_it =
        std::find_if(reopened.files.begin(), reopened.files.end(), [](const core::ProjectFileEntry& file) {
            return file.role == core::ProjectFileRole::EvidenceRegister;
        });
    ASSERT_NE(evidence_it, reopened.files.end());
    EXPECT_EQ(evidence_it->state, core::ProjectFileState::ModifiedOutsideAssuranceForge);
    EXPECT_FALSE(open_report.warnings.empty());
    EXPECT_TRUE(open_report.showPopup);

    std::filesystem::remove(project.rootPath / entry.relativePath);
    core::ProjectLoadReport missing_report = core::ProjectService::RefreshFileStatus(reopened);
    evidence_it = std::find_if(reopened.files.begin(), reopened.files.end(), [](const core::ProjectFileEntry& file) {
        return file.role == core::ProjectFileRole::EvidenceRegister;
    });
    ASSERT_NE(evidence_it, reopened.files.end());
    EXPECT_EQ(evidence_it->state, core::ProjectFileState::Missing);
    EXPECT_TRUE(missing_report.has_failures());
    EXPECT_TRUE(missing_report.showPopup);
}
// `ReadFileBytes` had no test. It measures the file with `tellg`, sizes a buffer
// to that, and reads. The read now has to deliver every byte it asked for, so
// these pin the sizes that must keep succeeding -- most of all the empty file,
// where no read happens at all and `gcount()` has nothing to report.
//
// What they deliberately do not cover is the case the check exists for: a file
// that shrinks between `tellg` and `read`. That needs the truncation to land
// inside ReadFileBytes, between two adjacent statements, and nothing outside the
// function can place it there. The stream states it produces were confirmed
// separately -- a `read` of 20 bytes from a 10-byte file reports
// `good=0 eof=1 fail=1 gcount=10`, so the old `!good() && !eof()` guard returned
// success with the buffer's tail left as zeros.
TEST(ProjectFileIoTest, ReadFileBytesReturnsEveryByteOrSaysWhyNot) {
    TempDir tmp(MakeTempParent());

    const std::filesystem::path empty_path = tmp.path / "empty.bin";
    { std::ofstream out(empty_path, std::ios::binary); }
    const std::expected<std::vector<unsigned char>, std::string> empty = core::ReadFileBytes(empty_path);
    ASSERT_TRUE(empty.has_value()) << empty.error();
    EXPECT_TRUE(empty->empty());

    const std::filesystem::path sized_path = tmp.path / "sized.bin";
    const std::string content(4096, 'x');
    {
        std::ofstream out(sized_path, std::ios::binary);
        out << content;
    }
    const std::expected<std::vector<unsigned char>, std::string> sized = core::ReadFileBytes(sized_path);
    ASSERT_TRUE(sized.has_value()) << sized.error();
    ASSERT_EQ(sized->size(), content.size());
    EXPECT_EQ(std::string(sized->begin(), sized->end()), content);

    // A byte that is not text, in a stream opened binary: a translated read
    // would come up short and is now an error rather than a zero-padded buffer.
    const std::filesystem::path binary_path = tmp.path / "crlf.bin";
    const std::string raw("a\r\nb\r\n\x1a"
                          "trailing",
                          15);
    {
        std::ofstream out(binary_path, std::ios::binary);
        out.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    }
    const std::expected<std::vector<unsigned char>, std::string> binary = core::ReadFileBytes(binary_path);
    ASSERT_TRUE(binary.has_value()) << binary.error();
    EXPECT_EQ(binary->size(), raw.size());
    EXPECT_EQ(std::string(binary->begin(), binary->end()), raw);

    const std::expected<std::vector<unsigned char>, std::string> missing =
        core::ReadFileBytes(tmp.path / "not-here.bin");
    EXPECT_FALSE(missing.has_value());
}
