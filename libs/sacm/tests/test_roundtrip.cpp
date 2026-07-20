#include "sacm/compare/semantic_compare.h"
#include "sacm/io/xmi.h"
#include "sacm/metadata/element_kind.h"
#include "sacm/metadata/namespaces.h"
#include "sacm/model/document.h"
#include "sacm/validation/codes.h"
#include "sacm/validation/validate.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
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

bool has_code(const std::vector<sacm::validation::Diagnostic>& diagnostics,
              std::string_view code) {
    return std::ranges::any_of(diagnostics, [&](const auto& diagnostic) {
        return diagnostic.code == code;
    });
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

// A real EMF-produced file carries no xmi:id at all and refers between elements
// by containment path, with GSN types layered on SACM via xsi:type. Before this
// was supported such a file reported VALID while every element landed in
// preserved content -- the worst possible outcome, since nothing signalled that
// the argument had not been read.
TEST(Sacm23RoundTrip, SACM23_COMPAT_002_EmfGsnFileWithoutIdsParsesIntoSacmElements) {
    const std::filesystem::path path = fixture("interop-emf-gsn-noids-valid.sacm.xmi");
    const LoadResult loaded = sacm::io::load_xmi_file(path);
    ASSERT_TRUE(loaded.ok) << (loaded.diagnostics.empty() ? "load failed"
                                                          : loaded.diagnostics.front().message);

    // GSN types resolve to the SACM classes they specialize, per gsn.ecore's
    // own eSuperTypes: Goal -> Claim, Strategy -> ArgumentReasoning,
    // Solution -> ArtifactReference, SupportedBy -> AssertedInference.
    std::map<sacm::metadata::ElementKind, int> counts;
    loaded.document->for_each_element(
        [&](const sacm::model::SACMElement& element) { ++counts[element.kind()]; });
    EXPECT_EQ(counts[sacm::metadata::ElementKind::Claim], 2);
    EXPECT_EQ(counts[sacm::metadata::ElementKind::ArgumentReasoning], 1);
    EXPECT_EQ(counts[sacm::metadata::ElementKind::ArtifactReference], 1);
    EXPECT_EQ(counts[sacm::metadata::ElementKind::AssertedInference], 2);
    // gsn_:Module specializes ArgumentPackage.
    EXPECT_EQ(counts[sacm::metadata::ElementKind::ArgumentPackage], 1);

    // Containment-path references resolve: no dangling-reference diagnostics.
    EXPECT_FALSE(has_code(loaded.diagnostics, sacm::validation::codes::kRefDangling))
        << "EMF containment-path references did not resolve";
    EXPECT_TRUE(sacm::validation::validate(*loaded.document).empty());

    expect_semantic_roundtrip(path);
}

// SACM 2.3 determines no instance-document namespace, so ours is a project
// choice that appears in no other tool's files. A caller that must be read by a
// specific tool can name that tool's URI, and a document can be re-exported
// under the namespace it arrived with (decision #18).
TEST(Sacm23XmiConformance, SACM23_XMI_002_ExportNamespaceCanBeOverridden) {
    const LoadResult loaded = sacm::io::load_xmi_file(fixture("package-minimal-valid.sacm.xmi"));
    ASSERT_TRUE(loaded.ok);

    // Default export uses the pin.
    const sacm::io::SaveResult pinned = sacm::io::save_xmi_string(*loaded.document);
    ASSERT_TRUE(pinned.ok);
    EXPECT_NE(pinned.xml.find(sacm::metadata::namespaces::kSacm), std::string::npos);

    // Overridden export uses the caller's URI and nothing of the pin.
    const std::string other = "http://omg.sacm/2.3/assurancecase";
    const sacm::io::SaveResult overridden = sacm::io::save_xmi_string(
        *loaded.document, sacm::io::SaveOptions{.mode = Mode::Tolerant, .namespace_uri = other});
    ASSERT_TRUE(overridden.ok);
    EXPECT_NE(overridden.xml.find(other), std::string::npos);
    EXPECT_EQ(overridden.xml.find(sacm::metadata::namespaces::kSacm), std::string::npos);

    // The result is still loadable and semantically identical -- only the
    // namespace changed, not the argument.
    const LoadResult reloaded = sacm::io::load_xmi_string(overridden.xml);
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(*loaded.document, *reloaded.document).empty());
}

// A pre-2.3 document that happens to parse is not a 2.3 document. The reader
// reports the detected revision under its own diagnostic code, so a caller can
// distinguish "we do not recognize this namespace" (SACM-XMI-002) from "this is
// SACM 2.2" (SACM-XMI-008). Without this, older files loaded silently under a
// 2.3 conformance claim.
TEST(Sacm23RoundTrip, SACM23_COMPAT_001_OlderStandardRevisionIsDetectedAndReported) {
    // The EMF reference fixture declares http://omg.sacm/2.2/*.
    const LoadResult older =
        sacm::io::load_xmi_file(fixture("interop-emf-reference-dialect-valid.sacm.xmi"));
    ASSERT_TRUE(older.ok);
    EXPECT_EQ(older.source_version, sacm::metadata::namespaces::StandardVersion::V2_2);
    EXPECT_TRUE(has_code(older.diagnostics, sacm::validation::codes::kXmiOlderStandardVersion));
    // Recognized-but-old must not masquerade as unrecognized.
    EXPECT_FALSE(has_code(older.diagnostics, sacm::validation::codes::kXmiUnknownNamespace));

    // A 2.3 document reports 2.3 and raises no revision diagnostic.
    const LoadResult current = sacm::io::load_xmi_file(fixture("package-minimal-valid.sacm.xmi"));
    ASSERT_TRUE(current.ok);
    EXPECT_EQ(current.source_version, sacm::metadata::namespaces::StandardVersion::V2_3);
    EXPECT_FALSE(has_code(current.diagnostics, sacm::validation::codes::kXmiOlderStandardVersion));
}

// The namespace of the *current* GSN metamodel (v2.2) is scsc.acwg.gsn/2.0,
// which supersedes acwg.org/3.0/gsn despite the lower number. Files written
// against the current specification must be recognized, including the two
// Away* types that only exist in this revision.
TEST(Sacm23RoundTrip, SACM23_COMPAT_002_CurrentGsnNamespaceAndAwayTypesAreRecognized) {
    const std::filesystem::path path =
        fixture("interop-gsn20-current-namespace-valid.sacm.xmi");
    const LoadResult loaded = sacm::io::load_xmi_file(path);
    ASSERT_TRUE(loaded.ok) << (loaded.diagnostics.empty() ? "load failed"
                                                          : loaded.diagnostics.front().message);

    std::map<sacm::metadata::ElementKind, int> counts;
    loaded.document->for_each_element(
        [&](const sacm::model::SACMElement& element) { ++counts[element.kind()]; });
    // Goal + AwayAssumption + AwayJustification all specialize Claim.
    EXPECT_EQ(counts[sacm::metadata::ElementKind::Claim], 3);
    EXPECT_EQ(counts[sacm::metadata::ElementKind::ArtifactReference], 1);
    EXPECT_EQ(counts[sacm::metadata::ElementKind::AssertedInference], 1);

    // The namespace must be recognized as a SACM extension, not reported as a
    // foreign namespace -- that distinction is what makes the Context
    // diagnostic below actionable.
    EXPECT_FALSE(has_code(loaded.diagnostics, sacm::validation::codes::kXmiUnknownNamespace));

    // Context has no concrete SACM equivalent, so it is preserved and said so.
    const bool context_reported =
        std::ranges::any_of(loaded.diagnostics, [](const auto& diagnostic) {
            return diagnostic.message.find("Context") != std::string::npos &&
                   diagnostic.message.find("abstract") != std::string::npos;
        });
    EXPECT_TRUE(context_reported) << "GSN Context was dropped without explanation";
}

// GSN's SupportedBy runs opposite to SACM's AssertedInference: GSN writes
// source="the goal being supported", while SACM 2.3 clause 11.14 states that
// "the truth of Claim A -- the source -- is said to infer the truth of Claim B
// -- the target". Importing without swapping would leave every inference in the
// argument pointing the wrong way, which is silent reinterpretation of a safety
// argument rather than a formatting detail.
TEST(Sacm23RoundTrip, SACM23_COMPAT_002_GsnSupportedByEndpointsAreSwappedToSacmDirection) {
    const LoadResult loaded =
        sacm::io::load_xmi_file(fixture("interop-emf-gsn-noids-valid.sacm.xmi"));
    ASSERT_TRUE(loaded.ok);

    // Fixture: element .0 is goal G1, .1 is goal G2, and the first SupportedBy
    // is written GSN-style as source=G2(.1) target=G1(.0) -- "G2 is supported
    // by G1" in file order. After import the premise must be the SACM source.
    const sacm::model::SACMElement* g1 = nullptr;
    const sacm::model::SACMElement* g2 = nullptr;
    std::vector<const sacm::model::AssertedRelationship*> inferences;
    loaded.document->for_each_element([&](const sacm::model::SACMElement& element) {
        if (element.kind() == sacm::metadata::ElementKind::Claim) {
            const auto* named = dynamic_cast<const sacm::model::ModelElement*>(&element);
            if (named != nullptr && named->name().content == "G1") {
                g1 = &element;
            } else if (named != nullptr && named->name().content == "G2") {
                g2 = &element;
            }
        }
        if (const auto* rel = dynamic_cast<const sacm::model::AssertedRelationship*>(&element)) {
            if (element.kind() == sacm::metadata::ElementKind::AssertedInference) {
                inferences.push_back(rel);
            }
        }
    });
    ASSERT_NE(g1, nullptr);
    ASSERT_NE(g2, nullptr);
    ASSERT_FALSE(inferences.empty());

    // The file's first SupportedBy has source=.1 (G2), target=.0 (G1). Swapped,
    // SACM source must be G1 and target G2 -- the reverse of the file order.
    const sacm::model::AssertedRelationship* first = inferences.front();
    ASSERT_EQ(first->sources().size(), 1u);
    ASSERT_EQ(first->targets().size(), 1u);
    EXPECT_EQ(first->sources().front(), g1->id())
        << "GSN SupportedBy endpoints were not swapped: the inference points the wrong way";
    EXPECT_EQ(first->targets().front(), g2->id());
}

// The EMF reference implementation (github.com/wrwei/SACM, Apache-2.0) declares
// one namespace per metamodel package instead of one per document. SACM 2.3
// determines no instance namespace at all, so that dialect is no less conformant
// than our own pin -- and it is what the mainstream SACM tooling emits. It loads
// on the tolerant path (the default) like any other third-party dialect; strict
// load still demands our canonical namespace, and strict export normalizes to it.
TEST(Sacm23RoundTrip, SACM23_COMPAT_001_EmfReferenceDialectImportsAndNormalizes) {
    const std::filesystem::path path = fixture("interop-emf-reference-dialect-valid.sacm.xmi");
    const LoadResult loaded = sacm::io::load_xmi_file(path);
    ASSERT_TRUE(loaded.ok) << (loaded.diagnostics.empty() ? "load failed"
                                                          : loaded.diagnostics.front().message);
    // The per-package namespaces must not be reported as unknown: this dialect
    // is recognized, not merely tolerated as an unrecognized namespace.
    EXPECT_FALSE(has_code(loaded.diagnostics, sacm::validation::codes::kXmiUnknownNamespace));
    EXPECT_TRUE(loaded.source_namespace.starts_with(sacm::metadata::namespaces::kEmfReferencePrefix))
        << "source namespace not recorded as the reference dialect: " << loaded.source_namespace;

    // Strict load is our canonical-namespace path, so it declines the dialect
    // with a namespace diagnostic rather than a parse failure.
    const LoadResult strict = sacm::io::load_xmi_file(path, LoadOptions{.mode = Mode::Strict});
    EXPECT_FALSE(strict.ok);
    EXPECT_TRUE(has_code(strict.diagnostics, sacm::validation::codes::kXmiUnknownNamespace));

    // The prefixed xsi:type values resolve to the right SACM classes.
    ASSERT_EQ(loaded.document->roots().size(), 1u);
    EXPECT_NE(loaded.document->find(sacm::model::ElementId{"claim_top"}), nullptr);
    EXPECT_NE(loaded.document->find(sacm::model::ElementId{"inf_1"}), nullptr);

    // Strict export emits our single pinned namespace, not the source dialect.
    const sacm::io::SaveResult saved = sacm::io::save_xmi_string(*loaded.document);
    ASSERT_TRUE(saved.ok);
    EXPECT_NE(saved.xml.find(sacm::metadata::namespaces::kSacm), std::string::npos);
    EXPECT_EQ(saved.xml.find("http://omg.sacm/"), std::string::npos);

    expect_semantic_roundtrip(path);
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

// Strict save must emit neither Assurance Forge layout metadata nor
// compatibility-only extensions. The two halves are asserted together here so
// the requirement has one owning test rather than being inferred from the
// golden comparison in XMI-001 and the vendor fixture in COMPAT-001.
TEST(Sacm23XmiConformance, SACM23_XMI_004_StrictSaveOmitsLayoutAndRefusesCompatOnlyContent) {
    const Document document = build_minimal_document();
    const sacm::io::SaveResult saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);

    for (const std::string_view banned : {"layout", "Layout", "canvas", "Canvas", "position",
                                          "Goal", "Strategy", "Solution", "TreeItem", "ImGui"}) {
        EXPECT_EQ(saved.xml.find(banned), std::string::npos)
            << "strict output leaks layout/GSN vocabulary: " << banned;
    }

    // A document carrying compatibility-only vendor content must be refused by
    // strict save, not silently stripped of it.
    const LoadResult vendor = sacm::io::load_xmi_file(fixture("vendor-extension-valid.sacm.xmi"));
    ASSERT_TRUE(vendor.ok);
    const sacm::io::SaveResult strict = sacm::io::save_xmi_string(*vendor.document);
    EXPECT_FALSE(strict.ok);
    EXPECT_TRUE(has_code(strict.diagnostics, sacm::validation::codes::kXmiStrictSaveRefused));
}

}  // namespace
