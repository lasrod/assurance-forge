// AF-ENG-016: register assessments persist with the project.
//
// The register *rows* are derived from the argument and cost nothing to lose.
// The assessment fields are the reviewer's own judgement and exist nowhere
// else, so these tests are about one thing: what a user typed is still there
// after the project is closed and reopened, and is never overwritten by a store
// the app failed to load.

#include "app/app_events.h"
#include "app/controllers/register_controller.h"
#include "core/project_service.h"
#include "core/registers/register_model.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(std::filesystem::path p) : path(std::move(p)) {}
    ~TempDir() noexcept {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

std::filesystem::path MakeTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();

    std::error_code ec;
    const std::filesystem::path temp_root = std::filesystem::temp_directory_path(ec);
    if (ec) {
        ADD_FAILURE() << "Failed to obtain temporary directory path: " << ec.message();
        return {};
    }

    const std::filesystem::path path =
        temp_root / ("assurance_forge_register_controller_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path, ec);
    if (ec) {
        ADD_FAILURE() << "Failed to create temporary directory '" << path.string() << "': " << ec.message();
        return {};
    }
    return path;
}

struct RegisterHarness {
    app::AppEvents events;
    app::controllers::RegisterController controller;
    int dirty_events = 0;

    RegisterHarness() : controller(events) {
        events.Subscribe<app::RegisterAssessmentsDirtyEvent>(
            [this](const app::RegisterAssessmentsDirtyEvent&) { ++dirty_events; });
    }
};

std::filesystem::path RegisterPath(const core::AssuranceProject& project) {
    return project.rootPath / "registers" / "register-assessments.af.json";
}

const core::ProjectFileEntry* FindEntry(const core::AssuranceProject& project, const std::string& relative_path) {
    for (const core::ProjectFileEntry& entry : project.files) {
        if (entry.relativePath.generic_string() == relative_path)
            return &entry;
    }
    return nullptr;
}

void TypeCseAssessment(app::controllers::RegisterController& controller,
                       const std::string& cse_id,
                       const std::string& owner,
                       const std::string& status) {
    core::registers::CseMetadata& metadata = controller.MutableStore().cse[cse_id];
    metadata.claim_owner = owner;
    metadata.assessment_status = status;
    metadata.notes = "Reviewed against the test procedure.";
    controller.MarkDirty();
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

} // namespace

TEST(RegisterControllerTest, TypedAssessmentsSurviveClosingAndReopeningTheProject) {
    TempDir temp(MakeTempDir());
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", temp.path, project, report, error)) << error;

    RegisterHarness authoring;
    ASSERT_TRUE(authoring.controller.ConfigureStorage(RegisterPath(project), error)) << error;

    const std::string cse_id = core::registers::MakeCseId("G1", "Sn1");
    TypeCseAssessment(authoring.controller, cse_id, "Alice", "Adequately Supported");
    core::registers::EvidenceMetadata& evidence = authoring.controller.MutableStore().evidence["Sn1"];
    evidence.evidence_owner = "Bob";
    evidence.maturity = "Released";

    EXPECT_TRUE(authoring.controller.IsDirty());
    EXPECT_EQ(authoring.dirty_events, 1);
    ASSERT_TRUE(authoring.controller.SaveIfDirty(project, error)) << error;
    EXPECT_FALSE(authoring.controller.IsDirty());

    // A second session: reopen the project from its manifest, as the app does.
    core::AssuranceProject reopened;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, reopened, report, error)) << error;

    RegisterHarness next_session;
    ASSERT_TRUE(next_session.controller.ConfigureStorage(RegisterPath(reopened), error)) << error;

    const core::registers::RegisterStore& store = next_session.controller.Store();
    ASSERT_TRUE(store.cse.count(cse_id) == 1u);
    EXPECT_EQ(store.cse.at(cse_id).claim_owner, "Alice");
    EXPECT_EQ(store.cse.at(cse_id).assessment_status, "Adequately Supported");
    EXPECT_EQ(store.cse.at(cse_id).notes, "Reviewed against the test procedure.");
    ASSERT_TRUE(store.evidence.count("Sn1") == 1u);
    EXPECT_EQ(store.evidence.at("Sn1").evidence_owner, "Bob");
    EXPECT_EQ(store.evidence.at("Sn1").maturity, "Released");
    EXPECT_FALSE(next_session.controller.IsDirty());
}

TEST(RegisterControllerTest, SavedAssessmentsAreTrackedInTheProjectManifest) {
    TempDir temp(MakeTempDir());
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", temp.path, project, report, error)) << error;

    RegisterHarness harness;
    ASSERT_TRUE(harness.controller.ConfigureStorage(RegisterPath(project), error)) << error;
    TypeCseAssessment(harness.controller, core::registers::MakeCseId("G1", "Sn1"), "Alice", "Not Assessed");
    ASSERT_TRUE(harness.controller.SaveIfDirty(project, error)) << error;

    EXPECT_TRUE(std::filesystem::exists(RegisterPath(project)));
    EXPECT_EQ(harness.controller.FilePath(), RegisterPath(project));

    // An untracked file is one the project health report cannot check and the
    // explorer cannot show, so tracking is part of "persists with the project".
    core::AssuranceProject reopened;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, reopened, report, error)) << error;
    const core::ProjectFileEntry* entry = FindEntry(reopened, "registers/register-assessments.af.json");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->role, core::ProjectFileRole::RegisterAssessments);
    EXPECT_FALSE(entry->rawHash.empty());
    EXPECT_EQ(entry->state, core::ProjectFileState::Clean);
}

TEST(RegisterControllerTest, UnreadableStoreIsReportedAndNeverOverwritten) {
    TempDir temp(MakeTempDir());
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", temp.path, project, report, error)) << error;

    const std::filesystem::path path = RegisterPath(project);
    std::filesystem::create_directories(path.parent_path());
    const std::string corrupt = "{ \"format\": \"assurance-forge-register-assessments\", \"cseAssessments\": [";
    {
        std::ofstream file(path, std::ios::binary);
        file << corrupt;
    }

    RegisterHarness harness;
    EXPECT_FALSE(harness.controller.ConfigureStorage(path, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(harness.controller.HasStorageError());
    EXPECT_TRUE(harness.controller.Store().cse.empty());

    // Whatever assessments that file held, we could not read them. Saving the
    // empty store over it would finish the job the corruption started.
    harness.controller.MutableStore().cse[core::registers::MakeCseId("G1", "Sn1")].claim_owner = "Alice";
    harness.controller.MarkDirty();
    EXPECT_FALSE(harness.controller.SaveIfDirty(project, error));
    EXPECT_NE(error.find("has not been recovered"), std::string::npos) << error;
    EXPECT_EQ(ReadFile(path), corrupt);
}

TEST(RegisterControllerTest, ForeignFormatIsRefusedRatherThanReadIntoAssessmentFields) {
    TempDir temp(MakeTempDir());
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", temp.path, project, report, error)) << error;

    const std::filesystem::path path = RegisterPath(project);
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream file(path, std::ios::binary);
        file << "{ \"format\": \"assurance-forge-evidence-register\", \"entries\": [] }";
    }

    RegisterHarness harness;
    EXPECT_FALSE(harness.controller.ConfigureStorage(path, error));
    EXPECT_TRUE(harness.controller.HasStorageError());
}

TEST(RegisterControllerTest, ReconfiguringTheSamePathKeepsUnsavedAssessments) {
    TempDir temp(MakeTempDir());
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", temp.path, project, report, error)) << error;

    RegisterHarness harness;
    const std::filesystem::path path = RegisterPath(project);
    ASSERT_TRUE(harness.controller.ConfigureStorage(path, error)) << error;

    const std::string cse_id = core::registers::MakeCseId("G1", "Sn1");
    TypeCseAssessment(harness.controller, cse_id, "Alice", "Adequately Supported");

    // Every project-open path re-runs ConfigureStorage; doing so while an edit
    // is pending must not throw the edit away.
    ASSERT_TRUE(harness.controller.ConfigureStorage(path, error)) << error;

    ASSERT_TRUE(harness.controller.Store().cse.count(cse_id) == 1u);
    EXPECT_EQ(harness.controller.Store().cse.at(cse_id).claim_owner, "Alice");
    EXPECT_TRUE(harness.controller.IsDirty());
}

TEST(RegisterControllerTest, StoreDoesNotFollowTheControllerIntoAnotherProject) {
    TempDir temp(MakeTempDir());
    core::AssuranceProject first;
    core::AssuranceProject second;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("FirstCase", temp.path, first, report, error)) << error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("SecondCase", temp.path, second, report, error)) << error;

    RegisterHarness harness;
    ASSERT_TRUE(harness.controller.ConfigureStorage(RegisterPath(first), error)) << error;
    TypeCseAssessment(harness.controller, core::registers::MakeCseId("G1", "Sn1"), "Alice", "Adequately Supported");
    ASSERT_TRUE(harness.controller.SaveIfDirty(first, error)) << error;

    // Switching projects re-points the controller. The second project must not
    // inherit the first project's assessments about elements it never had.
    harness.controller.ClearDirty();
    ASSERT_TRUE(harness.controller.ConfigureStorage(RegisterPath(second), error)) << error;

    EXPECT_TRUE(harness.controller.Store().cse.empty());
    EXPECT_TRUE(harness.controller.Store().evidence.empty());
    EXPECT_FALSE(harness.controller.IsDirty());
    EXPECT_FALSE(std::filesystem::exists(RegisterPath(second)));
}

TEST(RegisterControllerTest, DiscardingAnAssessmentRemovesItAndOnlyReachesDiskOnSave) {
    TempDir temp(MakeTempDir());
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", temp.path, project, report, error)) << error;

    RegisterHarness harness;
    ASSERT_TRUE(harness.controller.ConfigureStorage(RegisterPath(project), error)) << error;
    const std::string cse_id = core::registers::MakeCseId("G1", "Sn1");
    TypeCseAssessment(harness.controller, cse_id, "Alice", "Adequately Supported");
    harness.controller.MutableStore().evidence["Sn1"].evidence_owner = "Bob";
    ASSERT_TRUE(harness.controller.SaveIfDirty(project, error)) << error;

    EXPECT_TRUE(harness.controller.DiscardCseAssessment(cse_id));
    EXPECT_TRUE(harness.controller.Store().cse.empty());
    EXPECT_TRUE(harness.controller.IsDirty());
    // Discarding twice is not an error the user needs to see, but it is not a
    // second edit either.
    EXPECT_FALSE(harness.controller.DiscardCseAssessment(cse_id));

    // Until the project is saved the file still holds it, which is the only way
    // back from a mis-click.
    EXPECT_NE(ReadFile(RegisterPath(project)).find(cse_id), std::string::npos);
    ASSERT_TRUE(harness.controller.SaveIfDirty(project, error)) << error;
    const std::string saved = ReadFile(RegisterPath(project));
    EXPECT_EQ(saved.find(cse_id), std::string::npos);
    // Discarding one assessment must not take the neighbouring one with it.
    EXPECT_NE(saved.find("Bob"), std::string::npos);

    EXPECT_TRUE(harness.controller.DiscardEvidenceAssessment("Sn1"));
    EXPECT_TRUE(harness.controller.Store().evidence.empty());
}

TEST(RegisterControllerTest, ClearingStorageDropsTheStoreAndItsPath) {
    RegisterHarness harness;
    harness.controller.MutableStore().evidence["Sn1"].evidence_owner = "Bob";
    harness.controller.MarkDirty();

    harness.controller.ClearStorage();

    EXPECT_TRUE(harness.controller.Store().evidence.empty());
    EXPECT_FALSE(harness.controller.IsDirty());
    EXPECT_TRUE(harness.controller.FilePath().empty());
}
