#include "core/project_model.h"

#include <gtest/gtest.h>

#include <array>
#include <string>

// Intent of project_model conversions: ProjectFileRole<->string is a stable
// round-trip used for persistence, unknown strings degrade to Unknown, display
// strings are always presentable (non-empty), and ProjectLoadReport::has_failures
// is true exactly when at least one step Failed. Assertions encode that intent.

namespace {

constexpr std::array<core::ProjectFileRole, 10> kAllRoles = {
    core::ProjectFileRole::SacmArgument,
    core::ProjectFileRole::EvidenceRegister,
    core::ProjectFileRole::J3377CaeRegister,
    core::ProjectFileRole::RegisterAssessments,
    core::ProjectFileRole::ReviewItems,
    core::ProjectFileRole::ReviewProposal,
    core::ProjectFileRole::ConfidenceAssessments,
    core::ProjectFileRole::ConformanceSheet,
    core::ProjectFileRole::ExportedReport,
    core::ProjectFileRole::Unknown,
};

constexpr std::array<core::ProjectFileState, 9> kAllStates = {
    core::ProjectFileState::Clean,
    core::ProjectFileState::ModifiedOutsideAssuranceForge,
    core::ProjectFileState::ModifiedButCompatible,
    core::ProjectFileState::ModifiedWithBrokenReferences,
    core::ProjectFileState::Missing,
    core::ProjectFileState::Moved,
    core::ProjectFileState::ParseError,
    core::ProjectFileState::UnsupportedVersion,
    core::ProjectFileState::GeneratedFileOutdated,
};

} // namespace

TEST(ProjectModelTest, FileRoleStringRoundTripsForEveryRole) {
    for (core::ProjectFileRole role : kAllRoles) {
        const std::string serialized = core::ProjectFileRoleToString(role);
        EXPECT_FALSE(serialized.empty());
        EXPECT_EQ(core::ProjectFileRoleFromString(serialized), role) << "role string: " << serialized;
    }
}

TEST(ProjectModelTest, UnrecognizedRoleStringDegradesToUnknown) {
    EXPECT_EQ(core::ProjectFileRoleFromString("not.a.real.role"), core::ProjectFileRole::Unknown);
    EXPECT_EQ(core::ProjectFileRoleFromString(""), core::ProjectFileRole::Unknown);
}

TEST(ProjectModelTest, DisplayStringsArePresentableForEveryRoleAndState) {
    for (core::ProjectFileRole role : kAllRoles) {
        EXPECT_NE(core::ProjectFileRoleToDisplayString(role), nullptr);
        EXPECT_FALSE(std::string(core::ProjectFileRoleToDisplayString(role)).empty());
    }
    for (core::ProjectFileState state : kAllStates) {
        EXPECT_FALSE(std::string(core::ProjectFileStateToString(state)).empty());
        EXPECT_FALSE(std::string(core::ProjectFileStateToDisplayString(state)).empty());
    }
}

TEST(ProjectModelTest, HasFailuresIsTrueOnlyWhenAStepFailed) {
    core::ProjectLoadReport report;
    EXPECT_FALSE(report.has_failures()) << "an empty report has no failures";

    report.steps.push_back({"parse", core::ProjectLoadStepStatus::Passed, ""});
    report.steps.push_back({"validate", core::ProjectLoadStepStatus::Warning, ""});
    EXPECT_FALSE(report.has_failures()) << "passed/warning steps are not failures";

    report.steps.push_back({"load", core::ProjectLoadStepStatus::Failed, "boom"});
    EXPECT_TRUE(report.has_failures());
}
