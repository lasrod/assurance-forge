#include "sacm/compare/semantic_compare.h"
#include "sacm/io/xmi.h"
#include "sacm/model/document.h"
#include "sacm/validation/codes.h"
#include "sacm/validation/validate.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

namespace {

using sacm::commands::CreateArtifactAsset;
using sacm::commands::CreateArtifactAssetRelationship;
using sacm::commands::CreateArtifactPackage;
using sacm::commands::CreateAssuranceCasePackage;
using sacm::io::LoadOptions;
using sacm::io::LoadResult;
using sacm::io::Mode;
using sacm::metadata::ElementKind;
using sacm::model::Document;
using sacm::model::ElementId;

std::filesystem::path fixture(std::string_view name) {
    return std::filesystem::path(SACM_TEST_DATA_DIR) / "sacm23" / name;
}

TEST(Sacm23Artifact, SACM23_ART_001_FullArtifactFixtureRoundTrips) {
    const LoadResult first = sacm::io::load_xmi_file(fixture("artifact-full-valid.sacm.xmi"),
                                                     LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(first.ok) << (first.diagnostics.empty() ? "" : first.diagnostics.front().message);
    const auto& document = *first.document;

    const auto* artifact = document.find_as<sacm::model::Artifact>(ElementId{"artifact_report"});
    ASSERT_NE(artifact, nullptr);
    EXPECT_EQ(artifact->version(), "2.1");
    EXPECT_EQ(artifact->date(), "2026-02-01");
    ASSERT_EQ(artifact->properties().size(), 1u);
    EXPECT_EQ(artifact->properties().front()->id().value(), "prop_confidentiality");

    const auto* activity = document.find_as<sacm::model::Activity>(ElementId{"activity_review"});
    ASSERT_NE(activity, nullptr);
    EXPECT_EQ(activity->start_time(), "2026-01-10");
    EXPECT_EQ(activity->end_time(), "2026-01-20");

    const auto* event = document.find_as<sacm::model::Event>(ElementId{"event_release"});
    ASSERT_NE(event, nullptr);
    EXPECT_EQ(event->date(), "2026-02-02");
    EXPECT_NE(document.find_as<sacm::model::Participant>(ElementId{"participant_assessor"}),
              nullptr);
    EXPECT_NE(document.find_as<sacm::model::Technique>(ElementId{"technique_stpa"}), nullptr);
    EXPECT_NE(document.find_as<sacm::model::Resource>(ElementId{"resource_lab"}), nullptr);

    const auto* relationship =
        document.find_as<sacm::model::ArtifactAssetRelationship>(ElementId{"rel_produced"});
    ASSERT_NE(relationship, nullptr);
    EXPECT_EQ(relationship->sources().front().value(), "activity_review");

    // ptc/22-03-13 alias spelling resolves to the specification-text class.
    const auto* alias =
        document.find_as<sacm::model::ArtifactAssetRelationship>(ElementId{"rel_alias"});
    ASSERT_NE(alias, nullptr);
    EXPECT_EQ(alias->kind(), ElementKind::ArtifactAssetRelationship);

    const auto* group = document.find_as<sacm::model::ArtifactGroup>(ElementId{"group_evidence"});
    ASSERT_NE(group, nullptr);
    EXPECT_EQ(group->artifact_elements().size(), 2u);

    EXPECT_TRUE(sacm::validation::validate(document).empty());
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult second = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(second.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *second.document).empty());
}

TEST(Sacm23Artifact, SACM23_ART_001_CreatesArtifactModelWithCommands) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "Case"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateArtifactPackage{
                        .parent = ElementId{"acp_1"}, .id = ElementId{"artpkg_1"}, .name = "Ev"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateArtifactAsset{.parent = ElementId{"artpkg_1"},
                                               .kind = ElementKind::Artifact,
                                               .id = ElementId{"artifact_1"},
                                               .name = "Report",
                                               .version = "1.0",
                                               .date = "2026-03-01"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateArtifactAsset{.parent = ElementId{"artpkg_1"},
                                               .kind = ElementKind::Activity,
                                               .id = ElementId{"activity_1"},
                                               .name = "Review",
                                               .start_time = "2026-02-01",
                                               .end_time = "2026-02-10"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateArtifactAsset{.parent = ElementId{"artifact_1"},
                                               .kind = ElementKind::Property,
                                               .id = ElementId{"prop_1"},
                                               .name = "confidentiality"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateArtifactAssetRelationship{
                        .parent = ElementId{"artpkg_1"},
                        .id = ElementId{"rel_1"},
                        .name = "producedBy",
                        .sources = {ElementId{"activity_1"}},
                        .targets = {ElementId{"artifact_1"}}})
                    .applied);

    EXPECT_TRUE(sacm::validation::validate(document).empty());
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded =
        sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *reloaded.document).empty());
}

TEST(Sacm23Artifact, SACM23_ART_001_RejectsRelationshipToNonAsset) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "Case"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateArtifactPackage{
                        .parent = ElementId{"acp_1"}, .id = ElementId{"artpkg_1"}, .name = "Ev"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateArtifactAsset{.parent = ElementId{"artpkg_1"},
                                               .kind = ElementKind::Artifact,
                                               .id = ElementId{"artifact_1"},
                                               .name = "Report"})
                    .applied);
    const auto result = document.apply(CreateArtifactAssetRelationship{
        .parent = ElementId{"artpkg_1"},
        .sources = {ElementId{"artifact_1"}},
        .targets = {ElementId{"artpkg_1"}},  // a package is not an ArtifactAsset
    });
    EXPECT_FALSE(result.applied);
    EXPECT_TRUE(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == sacm::validation::codes::kRefWrongType;
    }));
}

}  // namespace
