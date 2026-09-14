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
#include <nlohmann/json.hpp>

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

// The rule the create dialog asks while the user types, and the one the create
// itself refuses on. One rule with two callers: a dialog that offers a Create
// the create would reject is how "nothing happens when I press it" is built.
TEST(ProjectServiceTest, TheCreateObstacleAndTheCreateItselfAgree) {
    TempDir tmp(MakeTempParent());
    const std::filesystem::path& parent = tmp.path;

    EXPECT_EQ(core::ProjectService::FindCreateProjectObstacle("MySafetyCase", parent),
              core::CreateProjectObstacle::None);
    EXPECT_EQ(core::ProjectService::FindCreateProjectObstacle("   ", parent),
              core::CreateProjectObstacle::NameRequired);
    EXPECT_EQ(core::ProjectService::FindCreateProjectObstacle("MySafetyCase", {}),
              core::CreateProjectObstacle::LocationRequired);

    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", parent, project, report, error)) << error;

    // The reported case: the same name a second time. The obstacle now says so,
    // which is what lets the dialog say so before the button is pressed...
    EXPECT_EQ(core::ProjectService::FindCreateProjectObstacle("MySafetyCase", parent),
              core::CreateProjectObstacle::FolderExists);
    // ...and the surrounding whitespace a user leaves behind must not hide it,
    // since the create trims before joining the path too.
    EXPECT_EQ(core::ProjectService::FindCreateProjectObstacle("  MySafetyCase  ", parent),
              core::CreateProjectObstacle::FolderExists);

    // ...and the create still refuses, naming the folder rather than failing mute.
    core::AssuranceProject second;
    std::string second_error;
    EXPECT_FALSE(core::ProjectService::CreateEmptyProject("MySafetyCase", parent, second, report, second_error));
    EXPECT_NE(second_error.find("already exists"), std::string::npos) << second_error;

    // A plain file in the way is an obstacle for the same reason a folder is:
    // the create cannot make a directory there either.
    std::ofstream(parent / "TakenByAFile") << "not a project";
    EXPECT_EQ(core::ProjectService::FindCreateProjectObstacle("TakenByAFile", parent),
              core::CreateProjectObstacle::FolderExists);
}

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

namespace {

void AppendNewline(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::app | std::ios::binary);
    file << "\n";
}

core::ProjectFileState EvidenceRegisterState(const core::AssuranceProject& project) {
    for (const core::ProjectFileEntry& file : project.files) {
        if (file.role == core::ProjectFileRole::EvidenceRegister)
            return file.state;
    }
    return core::ProjectFileState::Missing;
}

} // namespace

// #402: the "modified outside Assurance Forge" warning used to repeat on every
// open until something saved the project. Acknowledged once, the same change is
// not reported again -- and acknowledging does not rewrite af.proj, whose
// recorded hash is the evidence that the file was edited outside the tool.
TEST(ProjectServiceTest, AnAcknowledgedExternalChangeIsNotReportedAgainAndTheManifestKeepsItsHash) {
    TempDir tmp(MakeTempParent());
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    core::ProjectFileEntry entry;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("Acknowledged", tmp.path, project, report, error)) << error;
    ASSERT_TRUE(core::ProjectService::AddEvidenceRegister(project, "", entry, error)) << error;
    AppendNewline(project.rootPath / entry.relativePath);

    core::AssuranceProject opened;
    core::ProjectLoadReport first;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, opened, first, error)) << error;
    ASSERT_EQ(first.externalChanges.size(), 1u);
    EXPECT_FALSE(first.externalChanges.front().acknowledged);
    EXPECT_NE(first.externalChanges.front().recordedRawHash, first.externalChanges.front().observedRawHash);
    EXPECT_EQ(first.warnings.size(), 1u);
    EXPECT_TRUE(first.showPopup);

    const std::filesystem::path manifest = core::ProjectService::ManifestPath(opened);
    const std::string manifest_before = core::ReadTextFile(manifest).value();
    ASSERT_TRUE(core::ProjectService::AcknowledgeExternalChanges(opened, first.externalChanges, error)) << error;
    EXPECT_EQ(core::ReadTextFile(manifest).value(), manifest_before) << "acknowledging must not rewrite af.proj";
    EXPECT_TRUE(std::filesystem::exists(project.rootPath / ".af" / ".gitignore"));

    core::AssuranceProject reopened;
    core::ProjectLoadReport second;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, reopened, second, error)) << error;
    EXPECT_TRUE(second.warnings.empty());
    EXPECT_FALSE(second.showPopup);
    ASSERT_EQ(second.externalChanges.size(), 1u);
    EXPECT_TRUE(second.externalChanges.front().acknowledged);
    // The warning is withheld; the fact is not.
    EXPECT_EQ(EvidenceRegisterState(reopened), core::ProjectFileState::ModifiedOutsideAssuranceForge);

    // A further edit is a different change, and is reported.
    AppendNewline(project.rootPath / entry.relativePath);
    core::AssuranceProject edited_again;
    core::ProjectLoadReport third;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, edited_again, third, error)) << error;
    EXPECT_EQ(third.warnings.size(), 1u);
    EXPECT_TRUE(third.showPopup);
    ASSERT_EQ(third.externalChanges.size(), 1u);
    EXPECT_FALSE(third.externalChanges.front().acknowledged);
}

// Only the shape AcknowledgeExternalChanges writes acknowledges anything. Every
// other file -- unreadable, another format or version, an entry missing a field --
// leaves the change reported, as it was before acknowledgements existed. The first
// case is the control: the same entry in the written format does acknowledge, so
// each refusal below is about the shape and not about a hash that never matched.
TEST(ProjectServiceTest, AnAcknowledgementFileNotInTheWrittenFormatAcknowledgesNothing) {
    TempDir tmp(MakeTempParent());
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    core::ProjectFileEntry entry;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("Unreadable", tmp.path, project, report, error)) << error;
    ASSERT_TRUE(core::ProjectService::AddEvidenceRegister(project, "", entry, error)) << error;
    AppendNewline(project.rootPath / entry.relativePath);

    core::AssuranceProject opened;
    core::ProjectLoadReport first;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, opened, first, error)) << error;
    ASSERT_EQ(first.externalChanges.size(), 1u);
    const core::ExternalFileChange& change = first.externalChanges.front();
    const nlohmann::json valid{{"format", "assurance-forge.acknowledged-external-changes"},
                               {"version", 1},
                               {"changes",
                                nlohmann::json::array({{{"path", change.relativePath.generic_string()},
                                                        {"recorded_raw_hash", change.recordedRawHash},
                                                        {"observed_raw_hash", change.observedRawHash},
                                                        {"acknowledged_utc", "2026-09-15T00:00:00Z"}}})}};

    nlohmann::json no_format = valid;
    no_format.erase("format");
    nlohmann::json other_version = valid;
    other_version["version"] = 2;
    nlohmann::json no_recorded_hash = valid;
    no_recorded_hash["changes"][0].erase("recorded_raw_hash");
    nlohmann::json no_time = valid;
    no_time["changes"][0].erase("acknowledged_utc");
    nlohmann::json path_not_a_string = valid;
    path_not_a_string["changes"][0]["path"] = 5;

    struct Case {
        const char* label;
        std::string content;
        bool acknowledges;
    };
    const std::vector<Case> cases = {
        {"the written format (control)", valid.dump(), true},
        {"not JSON", "{ not json", false},
        {"no format", no_format.dump(), false},
        {"another version", other_version.dump(), false},
        {"an entry without its recorded hash", no_recorded_hash.dump(), false},
        {"an entry without its time", no_time.dump(), false},
        {"a path that is not a string", path_not_a_string.dump(), false},
    };
    std::filesystem::create_directories(project.rootPath / ".af");
    for (const Case& test_case : cases) {
        SCOPED_TRACE(test_case.label);
        std::ofstream(project.rootPath / ".af" / "acknowledged-external-changes.json", std::ios::binary)
            << test_case.content;
        core::AssuranceProject reopened;
        core::ProjectLoadReport open_report;
        ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, reopened, open_report, error)) << error;
        EXPECT_EQ(open_report.warnings.empty(), test_case.acknowledges);
        EXPECT_EQ(open_report.showPopup, !test_case.acknowledges);
    }
}

// Every acknowledged pair is kept, not the latest per file. A file changed to A
// and acknowledged, then to B and acknowledged, then back to A is back at a change
// already acknowledged.
TEST(ProjectServiceTest, AReturnToAnAcknowledgedChangeIsNotReportedAgain) {
    TempDir tmp(MakeTempParent());
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    core::ProjectFileEntry entry;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("Returned", tmp.path, project, report, error)) << error;
    ASSERT_TRUE(core::ProjectService::AddEvidenceRegister(project, "", entry, error)) << error;
    const std::filesystem::path file = project.rootPath / entry.relativePath;

    AppendNewline(file);
    core::AssuranceProject at_a;
    core::ProjectLoadReport report_a;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, at_a, report_a, error)) << error;
    ASSERT_EQ(report_a.warnings.size(), 1u);
    ASSERT_TRUE(core::ProjectService::AcknowledgeExternalChanges(at_a, report_a.externalChanges, error)) << error;

    AppendNewline(file);
    core::AssuranceProject at_b;
    core::ProjectLoadReport report_b;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, at_b, report_b, error)) << error;
    ASSERT_EQ(report_b.warnings.size(), 1u);
    ASSERT_TRUE(core::ProjectService::AcknowledgeExternalChanges(at_b, report_b.externalChanges, error)) << error;

    // Back to A: drop the newline B added.
    std::filesystem::resize_file(file, std::filesystem::file_size(file) - 1);
    core::AssuranceProject back_at_a;
    core::ProjectLoadReport report_back;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, back_at_a, report_back, error)) << error;
    EXPECT_TRUE(report_back.warnings.empty());
    ASSERT_EQ(report_back.externalChanges.size(), 1u);
    EXPECT_TRUE(report_back.externalChanges.front().acknowledged);
}

// An acknowledged change must not hide a missing file in the load report: the
// missing file is what keeps the popup open, so the message has to name it.
TEST(ProjectServiceTest, AnAcknowledgedChangeDoesNotHideAMissingFileInTheLoadReport) {
    TempDir tmp(MakeTempParent());
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    core::ProjectFileEntry entry;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("Missing", tmp.path, project, report, error)) << error;
    ASSERT_TRUE(core::ProjectService::AddEvidenceRegister(project, "", entry, error)) << error;
    AppendNewline(project.rootPath / entry.relativePath);

    core::AssuranceProject opened;
    core::ProjectLoadReport first;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, opened, first, error)) << error;
    ASSERT_TRUE(core::ProjectService::AcknowledgeExternalChanges(opened, first.externalChanges, error)) << error;

    const auto other = std::find_if(opened.files.begin(), opened.files.end(), [](const core::ProjectFileEntry& file) {
        return file.role != core::ProjectFileRole::EvidenceRegister;
    });
    ASSERT_NE(other, opened.files.end());
    std::filesystem::remove(project.rootPath / other->relativePath);

    core::AssuranceProject reopened;
    core::ProjectLoadReport second;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, reopened, second, error)) << error;
    EXPECT_TRUE(second.showPopup);
    const auto step = std::find_if(second.steps.begin(), second.steps.end(), [](const core::ProjectLoadStep& s) {
        return s.label == "Recalculate raw hashes";
    });
    ASSERT_NE(step, second.steps.end());
    EXPECT_NE(step->message.find("1 missing file(s)"), std::string::npos) << step->message;
    EXPECT_NE(step->message.find("already acknowledged"), std::string::npos) << step->message;
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
namespace {

std::string ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

// A SACM file that is known to load: the seed of a freshly created project,
// copied out under a name that is not `main.sacm` so the tests can tell the
// imported copy from the seed the destination project already has.
std::filesystem::path WriteLoadableSacm(const std::filesystem::path& parent, const char* file_name) {
    core::AssuranceProject donor;
    core::ProjectLoadReport report;
    std::string error;
    EXPECT_TRUE(core::ProjectService::CreateEmptyProject("Donor", parent, donor, report, error)) << error;
    const std::filesystem::path source = parent / file_name;
    std::filesystem::copy_file(donor.rootPath / "arguments" / "main.sacm", source);
    return source;
}

} // namespace

TEST(ProjectServiceTest, DefaultImportedSacmFileNameKeepsTheStemAndNormalizesTheExtension) {
    EXPECT_EQ(core::ProjectService::DefaultImportedSacmFileName("C:/cases/existing-case.xml").generic_string(),
              "existing-case.sacm");
    EXPECT_EQ(core::ProjectService::DefaultImportedSacmFileName("/cases/existing-case.sacm").generic_string(),
              "existing-case.sacm");
    EXPECT_EQ(core::ProjectService::DefaultImportedSacmFileName("existing-case.SACM").generic_string(),
              "existing-case.sacm");
    EXPECT_EQ(core::ProjectService::DefaultImportedSacmFileName("").generic_string(), "main.sacm");
}

// An import is a copy: the file is the argument, so the bytes must land in the
// project unchanged, tracked under its role, and be there when the manifest is
// read back.
TEST(ProjectServiceTest, ImportSacmFileCopiesTheArgumentByteForByteAndTracksIt) {
    TempDir tmp(MakeTempParent());
    const std::filesystem::path source = WriteLoadableSacm(tmp.path, "existing-case.xml");

    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("Target", tmp.path, project, report, error)) << error;
    const size_t files_before = project.files.size();

    core::ProjectFileEntry entry;
    ASSERT_TRUE(core::ProjectService::ImportSacmFile(project, source, "", entry, error)) << error;
    EXPECT_EQ(entry.relativePath.generic_string(), "arguments/existing-case.sacm");
    EXPECT_EQ(entry.role, core::ProjectFileRole::SacmArgument);
    EXPECT_EQ(project.files.size(), files_before + 1);
    EXPECT_EQ(ReadWholeFile(project.rootPath / entry.relativePath), ReadWholeFile(source));
    EXPECT_TRUE(std::filesystem::exists(source)) << "the source is copied, never moved";

    core::AssuranceProject reopened;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, reopened, report, error)) << error;
    EXPECT_TRUE(ContainsFileWithRole(reopened, "arguments/existing-case.sacm", core::ProjectFileRole::SacmArgument));

    // A requested name wins over the source's, and lands with the extension.
    core::ProjectFileEntry renamed;
    ASSERT_TRUE(core::ProjectService::ImportSacmFile(project, source, "second copy", renamed, error)) << error;
    EXPECT_EQ(renamed.relativePath.generic_string(), "arguments/second copy.sacm");

    // A name the project already tracks is refused rather than overwritten:
    // `main.sacm` is the seed argument, and an import that replaced it would
    // silently discard whatever was in it.
    core::ProjectFileEntry clash;
    EXPECT_FALSE(core::ProjectService::ImportSacmFile(project, source, "main.sacm", clash, error));
    EXPECT_NE(error.find("already tracked"), std::string::npos) << error;
}

// A tracked path is taken even when its file is gone from disk. Checking only
// the disk let an import add a second manifest entry for the same path, so the
// project reported the file missing and fresh at once.
TEST(ProjectServiceTest, ImportSacmFileRefusesAPathTheManifestTracksEvenWhenTheFileIsMissing) {
    TempDir tmp(MakeTempParent());
    const std::filesystem::path source = WriteLoadableSacm(tmp.path, "existing-case.xml");

    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("Target", tmp.path, project, report, error)) << error;
    ASSERT_TRUE(std::filesystem::remove(project.rootPath / "arguments" / "main.sacm"));
    const size_t files_before = project.files.size();

    core::ProjectFileEntry entry;
    EXPECT_FALSE(core::ProjectService::ImportSacmFile(project, source, "main.sacm", entry, error));
    EXPECT_NE(error.find("already tracked"), std::string::npos) << error;
    EXPECT_EQ(project.files.size(), files_before);
    EXPECT_FALSE(std::filesystem::exists(project.rootPath / "arguments" / "main.sacm"))
        << "a refused import must not write the file either";

    // The same rule guards the seeded create, which shares the helper.
    EXPECT_FALSE(core::ProjectService::AddSacmFile(project, "main.sacm", entry, error));
    EXPECT_NE(error.find("already tracked"), std::string::npos) << error;
    EXPECT_EQ(project.files.size(), files_before);
}

// The project must never track an argument it cannot open. A file the library
// refuses is refused here, with the library's reason, and nothing changes.
TEST(ProjectServiceTest, ImportSacmFileRefusesAFileTheLibraryCannotLoad) {
    TempDir tmp(MakeTempParent());
    const std::filesystem::path bogus = tmp.path / "not-a-case.sacm";
    {
        std::ofstream out(bogus);
        out << "<html><body>not an assurance case</body></html>";
    }

    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("Target", tmp.path, project, report, error)) << error;
    const size_t files_before = project.files.size();

    core::ProjectFileEntry entry;
    EXPECT_FALSE(core::ProjectService::ImportSacmFile(project, bogus, "", entry, error));
    EXPECT_NE(error.find("Not a loadable SACM file"), std::string::npos) << error;
    EXPECT_EQ(project.files.size(), files_before);
    EXPECT_FALSE(std::filesystem::exists(project.rootPath / "arguments" / "not-a-case.sacm"));

    EXPECT_FALSE(core::ProjectService::ImportSacmFile(project, tmp.path / "missing.sacm", "", entry, error));
    EXPECT_NE(error.find("not found"), std::string::npos) << error;
    EXPECT_EQ(project.files.size(), files_before);
}

// The welcome screen's "Create Project from Existing SACM": the copy is the
// project's first (and only) argument, the review file is there as for an empty
// create, and the report says the project is healthy.
TEST(ProjectServiceTest, CreateProjectFromSacmMakesTheCopyTheFirstArgument) {
    TempDir tmp(MakeTempParent());
    const std::filesystem::path source = WriteLoadableSacm(tmp.path, "existing-case.xml");

    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateProjectFromSacm("FromSacm", tmp.path, source, project, report, error))
        << error;
    EXPECT_EQ(project.name, "FromSacm");
    EXPECT_EQ(project.rootPath, tmp.path / "FromSacm");
    EXPECT_TRUE(std::filesystem::exists(project.rootPath / "af.proj"));
    EXPECT_TRUE(ContainsFileWithRole(project, "arguments/existing-case.sacm", core::ProjectFileRole::SacmArgument));
    EXPECT_FALSE(ContainsFileWithRole(project, "arguments/main.sacm", core::ProjectFileRole::SacmArgument))
        << "the imported file replaces the empty seed rather than sitting beside it";
    EXPECT_TRUE(ContainsFileWithRole(project, "reviews/review-items.af.json", core::ProjectFileRole::ReviewItems));
    EXPECT_EQ(ReadWholeFile(project.rootPath / "arguments" / "existing-case.sacm"), ReadWholeFile(source));
    EXPECT_FALSE(report.has_failures()) << ReportSummary(report);

    size_t arguments = 0;
    for (const core::ProjectFileEntry& entry : project.files) {
        if (entry.role == core::ProjectFileRole::SacmArgument)
            ++arguments;
    }
    EXPECT_EQ(arguments, 1u);
}

// A refused source must not leave an empty project folder behind: the next
// attempt with the same name would then be told the folder already exists,
// for a project that was never made.
TEST(ProjectServiceTest, CreateProjectFromSacmRefusesBeforeScaffolding) {
    TempDir tmp(MakeTempParent());
    const std::filesystem::path bogus = tmp.path / "not-a-case.sacm";
    {
        std::ofstream out(bogus);
        out << "this is not xml";
    }

    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    EXPECT_FALSE(core::ProjectService::CreateProjectFromSacm("FromBogus", tmp.path, bogus, project, report, error));
    EXPECT_NE(error.find("Not a loadable SACM file"), std::string::npos) << error;
    EXPECT_FALSE(std::filesystem::exists(tmp.path / "FromBogus"));
    EXPECT_EQ(core::ProjectService::FindCreateProjectObstacle("FromBogus", tmp.path),
              core::CreateProjectObstacle::None);

    // And the create-obstacle rule still applies before anything is copied.
    const std::filesystem::path source = WriteLoadableSacm(tmp.path, "existing-case.sacm");
    EXPECT_FALSE(core::ProjectService::CreateProjectFromSacm("Donor", tmp.path, source, project, report, error));
    EXPECT_NE(error.find("already exists"), std::string::npos) << error;
}

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
