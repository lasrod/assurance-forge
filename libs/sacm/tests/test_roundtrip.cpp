#include "sacm/compare/semantic_compare.h"
#include "sacm/io/xmi.h"
#include "sacm/model/document.h"
#include "sacm/validation/validate.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

using sacm::commands::CreateArgumentPackage;
using sacm::commands::CreateAssuranceCasePackage;
using sacm::commands::CreateClaim;
using sacm::io::LoadOptions;
using sacm::io::LoadResult;
using sacm::io::Mode;
using sacm::model::Document;
using sacm::model::ElementId;

std::filesystem::path fixture(std::string_view name) {
    return std::filesystem::path(SACM_TEST_DATA_DIR) / "sacm23" / name;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

// Loads (tolerant), saves strict, reloads, and expects semantic equality.
void expect_semantic_roundtrip(const std::filesystem::path& path) {
    const LoadResult first = sacm::io::load_xmi_file(path);
    ASSERT_TRUE(first.ok) << path.string() << ": "
                          << (first.diagnostics.empty() ? "" : first.diagnostics.front().message);
    const sacm::io::SaveResult saved = sacm::io::save_xmi_string(*first.document);
    ASSERT_TRUE(saved.ok);
    const LoadResult second = sacm::io::load_xmi_string(saved.xml);
    ASSERT_TRUE(second.ok) << (second.diagnostics.empty() ? saved.xml
                                                          : second.diagnostics.front().message);
    const auto differences = sacm::compare::semantic_compare(*first.document, *second.document);
    for (const auto& difference : differences) {
        ADD_FAILURE() << path.string() << " [" << difference.category << "] " << difference.path
                      << ": " << difference.message;
    }
}

TEST(Sacm23RoundTrip, SACM23_RT_001_StrictFixtureRoundTripsSemantically) {
    expect_semantic_roundtrip(fixture("package-minimal-valid.sacm.xmi"));
    expect_semantic_roundtrip(fixture("argument-claim-xsitype-valid.sacm.xmi"));
}

TEST(Sacm23RoundTrip, SACM23_RT_001_LegacyElementNameFixtureRoundTripsSemantically) {
    expect_semantic_roundtrip(fixture("argument-claim-elementname-valid.sacm.xmi"));
}

Document build_minimal_document() {
    Document document;
    EXPECT_TRUE(document
                    .apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"},
                                                      .name = "Created Case"})
                    .applied);
    EXPECT_TRUE(document
                    .apply(CreateArgumentPackage{.parent = ElementId{"acp_1"},
                                                 .id = ElementId{"argpkg_1"},
                                                 .name = "Main Argument"})
                    .applied);
    EXPECT_TRUE(document
                    .apply(CreateClaim{.parent = ElementId{"argpkg_1"},
                                       .id = ElementId{"claim_top"},
                                       .name = "Top Claim",
                                       .description = "The system is acceptably safe.",
                                       .language = "en"})
                    .applied);
    return document;
}

TEST(Sacm23RoundTrip, SACM23_RT_002_CreatedDocumentSavesReloadsAndSemanticallyMatches) {
    const Document document = build_minimal_document();
    const sacm::io::SaveResult saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);

    const LoadResult reloaded =
        sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok) << (reloaded.diagnostics.empty()
                                     ? saved.xml
                                     : reloaded.diagnostics.front().message);
    EXPECT_TRUE(sacm::validation::validate(*reloaded.document).empty());

    const auto differences = sacm::compare::semantic_compare(document, *reloaded.document);
    for (const auto& difference : differences) {
        ADD_FAILURE() << "[" << difference.category << "] " << difference.path << ": "
                      << difference.message;
    }
}

TEST(Sacm23XmiConformance, SACM23_XMI_001_SavesStrictSACM23ForCreatedDocument) {
    const Document document = build_minimal_document();
    const sacm::io::SaveResult saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);

    // Deterministic golden output; regenerate deliberately with
    // SACM_UPDATE_GOLDEN=1 and review the diff.
    const std::filesystem::path golden_path = fixture("golden/created-minimal.golden.sacm.xmi");
    if (std::getenv("SACM_UPDATE_GOLDEN") != nullptr) {
        std::filesystem::create_directories(golden_path.parent_path());
        std::ofstream out(golden_path, std::ios::binary);
        out << saved.xml;
        GTEST_SKIP() << "golden regenerated at " << golden_path.string();
    }
    ASSERT_TRUE(std::filesystem::exists(golden_path))
        << "golden missing; run with SACM_UPDATE_GOLDEN=1 to create it";
    EXPECT_EQ(saved.xml, read_file(golden_path));

    // Strict output declares the pinned namespaces.
    EXPECT_NE(saved.xml.find("http://www.omg.org/spec/SACM/20220301"), std::string::npos);
    EXPECT_NE(saved.xml.find("xmi:version=\"2.0\""), std::string::npos);
    // No layout or GSN vocabulary in strict output (SACM23-LIB-003).
    EXPECT_EQ(saved.xml.find("layout"), std::string::npos);
    EXPECT_EQ(saved.xml.find("Goal"), std::string::npos);
}

TEST(Sacm23XmiConformance, SACM23_XMI_002_ExportIsDeterministic) {
    const Document first = build_minimal_document();
    const Document second = build_minimal_document();
    const auto saved_first = sacm::io::save_xmi_string(first);
    const auto saved_second = sacm::io::save_xmi_string(second);
    ASSERT_TRUE(saved_first.ok);
    ASSERT_TRUE(saved_second.ok);
    EXPECT_EQ(saved_first.xml, saved_second.xml);
}

}  // namespace
