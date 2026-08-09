#include "core/commands/package_commands.h"

#include "core/acp/assurance_claim_point.h"
#include "legacy_sacm/sacm_model.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

// Intent of package_commands (core/commands/package_commands.h):
//  - DeleteArgumentPackage removes the SACM argument package AND scrubs the
//    parser projections of every element that lived inside it, so the GSN tree
//    has no orphans; when the removed package backed a separate confidence
//    argument tree it also clears the link on any ACP that referenced it.
//  - DeleteArtifactPackage removes the matching artifact package.
//  - Both return false with an error when no matching package exists.
//  - The Remove*Command wrappers require an id or gid and record the identity
//    in the audit payload.
// Assertions encode that intent; failures are findings.

namespace {

template <typename Range>
bool ContainsId(const Range& range, const std::string& id) {
    return std::any_of(range.begin(), range.end(), [&](const auto& item) { return item.id == id; });
}

} // namespace

TEST(PackageCommandsTest, DeleteArgumentPackageRemovesPackageAndScrubsParserProjections) {
    sacm::AssuranceCasePackage package;
    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    sacm::Claim claim;
    claim.id = "G1";
    argument_package.claims.push_back(claim);
    package.argumentPackages.push_back(argument_package);

    parser::AssuranceCase model;
    parser::SacmElement projected;
    projected.id = "G1";
    projected.type = "claim";
    model.elements.push_back(projected);
    parser::SacmElement unrelated;
    unrelated.id = "G2";
    unrelated.type = "claim";
    model.elements.push_back(unrelated);

    std::string error;
    ASSERT_TRUE(core::DeleteArgumentPackage(package, model, "AP1", "", error)) << error;

    EXPECT_TRUE(package.argumentPackages.empty());
    // Projections of removed elements are scrubbed; unrelated elements survive.
    EXPECT_FALSE(ContainsId(model.elements, "G1"));
    EXPECT_TRUE(ContainsId(model.elements, "G2"));
}

TEST(PackageCommandsTest, DeleteConfidenceArgumentPackageClearsReferencingAcpLink) {
    sacm::AssuranceCasePackage package;
    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    core::acp::SetConfidenceArgumentPackage(argument_package, true);
    package.argumentPackages.push_back(argument_package);

    parser::AssuranceCase model;
    parser::AcpRecord acp;
    acp.id = "ACP1";
    acp.resolution_kind = "topGoalReference";
    acp.argument_package_id = "AP1";
    acp.top_goal_id = "CC1";
    model.acps.push_back(acp);

    std::string error;
    ASSERT_TRUE(core::DeleteArgumentPackage(package, model, "AP1", "", error)) << error;

    ASSERT_EQ(model.acps.size(), 1u);
    EXPECT_TRUE(model.acps.front().argument_package_id.empty());
    EXPECT_TRUE(model.acps.front().top_goal_id.empty());
}

TEST(PackageCommandsTest, DeleteArgumentPackageFailsForUnknownIdAndLeavesModelIntact) {
    sacm::AssuranceCasePackage package;
    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    package.argumentPackages.push_back(argument_package);
    parser::AssuranceCase model;

    std::string error;
    EXPECT_FALSE(core::DeleteArgumentPackage(package, model, "MISSING", "", error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(package.argumentPackages.size(), 1u);
}

TEST(PackageCommandsTest, DeleteArtifactPackageRemovesMatchingPackage) {
    sacm::AssuranceCasePackage package;
    sacm::ArtifactPackage artifact_package;
    artifact_package.id = "ARTP1";
    package.artifactPackages.push_back(artifact_package);

    std::string error;
    ASSERT_TRUE(core::DeleteArtifactPackage(package, "ARTP1", "", error)) << error;
    EXPECT_TRUE(package.artifactPackages.empty());

    EXPECT_FALSE(core::DeleteArtifactPackage(package, "MISSING", "", error));
    EXPECT_FALSE(error.empty());
}

TEST(PackageCommandsTest, RemoveArgumentPackageCommandRecordsIdentityPayload) {
    sacm::AssuranceCasePackage package;
    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    package.argumentPackages.push_back(argument_package);
    parser::AssuranceCase model;
    core::commands::CommandContext ctx{model, package};

    core::commands::RemoveArgumentPackageCommand command("AP1", "");
    core::audit::AuditEvent event;
    std::string error;
    ASSERT_TRUE(command.Apply(ctx, event, error)) << error;

    EXPECT_TRUE(package.argumentPackages.empty());
    EXPECT_EQ(event.event_type, "RemoveArgumentPackage");
    EXPECT_EQ(event.payload.at("package_id").get<std::string>(), "AP1");
}

TEST(PackageCommandsTest, RemoveArgumentPackageCommandRejectsEmptyIdentity) {
    sacm::AssuranceCasePackage package;
    parser::AssuranceCase model;
    core::commands::CommandContext ctx{model, package};

    core::commands::RemoveArgumentPackageCommand command("", "");
    core::audit::AuditEvent event;
    std::string error;

    EXPECT_FALSE(command.Apply(ctx, event, error));
    EXPECT_FALSE(error.empty());
}
