#include "sacm/compare/semantic_compare.h"
#include "sacm/io/xmi.h"
#include "sacm/model/document.h"
#include "sacm/validation/codes.h"
#include "sacm/validation/validate.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

namespace {

using sacm::commands::CreateArgumentPackage;
using sacm::commands::CreateArtifactAsset;
using sacm::commands::CreateArtifactAssetRelationship;
using sacm::commands::CreateArtifactPackage;
using sacm::commands::CreateArtifactReference;
using sacm::commands::CreateAssuranceCasePackage;
using sacm::commands::SetArtifactProvenance;
using sacm::commands::SetArtifactReferenceElements;
using sacm::commands::SetResourceLocation;
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
    const LoadResult first =
        sacm::io::load_xmi_file(fixture("artifact-full-valid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
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
    EXPECT_NE(document.find_as<sacm::model::Participant>(ElementId{"participant_assessor"}), nullptr);
    EXPECT_NE(document.find_as<sacm::model::Technique>(ElementId{"technique_stpa"}), nullptr);
    EXPECT_NE(document.find_as<sacm::model::Resource>(ElementId{"resource_lab"}), nullptr);

    const auto* relationship = document.find_as<sacm::model::ArtifactAssetRelationship>(ElementId{"rel_produced"});
    ASSERT_NE(relationship, nullptr);
    EXPECT_EQ(relationship->sources().front().value(), "activity_review");

    // ptc/22-03-13 alias spelling resolves to the specification-text class.
    const auto* alias = document.find_as<sacm::model::ArtifactAssetRelationship>(ElementId{"rel_alias"});
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
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "Case"}).applied);
    ASSERT_TRUE(
        document.apply(CreateArtifactPackage{.parent = ElementId{"acp_1"}, .id = ElementId{"artpkg_1"}, .name = "Ev"})
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
                    .apply(CreateArtifactAssetRelationship{.parent = ElementId{"artpkg_1"},
                                                           .id = ElementId{"rel_1"},
                                                           .name = "producedBy",
                                                           .sources = {ElementId{"activity_1"}},
                                                           .targets = {ElementId{"artifact_1"}}})
                    .applied);

    EXPECT_TRUE(sacm::validation::validate(document).empty());
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *reloaded.document).empty());
}

TEST(Sacm23Artifact, SACM23_ART_001_RejectsRelationshipToNonAsset) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "Case"}).applied);
    ASSERT_TRUE(
        document.apply(CreateArtifactPackage{.parent = ElementId{"acp_1"}, .id = ElementId{"artpkg_1"}, .name = "Ev"})
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
        .targets = {ElementId{"artpkg_1"}}, // a package is not an ArtifactAsset
    });
    EXPECT_FALSE(result.applied);
    EXPECT_TRUE(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == sacm::validation::codes::kRefWrongType;
    }));
}

// Clause 12.10 declares `location: Base::MultiLangString (composition)` -- the
// path or URL of the resource, and the only payload a Resource has.
// ptc/22-03-13 omits the attribute entirely, and the library followed the model,
// so a text-conformant `<location>` fell into preserved content and strict save
// then refused the document. Resolved toward the TEXT and recorded as a
// divergence in docs/sacm/sacm-2.3-metamodel-inventory.md (#337).
TEST(Sacm23Artifact, SACM23_ART_001_ResourceLocationRoundTrips) {
    const LoadResult first =
        sacm::io::load_xmi_file(fixture("artifact-full-valid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(first.ok) << (first.diagnostics.empty() ? "" : first.diagnostics.front().message);

    const auto* resource = first.document->find_as<sacm::model::Resource>(ElementId{"resource_lab"});
    ASSERT_NE(resource, nullptr);
    ASSERT_EQ(resource->location().values.size(), 2u) << "the resource's location was not read as a MultiLangString";
    const std::string* english = resource->location().find("en");
    ASSERT_NE(english, nullptr);
    EXPECT_EQ(*english, "file:///srv/hil/lab-bench-3");
    const std::string* japanese = resource->location().find("ja");
    ASSERT_NE(japanese, nullptr);
    EXPECT_EQ(*japanese, "file:///srv/hil/ラボベンチ3");

    // Strict save must emit it rather than refuse the document for carrying
    // preserved content -- which is what happened before the field existed.
    const auto saved = sacm::io::save_xmi_string(*first.document);
    ASSERT_TRUE(saved.ok) << (saved.diagnostics.empty() ? "" : saved.diagnostics.front().message);
    EXPECT_NE(saved.xml.find("file:///srv/hil/lab-bench-3"), std::string::npos)
        << "strict save dropped the resource location:\n"
        << saved.xml;

    const LoadResult second = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(second.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(*first.document, *second.document).empty());
}

// A Resource's location is the one thing it says, and until now a document
// could only carry one it was loaded with. Setting, replacing and clearing it
// by command, and the change surviving strict save, is what lets a tool record
// where a piece of evidence actually lives.
TEST(Sacm23Artifact, SACM23_ART_001_ResourceLocationIsSetAndClearedByCommand) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "Case"}).applied);
    ASSERT_TRUE(
        document.apply(CreateArtifactPackage{.parent = ElementId{"acp_1"}, .id = ElementId{"artpkg_1"}, .name = "Ev"})
            .applied);
    ASSERT_TRUE(document
                    .apply(CreateArtifactAsset{.parent = ElementId{"artpkg_1"},
                                               .kind = ElementKind::Resource,
                                               .id = ElementId{"res_1"},
                                               .name = "Test report"})
                    .applied);

    const auto set = document.apply(SetResourceLocation{
        .element = ElementId{"res_1"}, .location = "https://example.org/report.pdf", .language = "en"});
    ASSERT_TRUE(set.applied) << (set.diagnostics.empty() ? "" : set.diagnostics.front().message);
    const auto* resource = document.find_as<sacm::model::Resource>(ElementId{"res_1"});
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(resource->location().primary(), "https://example.org/report.pdf");

    // Replacing overwrites the same language rather than accumulating entries.
    ASSERT_TRUE(document
                    .apply(SetResourceLocation{
                        .element = ElementId{"res_1"}, .location = "file:///evidence/report-v2.pdf", .language = "en"})
                    .applied);
    ASSERT_EQ(resource->location().values.size(), 1u);
    EXPECT_EQ(resource->location().primary(), "file:///evidence/report-v2.pdf");

    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok) << (saved.diagnostics.empty() ? "" : saved.diagnostics.front().message);
    EXPECT_NE(saved.xml.find("file:///evidence/report-v2.pdf"), std::string::npos) << saved.xml;
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *reloaded.document).empty());

    // An empty location clears the entry; a cleared Resource is still a Resource.
    ASSERT_TRUE(
        document.apply(SetResourceLocation{.element = ElementId{"res_1"}, .location = "", .language = "en"}).applied);
    EXPECT_TRUE(resource->location().empty());

    // Only a Resource has a location: the operation is refused on an Artifact,
    // rather than silently doing nothing.
    ASSERT_TRUE(document
                    .apply(CreateArtifactAsset{.parent = ElementId{"artpkg_1"},
                                               .kind = ElementKind::Artifact,
                                               .id = ElementId{"artifact_1"},
                                               .name = "Report"})
                    .applied);
    const auto refused = document.apply(
        SetResourceLocation{.element = ElementId{"artifact_1"}, .location = "https://example.org", .language = "en"});
    EXPECT_FALSE(refused.applied);
    ASSERT_FALSE(refused.diagnostics.empty());
    EXPECT_EQ(refused.diagnostics.front().code, sacm::validation::codes::kCmdTargetNotFound);
}

// An ArtifactReference created without a cited artifact -- every GSN Solution
// imported from a diagram -- can be pointed at one later, and the citation
// round-trips. Pointing it at something that is not an ArtifactElement is
// refused, so a reference can never cite a Claim.
TEST(Sacm23Artifact, SACM23_ARG_001_ArtifactReferenceCitationIsSetByCommand) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "Case"}).applied);
    ASSERT_TRUE(
        document.apply(CreateArtifactPackage{.parent = ElementId{"acp_1"}, .id = ElementId{"artpkg_1"}, .name = "Ev"})
            .applied);
    ASSERT_TRUE(document
                    .apply(CreateArtifactAsset{.parent = ElementId{"artpkg_1"},
                                               .kind = ElementKind::Resource,
                                               .id = ElementId{"res_1"},
                                               .name = "Test report"})
                    .applied);
    ASSERT_TRUE(
        document.apply(CreateArgumentPackage{.parent = ElementId{"acp_1"}, .id = ElementId{"argpkg_1"}, .name = "Arg"})
            .applied);
    ASSERT_TRUE(document
                    .apply(CreateArtifactReference{
                        .parent = ElementId{"argpkg_1"}, .id = ElementId{"Sn1"}, .name = "Test report"})
                    .applied);
    const auto* reference = document.find_as<sacm::model::ArtifactReference>(ElementId{"Sn1"});
    ASSERT_NE(reference, nullptr);
    ASSERT_TRUE(reference->referenced_artifact_elements().empty());

    const auto linked = document.apply(SetArtifactReferenceElements{
        .element = ElementId{"Sn1"}, .referenced_artifact_elements = {ElementId{"res_1"}}});
    ASSERT_TRUE(linked.applied) << (linked.diagnostics.empty() ? "" : linked.diagnostics.front().message);
    ASSERT_EQ(reference->referenced_artifact_elements().size(), 1u);
    EXPECT_EQ(reference->referenced_artifact_elements().front().value(), "res_1");

    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok) << (saved.diagnostics.empty() ? "" : saved.diagnostics.front().message);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *reloaded.document).empty());

    // An id that resolves to nothing is refused, and a refused edit leaves the
    // citation as it was. (Citing another argument element is legal -- an
    // ArgumentationElement is an ArtifactElement, which is how modules are
    // cited -- so the unresolved id is the refusal this test needs.)
    const auto refused = document.apply(SetArtifactReferenceElements{
        .element = ElementId{"Sn1"}, .referenced_artifact_elements = {ElementId{"res_missing"}}});
    EXPECT_FALSE(refused.applied);
    EXPECT_EQ(reference->referenced_artifact_elements().front().value(), "res_1") << "a refused edit changed the model";

    // An empty list withdraws the citation.
    ASSERT_TRUE(
        document.apply(SetArtifactReferenceElements{.element = ElementId{"Sn1"}, .referenced_artifact_elements = {}})
            .applied);
    EXPECT_TRUE(reference->referenced_artifact_elements().empty());
}

// An Artifact's version and date could only be given at creation. Setting,
// replacing and clearing them by command, and the edit surviving strict save,
// is what lets a tool keep an evidence record current.
TEST(Sacm23Artifact, SACM23_ART_001_ArtifactProvenanceIsSetByCommand) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "Case"}).applied);
    ASSERT_TRUE(
        document.apply(CreateArtifactPackage{.parent = ElementId{"acp_1"}, .id = ElementId{"artpkg_1"}, .name = "Ev"})
            .applied);
    ASSERT_TRUE(document
                    .apply(CreateArtifactAsset{.parent = ElementId{"artpkg_1"},
                                               .kind = ElementKind::Artifact,
                                               .id = ElementId{"artifact_1"},
                                               .name = "Test report"})
                    .applied);
    const auto* artifact = document.find_as<sacm::model::Artifact>(ElementId{"artifact_1"});
    ASSERT_NE(artifact, nullptr);
    ASSERT_TRUE(artifact->version().empty());

    const auto set = document.apply(
        SetArtifactProvenance{.element = ElementId{"artifact_1"}, .version = "rev B", .date = "2026-06-01"});
    ASSERT_TRUE(set.applied) << (set.diagnostics.empty() ? "" : set.diagnostics.front().message);
    EXPECT_EQ(artifact->version(), "rev B");
    EXPECT_EQ(artifact->date(), "2026-06-01");

    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok) << (saved.diagnostics.empty() ? "" : saved.diagnostics.front().message);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *reloaded.document).empty());

    // Cleared fields are absent, not empty strings that print.
    ASSERT_TRUE(
        document.apply(SetArtifactProvenance{.element = ElementId{"artifact_1"}, .version = "", .date = ""}).applied);
    EXPECT_TRUE(artifact->version().empty());
    EXPECT_TRUE(artifact->date().empty());

    // Only an Artifact carries provenance: refused on a Resource.
    ASSERT_TRUE(document
                    .apply(CreateArtifactAsset{.parent = ElementId{"artpkg_1"},
                                               .kind = ElementKind::Resource,
                                               .id = ElementId{"res_1"},
                                               .name = "Lab"})
                    .applied);
    const auto refused =
        document.apply(SetArtifactProvenance{.element = ElementId{"res_1"}, .version = "1", .date = "2026"});
    EXPECT_FALSE(refused.applied);
    ASSERT_FALSE(refused.diagnostics.empty());
    EXPECT_EQ(refused.diagnostics.front().code, sacm::validation::codes::kCmdTargetNotFound);
}

} // namespace
