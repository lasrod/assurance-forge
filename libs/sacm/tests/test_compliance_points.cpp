// SACM 2.3 Clause 2 compliance points: the units of interchange.
//
// Clause 2.1 defines five compliance points, and 2.2-2.6 each name the element
// that "as a unit of interchange shall be" that point's top object. Software
// conforming at a point must import and export XMI documents rooted at it.
//
//   2.2 Argumentation Model    -> Argumentation::ArgumentPackage
//   2.3 Artifact Model         -> Artifact::ArtifactPackage
//   2.4 Assurance Case Model   -> SACM::AssuranceCasePackage   (mandatory)
//   2.5 Terminology Model      -> Terminology::TerminologyPackage
//   2.6 SACM UML Profile       -> not claimed; see docs/sacm/sacm-compliance-points.md
//
// Before these tests the library handled all four roots -- `load_xmi_file`'s own
// docstring says so, `read_root` accepts any package kind, and the writer emits
// `other_roots` -- but **every committed fixture was rooted at
// AssuranceCasePackage**. Three of the four units of interchange therefore had
// no evidence at all. The capability was real and the claim was unsupported,
// which is the gap this file closes rather than a defect it fixes.
//
// Each test asserts the round trip AND the root element of the output, because a
// writer that quietly promoted an ArgumentPackage into an AssuranceCasePackage
// would still compare equal semantically while emitting a document that is no
// longer the clause 2.2 unit of interchange.

#include "sacm/compare/semantic_compare.h"
#include "sacm/io/xmi.h"
#include "sacm/metadata/element_kind.h"
#include "sacm/model/document.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace {

using sacm::io::LoadOptions;
using sacm::io::LoadResult;
using sacm::io::Mode;
using sacm::io::SaveResult;
using sacm::metadata::ElementKind;

std::filesystem::path fixture(std::string_view name) {
    return std::filesystem::path(SACM_TEST_DATA_DIR) / "sacm23" / name;
}

// The local name of the serialized root element, which is what the clause
// constrains. Read off the emitted XML rather than the model: the question is
// what a consuming tool actually receives.
std::string serialized_root_name(const std::string& xml) {
    const std::size_t open = xml.find("<sacm:");
    if (open == std::string::npos) {
        return "<no sacm-prefixed root element>";
    }
    const std::size_t start = open + std::string_view("<sacm:").size();
    const std::size_t end = xml.find_first_of(" \t\r\n/>", start);
    return xml.substr(start, end - start);
}

void expect_interchange_unit(std::string_view fixture_name, std::string_view expected_root, ElementKind expected_kind) {
    // Strict is the mode that matters. A tolerant load accepts roots the standard
    // does not permit, so passing tolerantly would prove nothing about conformance.
    const LoadResult loaded = sacm::io::load_xmi_file(fixture(fixture_name), LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(loaded.ok) << "strict load failed for " << fixture_name
                           << (loaded.diagnostics.empty() ? "" : ": " + loaded.diagnostics.front().message);
    ASSERT_TRUE(loaded.document.has_value());

    // Non-vacuity: a fixture that parsed into an empty document would satisfy
    // every assertion below. Assert the package arrived, under the kind the
    // clause names, before asserting anything about it.
    const bool mandatory_point = expected_kind == ElementKind::AssuranceCasePackage;
    const std::size_t root_count =
        mandatory_point ? loaded.document->roots().size() : loaded.document->other_roots().size();
    ASSERT_EQ(root_count, 1u) << "expected exactly one root package in " << fixture_name;
    if (!mandatory_point) {
        EXPECT_EQ(loaded.document->other_roots().front()->kind(), expected_kind);
    }

    const SaveResult saved = sacm::io::save_xmi_string(*loaded.document, sacm::io::SaveOptions{.mode = Mode::Strict});
    ASSERT_TRUE(saved.ok) << "strict save failed for " << fixture_name;

    // Export must keep the root the clause names. Promoting or wrapping it would
    // still round-trip semantically and would no longer be this unit of interchange.
    EXPECT_EQ(serialized_root_name(saved.xml), expected_root)
        << "exported root is not the clause's unit of interchange for " << fixture_name;

    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok) << "strict reload of the exported document failed for " << fixture_name;
    ASSERT_TRUE(reloaded.document.has_value());
    EXPECT_TRUE(sacm::compare::semantic_compare(*loaded.document, *reloaded.document).empty())
        << "round trip changed the model for " << fixture_name;
}

TEST(Sacm23CompliancePoints, SACM23_CP_002_ArgumentPackageIsAnInterchangeUnit) {
    expect_interchange_unit("argumentation-only-root-valid.sacm.xmi", "ArgumentPackage", ElementKind::ArgumentPackage);
}

TEST(Sacm23CompliancePoints, SACM23_CP_003_ArtifactPackageIsAnInterchangeUnit) {
    expect_interchange_unit("artifact-only-root-valid.sacm.xmi", "ArtifactPackage", ElementKind::ArtifactPackage);
}

TEST(Sacm23CompliancePoints, SACM23_CP_004_TerminologyPackageIsAnInterchangeUnit) {
    expect_interchange_unit(
        "terminology-only-root-valid.sacm.xmi", "TerminologyPackage", ElementKind::TerminologyPackage);
}

// Clause 2.4 is the mandatory point and most of the suite exercises it already.
// It is pinned here too so all four roots are asserted the same way in one
// place -- the comparison is the point, and a reader checking whether the three
// optional points are held to the same standard should not have to go looking.
TEST(Sacm23CompliancePoints, SACM23_CP_001_AssuranceCasePackageIsTheMandatoryInterchangeUnit) {
    expect_interchange_unit(
        "package-minimal-valid.sacm.xmi", "AssuranceCasePackage", ElementKind::AssuranceCasePackage);
}

} // namespace
