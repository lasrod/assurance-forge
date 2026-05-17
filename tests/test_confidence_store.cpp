#include "app/app_events.h"
#include "app/controllers/confidence_controller.h"
#include "core/confidence/confidence_store.h"
#include "core/project_service.h"
#include "core/sacm_identity.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <regex>

namespace {

parser::SacmElement MakeClaim(std::string gid = "G1", std::string content = "The ODD is well defined") {
    parser::SacmElement element;
    element.id = "claim-1";
    element.gid = std::move(gid);
    element.type = "claim";
    element.name = "G1";
    element.content = std::move(content);
    element.assertion_declaration = "asserted";
    return element;
}

core::confidence::ConfidenceAssessment MakeFixedAssessment(const parser::SacmElement& element) {
    core::confidence::FixedConfidenceValue fixed;
    fixed.value = 0.80;

    core::confidence::ConfidenceAssessment assessment;
    assessment.id = "conf-000001";
    assessment.target.kind = core::confidence::ConfidenceTargetKind::Element;
    assessment.target.sourceId = "main";
    assessment.target.sacmGid = element.gid;
    assessment.target.sacmType = core::confidence::DisplaySacmType(element);
    assessment.method = core::confidence::ConfidenceMethod::FixedValue;
    assessment.fixedValue = fixed;
    assessment.derived = core::confidence::DerivedFor(fixed);
    assessment.targetFingerprint = core::confidence::FingerprintElement(element);
    assessment.createdAt = "2026-05-17T00:00:00Z";
    assessment.updatedAt = "2026-05-17T00:00:00Z";
    return assessment;
}

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
        std::filesystem::temp_directory_path() / ("assurance_forge_confidence_test_" + std::to_string(stamp));
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

} // namespace

TEST(ConfidenceStoreTest, FixedValueRoundTripsWithExpectedConfidence) {
    const parser::SacmElement element = MakeClaim();
    core::confidence::ConfidenceStore store;
    store.projectId = "project-1";
    store.sacmSources.push_back(core::confidence::SacmSource{"main", "arguments/main.sacm", "sha256:abc"});
    store.assessments.push_back(MakeFixedAssessment(element));

    const std::string json = core::confidence::SerializeConfidenceStore(store);
    EXPECT_NE(json.find("\"schema\": \"assurance-forge.confidence\""), std::string::npos);
    EXPECT_NE(json.find("\"fixedValue\""), std::string::npos);
    EXPECT_EQ(json.back(), '\n');

    core::confidence::ConfidenceStore restored;
    std::string error;
    ASSERT_TRUE(core::confidence::DeserializeConfidenceStore(json, restored, error)) << error;
    ASSERT_EQ(restored.assessments.size(), 1u);
    const auto& assessment = restored.assessments.front();
    ASSERT_TRUE(assessment.fixedValue.has_value());
    EXPECT_EQ(assessment.target.sacmGid, "G1");
    EXPECT_DOUBLE_EQ(assessment.fixedValue->value, 0.80);
    EXPECT_DOUBLE_EQ(assessment.derived.expectedConfidence, 0.80);
}

TEST(ConfidenceStoreTest, InactiveAssessmentRoundTripsAndRemainsFindable) {
    const parser::SacmElement element = MakeClaim();
    core::confidence::ConfidenceStore store;
    store.assessments.push_back(MakeFixedAssessment(element));
    store.assessments.front().status = core::confidence::ConfidenceStatus::Inactive;

    const std::string json = core::confidence::SerializeConfidenceStore(store);
    EXPECT_NE(json.find("\"status\": \"inactive\""), std::string::npos);

    core::confidence::ConfidenceStore restored;
    std::string error;
    ASSERT_TRUE(core::confidence::DeserializeConfidenceStore(json, restored, error)) << error;
    const core::confidence::ConfidenceAssessment* assessment =
        core::confidence::FindAssessment(restored, "main", element.gid);
    ASSERT_NE(assessment, nullptr);
    EXPECT_EQ(assessment->status, core::confidence::ConfidenceStatus::Inactive);
    ASSERT_TRUE(assessment->fixedValue.has_value());
    EXPECT_DOUBLE_EQ(assessment->fixedValue->value, 0.80);
}

TEST(ConfidenceStoreTest, JosangOpinionStoresRawValuesAndDerivedConfidence) {
    core::confidence::JosangOpinion opinion;
    opinion.belief = 0.65;
    opinion.disbelief = 0.10;
    opinion.uncertainty = 0.25;
    opinion.baseRate = 0.50;

    std::string error;
    ASSERT_TRUE(core::confidence::ValidateJosangOpinion(opinion, error)) << error;

    core::confidence::ConfidenceAssessment assessment;
    assessment.id = "conf-000001";
    assessment.target.sourceId = "main";
    assessment.target.sacmGid = "G1";
    assessment.target.sacmType = "Claim";
    assessment.method = core::confidence::ConfidenceMethod::JosangOpinion;
    assessment.josangOpinion = opinion;
    assessment.derived = core::confidence::DerivedFor(opinion);
    assessment.createdAt = "2026-05-17T00:00:00Z";
    assessment.updatedAt = "2026-05-17T00:00:00Z";

    core::confidence::ConfidenceStore store;
    store.assessments.push_back(assessment);

    core::confidence::ConfidenceStore restored;
    ASSERT_TRUE(core::confidence::DeserializeConfidenceStore(core::confidence::SerializeConfidenceStore(store), restored, error))
        << error;
    ASSERT_EQ(restored.assessments.size(), 1u);
    ASSERT_TRUE(restored.assessments.front().josangOpinion.has_value());
    EXPECT_DOUBLE_EQ(restored.assessments.front().josangOpinion->belief, 0.65);
    EXPECT_DOUBLE_EQ(restored.assessments.front().josangOpinion->disbelief, 0.10);
    EXPECT_DOUBLE_EQ(restored.assessments.front().josangOpinion->uncertainty, 0.25);
    EXPECT_DOUBLE_EQ(restored.assessments.front().josangOpinion->baseRate, 0.50);
    EXPECT_DOUBLE_EQ(restored.assessments.front().derived.expectedConfidence, 0.775);
}

TEST(ConfidenceStoreTest, RejectsInvalidJsonAndInvalidJosangSum) {
    core::confidence::ConfidenceStore ignored;
    std::string error;
    EXPECT_FALSE(core::confidence::DeserializeConfidenceStore("{ not json", ignored, error));
    EXPECT_FALSE(error.empty());

    core::confidence::JosangOpinion opinion;
    opinion.belief = 0.6;
    opinion.disbelief = 0.1;
    opinion.uncertainty = 0.1;
    opinion.baseRate = 0.5;
    EXPECT_FALSE(core::confidence::ValidateJosangOpinion(opinion, error));
}

TEST(ConfidenceStoreTest, RefreshStaleFlagsMarksChangedTargetsButIgnoresOrphans) {
    parser::SacmElement original = MakeClaim("G1", "The ODD is well defined");
    core::confidence::ConfidenceStore store;
    store.assessments.push_back(MakeFixedAssessment(original));

    parser::AssuranceCase changed_model;
    changed_model.elements.push_back(MakeClaim("G1", "The ODD is completely defined for target use cases"));
    changed_model.elements.push_back(MakeClaim("G2", "Other claim"));

    int inactivated_count = 0;
    EXPECT_TRUE(core::confidence::RefreshStaleFlags(store, "main", changed_model, &inactivated_count));
    ASSERT_EQ(store.assessments.size(), 1u);
    EXPECT_TRUE(store.assessments.front().stale);
    EXPECT_EQ(store.assessments.front().status, core::confidence::ConfidenceStatus::Inactive);
    ASSERT_TRUE(store.assessments.front().fixedValue.has_value());
    EXPECT_DOUBLE_EQ(store.assessments.front().fixedValue->value, 0.80);
    EXPECT_EQ(inactivated_count, 1);

    EXPECT_FALSE(core::confidence::RefreshStaleFlags(store, "main", changed_model, &inactivated_count));
    EXPECT_EQ(inactivated_count, 0);

    core::confidence::ConfidenceAssessment orphan = MakeFixedAssessment(MakeClaim("G3", "Deleted claim"));
    orphan.id = "conf-000002";
    store.assessments.push_back(orphan);
    EXPECT_FALSE(core::confidence::RefreshStaleFlags(store, "main", changed_model));
    EXPECT_FALSE(store.assessments.back().stale);
}

TEST(ConfidenceStoreTest, RefreshStaleFlagsMarksNameChangesInactive) {
    parser::SacmElement original = MakeClaim("G1", "The ODD is well defined");
    original.name = "Original claim";
    core::confidence::ConfidenceStore store;
    store.assessments.push_back(MakeFixedAssessment(original));

    parser::SacmElement renamed = original;
    renamed.name = "Renamed claim";
    parser::AssuranceCase changed_model;
    changed_model.elements.push_back(renamed);

    int inactivated_count = 0;
    EXPECT_TRUE(core::confidence::RefreshStaleFlags(store, "main", changed_model, &inactivated_count));
    ASSERT_EQ(store.assessments.size(), 1u);
    EXPECT_TRUE(store.assessments.front().stale);
    EXPECT_EQ(store.assessments.front().status, core::confidence::ConfidenceStatus::Inactive);
    EXPECT_EQ(inactivated_count, 1);
}

TEST(ConfidenceStoreTest, GenerateSacmGidUsesFullUuidShapeAndEntropy) {
    const std::regex uuid_regex(
        "^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
    bool saw_nonzero_final_prefix = false;
    for (int index = 0; index < 64; ++index) {
        const std::string gid = core::GenerateSacmGid();
        EXPECT_TRUE(std::regex_match(gid, uuid_regex)) << gid;
        const std::string final_group = gid.substr(gid.rfind('-') + 1);
        if (final_group.substr(0, 4) != "0000")
            saw_nonzero_final_prefix = true;
    }
    EXPECT_TRUE(saw_nonzero_final_prefix);
}

TEST(ConfidenceControllerTest, RefreshStaleFlagsDoesNotEmitDirtyEvent) {
    TempDir tmp(MakeTempParent());
    const std::filesystem::path confidence_path = tmp.path / "confidence.af.json";

    parser::SacmElement original = MakeClaim("G1", "Original claim text");
    core::confidence::ConfidenceStore store;
    store.projectId = "project-1";
    store.assessments.push_back(MakeFixedAssessment(original));
    {
        std::ofstream file(confidence_path, std::ios::binary);
        file << core::confidence::SerializeConfidenceStore(store);
    }

    app::AppEvents events;
    int dirty_events = 0;
    const auto subscription = events.Subscribe<app::ConfidenceDirtyEvent>(
        [&](const app::ConfidenceDirtyEvent&) { ++dirty_events; });

    app::controllers::ConfidenceController controller(events);
    std::string error;
    ASSERT_TRUE(controller.ConfigureStorage(confidence_path, "project-1", error)) << error;
    controller.SetActiveSource("main", "arguments/main.sacm", {});

    parser::AssuranceCase changed_model;
    changed_model.elements.push_back(MakeClaim("G1", "Changed claim text"));

    EXPECT_TRUE(controller.RefreshStaleFlags(changed_model));
    EXPECT_TRUE(controller.IsDirty());
    EXPECT_EQ(controller.LastInactivatedCount(), 1);
    EXPECT_EQ(dirty_events, 0);

    events.Unsubscribe(subscription);
}

TEST(ConfidenceStoreTest, EnsureElementGidGeneratesAndMirrorsToSacmPackage) {
    parser::AssuranceCase model;
    parser::SacmElement element = MakeClaim("", "Claim text");
    model.elements.push_back(element);

    sacm::AssuranceCasePackage package;
    package.argumentPackages.emplace_back();
    sacm::Claim claim;
    claim.id = element.id;
    package.argumentPackages.front().claims.push_back(claim);

    std::string error;
    EXPECT_EQ(core::EnsureElementGid(model, &package, model.elements.front(), error), core::EnsureGidResult::Generated)
        << error;
    EXPECT_FALSE(model.elements.front().gid.empty());
    EXPECT_EQ(package.argumentPackages.front().claims.front().gid, model.elements.front().gid);
    EXPECT_EQ(core::EnsureElementGid(model, &package, model.elements.front(), error), core::EnsureGidResult::AlreadyPresent)
        << error;
}

TEST(ConfidenceStoreTest, SaveConfidenceFileCreatesAnalysisSidecarAndTracksManifestRole) {
    TempDir tmp(MakeTempParent());
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", tmp.path, project, report, error)) << error;
    EXPECT_FALSE(ContainsFileWithRole(
        project, "analysis/confidence.af.json", core::ProjectFileRole::ConfidenceAssessments));

    core::confidence::ConfidenceStore store;
    store.projectId = project.id;
    store.assessments.push_back(MakeFixedAssessment(MakeClaim()));

    core::ProjectFileEntry entry;
    ASSERT_TRUE(core::ProjectService::SaveConfidenceFile(
        project, core::confidence::SerializeConfidenceStore(store), entry, error))
        << error;
    EXPECT_EQ(entry.relativePath.generic_string(), "analysis/confidence.af.json");
    EXPECT_EQ(entry.role, core::ProjectFileRole::ConfidenceAssessments);
    EXPECT_TRUE(std::filesystem::exists(project.rootPath / "analysis" / "confidence.af.json"));
    EXPECT_TRUE(ContainsFileWithRole(
        project, "analysis/confidence.af.json", core::ProjectFileRole::ConfidenceAssessments));

    core::AssuranceProject reopened;
    core::ProjectLoadReport open_report;
    ASSERT_TRUE(core::ProjectService::OpenProject(project.rootPath, reopened, open_report, error)) << error;
    EXPECT_TRUE(ContainsFileWithRole(
        reopened, "analysis/confidence.af.json", core::ProjectFileRole::ConfidenceAssessments));
}
