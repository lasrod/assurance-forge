#include "sacm/compare/semantic_compare.h"
#include "sacm/compat/preserve.h"
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

// The same extension type under our own pinned namespace and a different
// prefix, so the strict-mode assertions below are not confounded by the GSN
// fixture's dialect namespace and missing ids (strict rejects those first, and
// never reaches the element).
// The same shape, but the extension prefix is declared ON THE CHILD rather than
// on an ancestor. Namespace scoping makes this equivalent XML, so it must behave
// identically -- resolving `xsi:type` without pushing the child's own scope would
// leave the prefix unresolvable, miss the extension branch, and drop the subtree.
constexpr std::string_view kLocallyDeclaredExtensionDocument =
    R"(<?xml version="1.0" encoding="UTF-8"?>)"
    R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
    R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
    R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" )"
    R"(xmi:version="2.0" xmi:id="acp_1">)"
    R"(<name content="Locally Declared Case"/><argumentPackage xmi:id="argpkg_1">)"
    R"(<argumentElement xmlns:gsn="http://scsc.acwg.gsn/2.0" xsi:type="gsn:Context" )"
    R"(xmi:id="ctx_1"><name content="C1"/>)"
    R"(</argumentElement></argumentPackage></sacm:AssuranceCasePackage>)";

constexpr std::string_view kStrictNamespaceExtensionDocument =
    R"(<?xml version="1.0" encoding="UTF-8"?>)"
    R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
    R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
    R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" )"
    R"(xmlns:gsn="http://scsc.acwg.gsn/2.0" xmi:version="2.0" xmi:id="acp_1">)"
    R"(<name content="Strict Namespace Case"/><argumentPackage xmi:id="argpkg_1">)"
    R"(<argumentElement xsi:type="gsn:Context" xmi:id="ctx_1"><name content="C1"/>)"
    R"(</argumentElement></argumentPackage></sacm:AssuranceCasePackage>)";

// A GSN `Context` specializes the *abstract* SACM ArgumentAsset, so there is no
// concrete class for the reader to build. It said as much -- "preserved as
// compatibility content" -- and then dropped the element: the `std::nullopt`
// that meant "preserve this subtree" was indistinguishable from the one that
// meant "there is no xsi:type here", so the caller fell through to name-based
// kind inference, failed, and returned having preserved nothing. A diagnostic
// that claims preservation while the element disappears is worse than silence,
// because it reads as evidence that nothing was lost.
TEST(Sacm23RoundTrip, SACM23_COMPAT_002_ExtensionTypedElementIsPreservedNotDropped) {
    const std::filesystem::path path = fixture("interop-gsn20-current-namespace-valid.sacm.xmi");
    ASSERT_NE(read_file(path).find(R"(xsi:type="gsn_:Context")"), std::string::npos)
        << "fixture no longer carries an extension type with an abstract SACM supertype; "
           "the test measures nothing";

    const LoadResult loaded = sacm::io::load_xmi_file(path);
    ASSERT_TRUE(loaded.ok) << (loaded.diagnostics.empty() ? "load failed"
                                                          : loaded.diagnostics.front().message);

    // The subtree lands verbatim on its parent, so nothing about it is
    // reinterpreted -- not its type, not its name, not its children.
    int preserved_fragments = 0;
    loaded.document->for_each_element([&](const sacm::model::SACMElement& element) {
        for (const sacm::model::PreservedFragment& fragment : element.preserved_content()) {
            if (fragment.xml.find("gsn_:Context") != std::string::npos) {
                ++preserved_fragments;
                EXPECT_NE(fragment.xml.find(R"(content="C1")"), std::string::npos)
                    << "the fragment was truncated to its opening tag: " << fragment.xml;
            }
        }
    });
    EXPECT_EQ(preserved_fragments, 1) << "the GSN Context subtree was dropped, not preserved";

    // A compatibility save re-emits it; without this the preservation is
    // internal bookkeeping the user never gets back.
    const sacm::io::SaveResult compat =
        sacm::io::save_xmi_string(*loaded.document, sacm::io::SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(compat.ok);
    EXPECT_NE(compat.xml.find(R"(xsi:type="gsn_:Context")"), std::string::npos)
        << "compatibility output dropped the preserved extension element:\n"
        << compat.xml;
    EXPECT_NE(compat.xml.find(R"(content="C1")"), std::string::npos) << compat.xml;

    // Strict save must refuse the document rather than quietly writing it
    // without the fragment (SACM23-XMI-004).
    const sacm::io::SaveResult strict = sacm::io::save_xmi_string(*loaded.document);
    EXPECT_FALSE(strict.ok);
    EXPECT_TRUE(has_code(strict.diagnostics, sacm::validation::codes::kXmiStrictSaveRefused));

    // Preservation is the *tolerant* answer. Strict must keep rejecting: a
    // strict document carries no compatibility content, so accepting one and
    // preserving the element would let strict output be produced from input
    // strict does not admit. The prefix here is `gsn`, not the fixture's
    // `gsn_` -- the reader resolves the namespace, never the prefix spelling.
    const LoadResult strict_load = sacm::io::load_xmi_string(kStrictNamespaceExtensionDocument,
                                                             LoadOptions{.mode = Mode::Strict});
    EXPECT_FALSE(strict_load.ok) << "strict accepted a type it cannot represent";
    EXPECT_TRUE(std::ranges::any_of(strict_load.diagnostics, [](const auto& diagnostic) {
        return diagnostic.requirement_id == "SACM23-COMPAT-002" &&
               diagnostic.severity == sacm::validation::Severity::Error;
    })) << "strict rejection was not attributed to the extension type";

    // The same document on the tolerant path preserves it, prefix and all.
    const LoadResult tolerant = sacm::io::load_xmi_string(kStrictNamespaceExtensionDocument);
    ASSERT_TRUE(tolerant.ok);
    const sacm::io::SaveResult tolerant_saved =
        sacm::io::save_xmi_string(*tolerant.document, sacm::io::SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(tolerant_saved.ok);
    EXPECT_NE(tolerant_saved.xml.find(R"(xsi:type="gsn:Context")"), std::string::npos)
        << tolerant_saved.xml;
}

// The same loss that SACM23-COMPAT-001 fixed for vendor prefixes applies to
// extension prefixes the moment extension fragments actually reach preserved
// content: a fragment re-emitted under an undeclared prefix is not
// namespace-well-formed, and the next load resolves the prefix to no namespace.
TEST(Sacm23RoundTrip, SACM23_COMPAT_002_PreservedExtensionFragmentDeclaresItsNamespace) {
    const LoadResult loaded =
        sacm::io::load_xmi_file(fixture("interop-gsn20-current-namespace-valid.sacm.xmi"));
    ASSERT_TRUE(loaded.ok) << (loaded.diagnostics.empty() ? "load failed"
                                                          : loaded.diagnostics.front().message);

    const auto& declarations = loaded.document->foreign_namespaces();
    const auto gsn = declarations.find("gsn_");
    ASSERT_NE(gsn, declarations.end())
        << "the prefix the preserved fragment uses was not recorded for re-declaration";
    EXPECT_EQ(gsn->second, "http://scsc.acwg.gsn/2.0");

    const sacm::io::SaveResult compat =
        sacm::io::save_xmi_string(*loaded.document, sacm::io::SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(compat.ok);
    EXPECT_NE(compat.xml.find(R"(xmlns:gsn_="http://scsc.acwg.gsn/2.0")"), std::string::npos)
        << "compatibility output re-emits the gsn_ prefix without declaring it:\n"
        << compat.xml;

    // The declaration exists to make the fragment readable again, so a reload
    // must classify it exactly as the first load did.
    const LoadResult reloaded = sacm::io::load_xmi_string(compat.xml);
    ASSERT_TRUE(reloaded.ok) << (reloaded.diagnostics.empty() ? compat.xml
                                                             : reloaded.diagnostics.front().message);
    const auto differences = sacm::compare::semantic_compare(*loaded.document, *reloaded.document);
    for (const auto& difference : differences) {
        ADD_FAILURE() << "[" << difference.category << "] " << difference.path << ": "
                      << difference.message;
    }
}

// Namespace scoping: an extension prefix declared on the element itself is
// equivalent XML to one declared on an ancestor, so it must resolve the same way.
// Reading `xsi:type` without pushing the child's own scope left the prefix
// unresolvable, so the type missed the extension branch and the subtree was
// dropped -- the same silent loss this preservation path exists to prevent.
TEST(Sacm23RoundTrip, SACM23_COMPAT_002_LocallyDeclaredExtensionPrefixIsPreserved) {
    const LoadResult loaded = sacm::io::load_xmi_string(kLocallyDeclaredExtensionDocument);
    ASSERT_TRUE(loaded.ok);
    ASSERT_EQ(loaded.document->roots().size(), 1u);

    const sacm::io::SaveResult saved =
        sacm::io::save_xmi_string(*loaded.document, sacm::io::SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(saved.ok);
    EXPECT_NE(saved.xml.find(R"(xsi:type="gsn:Context")"), std::string::npos)
        << "the locally-declared extension subtree was dropped: " << saved.xml;
    EXPECT_NE(saved.xml.find(R"(xmlns:gsn="http://scsc.acwg.gsn/2.0")"), std::string::npos)
        << "the preserved fragment's prefix was not declared: " << saved.xml;
}

// Preserving on one save is not preserving: the loss shows up on the second
// pass, when the re-emitted fragment has to be read back through the same
// reader that produced it.
TEST(Sacm23RoundTrip, SACM23_COMPAT_002_PreservedExtensionContentSurvivesTwoRoundTrips) {
    const sacm::io::SaveOptions compat_options{.mode = Mode::Tolerant};
    const LoadResult first =
        sacm::io::load_xmi_file(fixture("interop-gsn20-current-namespace-valid.sacm.xmi"));
    ASSERT_TRUE(first.ok);

    const sacm::io::SaveResult once = sacm::io::save_xmi_string(*first.document, compat_options);
    ASSERT_TRUE(once.ok);
    const LoadResult second = sacm::io::load_xmi_string(once.xml);
    ASSERT_TRUE(second.ok) << (second.diagnostics.empty() ? once.xml
                                                          : second.diagnostics.front().message);

    int preserved_fragments = 0;
    second.document->for_each_element([&](const sacm::model::SACMElement& element) {
        for (const sacm::model::PreservedFragment& fragment : element.preserved_content()) {
            if (fragment.xml.find("gsn_:Context") != std::string::npos) {
                ++preserved_fragments;
            }
        }
    });
    EXPECT_EQ(preserved_fragments, 1) << "the re-emitted fragment was lost when read back:\n"
                                      << once.xml;

    const sacm::io::SaveResult twice = sacm::io::save_xmi_string(*second.document, compat_options);
    ASSERT_TRUE(twice.ok);
    EXPECT_EQ(once.xml, twice.xml) << "compatibility output is not idempotent";
    EXPECT_NE(twice.xml.find(R"(xsi:type="gsn_:Context")"), std::string::npos)
        << "the preserved fragment did not survive a second round trip:\n"
        << twice.xml;
    EXPECT_NE(twice.xml.find(R"(content="C1")"), std::string::npos) << twice.xml;
}

// A relationship endpoint that points at an element only preserved
// compatibility content can carry is not a broken reference: the target IS in
// the file, the reader simply cannot type it. Reporting SACM-REF-001 there
// states the opposite of what happened -- and because that code is an Error, it
// makes every GSN file containing a Context fail validation even though its
// argument is intact.
constexpr std::string_view kReferenceToPreservedElementDocument =
    R"(<?xml version="1.0" encoding="UTF-8"?>)"
    R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
    R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
    R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" )"
    R"(xmlns:gsn="http://scsc.acwg.gsn/2.0" xmi:version="2.0" xmi:id="acp_1">)"
    R"(<name content="Context Reference Case"/>)"
    R"(<argumentPackage xmi:id="ap_1"><name content="Main"/>)"
    R"(<argumentElement xsi:type="sacm:Claim" xmi:id="g1"><name content="G1"/></argumentElement>)"
    R"(<argumentElement xsi:type="gsn:Context" xmi:id="c1"><name content="C1"/></argumentElement>)"
    R"(<argumentElement xsi:type="sacm:AssertedContext" xmi:id="ctx_1" source="g1" )"
    R"(target="c1"/></argumentPackage></sacm:AssuranceCasePackage>)";

TEST(Sacm23RoundTrip, SACM23_COMPAT_002_ReferenceToPreservedElementIsNotDangling) {
    const LoadResult loaded = sacm::io::load_xmi_string(kReferenceToPreservedElementDocument);
    ASSERT_TRUE(loaded.ok) << (loaded.diagnostics.empty() ? "load failed"
                                                          : loaded.diagnostics.front().message);

    // Precondition: the Context really is preserved rather than parsed, so the
    // reference really does point outside the index.
    ASSERT_TRUE(loaded.document->find(ElementId{"c1"}) == nullptr)
        << "fixture no longer preserves the Context; the test measures nothing";
    EXPECT_TRUE(loaded.document->has_preserved_element(ElementId{"c1"}))
        << "the preserved element's id was not recorded, so nothing can tell a reference into "
           "compatibility content from a reference into nothing";

    // The distinction the codes have to carry: not missing (SACM-REF-001),
    // but untyped (SACM-REF-003).
    EXPECT_FALSE(has_code(loaded.diagnostics, sacm::validation::codes::kRefDangling))
        << "a reference into preserved content was reported as dangling";
    EXPECT_TRUE(has_code(loaded.diagnostics, sacm::validation::codes::kRefPreservedTarget));

    const std::vector<sacm::validation::Diagnostic> problems =
        sacm::validation::validate(*loaded.document);
    EXPECT_FALSE(has_code(problems, sacm::validation::codes::kRefDangling))
        << "validation still calls the preserved target missing";
    for (const auto& problem : problems) {
        EXPECT_NE(problem.severity, sacm::validation::Severity::Error)
            << "[" << problem.code << "] " << problem.message;
    }
    EXPECT_TRUE(has_code(problems, sacm::validation::codes::kRefPreservedTarget))
        << "validation gave no signal at all that the endpoint is untyped; silence would be "
           "worse than the wrong code";

    // The endpoint itself must survive: a compatibility save re-emits both the
    // preserved element and the reference to it, and the pair still holds
    // together on the way back in.
    const sacm::io::SaveOptions compat_options{.mode = Mode::Tolerant};
    const sacm::io::SaveResult saved =
        sacm::io::save_xmi_string(*loaded.document, compat_options);
    ASSERT_TRUE(saved.ok);
    EXPECT_NE(saved.xml.find(R"(target="c1")"), std::string::npos)
        << "the reference to the preserved element was dropped on save:\n"
        << saved.xml;

    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml);
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(reloaded.document->has_preserved_element(ElementId{"c1"}));
    EXPECT_FALSE(has_code(sacm::validation::validate(*reloaded.document),
                          sacm::validation::codes::kRefDangling))
        << "the endpoint became dangling once the saved file was read back";
    const auto differences = sacm::compare::semantic_compare(*loaded.document, *reloaded.document);
    for (const auto& difference : differences) {
        ADD_FAILURE() << "[" << difference.category << "] " << difference.path << ": "
                      << difference.message;
    }
}

// The boundary of the REF-001 -> REF-003 downgrade. A document with BOTH a
// preserved target and a genuinely missing one must report one code each: if
// the downgrade leaked, a real broken reference would be demoted to a warning
// and the document would validate. Also pins that a vendor attribute merely
// spelled `...:id` does not confer preserved-element identity.
constexpr std::string_view kMixedTargetDocument =
    R"(<?xml version="1.0" encoding="UTF-8"?>)"
    R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
    R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
    R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" )"
    R"(xmlns:gsn="http://scsc.acwg.gsn/2.0" xmlns:acme="http://acme.example/t" )"
    R"(xmi:version="2.0" xmi:id="acp_1"><name content="Mixed Targets"/>)"
    R"(<argumentPackage xmi:id="ap_1"><name content="Main"/>)"
    R"(<argumentElement xsi:type="sacm:Claim" xmi:id="g1"><name content="G1"/></argumentElement>)"
    R"(<argumentElement xsi:type="gsn:Context" xmi:id="c1" acme:id="ghost_1">)"
    R"(<name content="C1"/></argumentElement>)"
    R"(<argumentElement xsi:type="sacm:AssertedContext" xmi:id="ctx_1" source="g1" )"
    R"(target="c1"/>)"
    R"(<argumentElement xsi:type="sacm:AssertedContext" xmi:id="ctx_2" source="g1" )"
    R"(target="nowhere_1"/>)"
    R"(<argumentElement xsi:type="sacm:AssertedContext" xmi:id="ctx_3" source="g1" )"
    R"(target="ghost_1"/></argumentPackage></sacm:AssuranceCasePackage>)";

TEST(Sacm23RoundTrip, SACM23_COMPAT_002_PreservedTargetDowngradeDoesNotMaskRealDangling) {
    const LoadResult loaded = sacm::io::load_xmi_string(kMixedTargetDocument);
    ASSERT_TRUE(loaded.ok);
    ASSERT_TRUE(loaded.document->has_preserved_element(ElementId{"c1"}))
        << "fixture no longer preserves the Context; the test measures nothing";

    // A vendor attribute that merely spells `id` must not confer identity:
    // `ghost_1` exists only as `acme:id`, so a reference to it is still broken.
    EXPECT_FALSE(loaded.document->has_preserved_element(ElementId{"ghost_1"}))
        << "a foreign attribute whose local name is 'id' was treated as the element's XMI "
           "identity, which would demote a genuinely dangling reference to a warning";

    const std::vector<sacm::validation::Diagnostic> problems =
        sacm::validation::validate(*loaded.document);
    const auto code_for = [&problems](std::string_view target) -> std::string {
        for (const auto& problem : problems) {
            if (problem.message.find(target) != std::string::npos) {
                return problem.code;
            }
        }
        return {};
    };
    EXPECT_EQ(code_for("c1"), std::string(sacm::validation::codes::kRefPreservedTarget));
    EXPECT_EQ(code_for("nowhere_1"), std::string(sacm::validation::codes::kRefDangling));
    EXPECT_EQ(code_for("ghost_1"), std::string(sacm::validation::codes::kRefDangling));

    // The other half of the identity rule: XMI identity must be recognized by
    // NAMESPACE, not by the prefix happening to be spelled `xmi`. The EMF
    // dialects in the corpus declare the older `http://www.omg.org/XMI`; if
    // that URI is not accepted here, every EMF file that binds it to some other
    // prefix loses preserved-element identity and its references go back to
    // being reported as broken.
    const LoadResult legacy_xmi_prefix = sacm::io::load_xmi_string(
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
        R"(xmlns:x="http://www.omg.org/XMI" )"
        R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" )"
        R"(xmlns:gsn="http://scsc.acwg.gsn/2.0" x:version="2.0" x:id="acp_9">)"
        R"(<name content="Legacy XMI Prefix"/><argumentPackage x:id="ap_9"><name content="M"/>)"
        R"(<argumentElement xsi:type="sacm:Claim" x:id="g9"><name content="G9"/></argumentElement>)"
        R"(<argumentElement xsi:type="gsn:Context" x:id="c9"><name content="C9"/></argumentElement>)"
        R"(<argumentElement xsi:type="sacm:AssertedContext" x:id="ctx_9" source="g9" )"
        R"(target="c9"/></argumentPackage></sacm:AssuranceCasePackage>)");
    ASSERT_TRUE(legacy_xmi_prefix.ok);
    EXPECT_TRUE(legacy_xmi_prefix.document->has_preserved_element(ElementId{"c9"}))
        << "XMI identity was matched on the prefix spelling rather than the namespace, so an EMF "
           "document binding the legacy XMI URI to another prefix loses preserved-element identity";
    EXPECT_FALSE(has_code(sacm::validation::validate(*legacy_xmi_prefix.document),
                          sacm::validation::codes::kRefDangling))
        << "the reference into preserved content was reported as broken again";

    // The real breakage still fails the document.
    EXPECT_TRUE(std::ranges::any_of(problems, [](const auto& problem) {
        return problem.severity == sacm::validation::Severity::Error;
    })) << "a document with a genuinely dangling reference validated clean";

    // Strict mode preserves nothing, so every unresolved target is plain
    // SACM-REF-001 there -- the downgrade is a tolerant-path concept only.
    const LoadResult strict =
        sacm::io::load_xmi_string(kMixedTargetDocument, LoadOptions{.mode = Mode::Strict});
    EXPECT_FALSE(has_code(strict.diagnostics, sacm::validation::codes::kRefPreservedTarget))
        << "strict mode produced a preserved-target diagnostic despite preserving nothing";

    // The prefix spelling `xmi` is accepted only when UNDECLARED. A document
    // that rebinds `xmlns:xmi` to a non-XMI namespace must not be able to
    // confer serialization identity on its own terms -- otherwise it could
    // downgrade a genuinely dangling reference to a warning, which is the same
    // masking this check exists to prevent.
    const LoadResult rebound = sacm::io::load_xmi_string(
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
        R"(xmlns:xmi="http://acme.example/not-xmi" )"
        R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" )"
        R"(xmlns:gsn="http://scsc.acwg.gsn/2.0" xmi:id="acp_r">)"
        R"(<name content="Rebound XMI Prefix"/><argumentPackage xmi:id="ap_r"><name content="M"/>)"
        R"(<argumentElement xsi:type="sacm:Claim" xmi:id="g_r"><name content="GR"/></argumentElement>)"
        R"(<argumentElement xsi:type="gsn:Context" xmi:id="c_r"><name content="CR"/></argumentElement>)"
        R"(<argumentElement xsi:type="sacm:AssertedContext" xmi:id="ctx_r" source="g_r" )"
        R"(target="c_r"/></argumentPackage></sacm:AssuranceCasePackage>)");
    ASSERT_TRUE(rebound.ok);
    EXPECT_FALSE(rebound.document->has_preserved_element(ElementId{"c_r"}))
        << "a document that rebound xmlns:xmi to a non-XMI namespace was still allowed to confer "
           "preserved-element identity, so it could mask a real dangling reference";
    EXPECT_TRUE(has_code(sacm::validation::validate(*rebound.document),
                         sacm::validation::codes::kRefDangling))
        << "the reference should be reported as genuinely dangling, since nothing in this document "
           "establishes XMI identity for the preserved element";
}

// EMF-dialect files address elements by containment position
// (//@argumentPackage.0/@argumentationElement.18), so where a preserved
// fragment sits among its siblings is not cosmetic: re-emitting it after the
// typed children renumbers every element that followed it in the source, and
// any positional reference into that package then resolves to a different
// element. Our own reload is immune (the reader assigns ids first), which is
// exactly why this has to be asserted on the bytes.
TEST(Sacm23RoundTrip, SACM23_COMPAT_002_PreservedFragmentKeepsItsSiblingPosition) {
    const LoadResult loaded =
        sacm::io::load_xmi_file(fixture("interop-gsn20-current-namespace-valid.sacm.xmi"));
    ASSERT_TRUE(loaded.ok) << (loaded.diagnostics.empty() ? "load failed"
                                                          : loaded.diagnostics.front().message);

    const sacm::io::SaveResult compat =
        sacm::io::save_xmi_string(*loaded.document, sacm::io::SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(compat.ok);

    // Source order inside the argumentPackage: G1, AA1, AJ1, Sn1, C1(preserved),
    // SupportedBy. So the Context belongs after the Solution and before the
    // inference -- not appended at the end, and not hoisted to the front.
    const std::size_t solution = compat.xml.find(R"(content="Sn1")");
    const std::size_t context = compat.xml.find("gsn_:Context");
    const std::size_t inference = compat.xml.find("AssertedInference");
    ASSERT_NE(solution, std::string::npos) << compat.xml;
    ASSERT_NE(context, std::string::npos) << compat.xml;
    ASSERT_NE(inference, std::string::npos) << compat.xml;
    EXPECT_LT(solution, context) << "the preserved fragment was hoisted above its siblings:\n"
                                 << compat.xml;
    EXPECT_LT(context, inference)
        << "the preserved fragment was appended after the typed children instead of kept in "
           "position, which renumbers every later sibling:\n"
        << compat.xml;

    // Position has to be stable under repeated saves, or the file churns.
    const LoadResult reloaded = sacm::io::load_xmi_string(compat.xml);
    ASSERT_TRUE(reloaded.ok);
    const sacm::io::SaveResult twice =
        sacm::io::save_xmi_string(*reloaded.document, sacm::io::SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(twice.ok);
    EXPECT_EQ(compat.xml, twice.xml) << "preserved-fragment placement is not idempotent";
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

// A tolerant-mode document carrying both kinds of foreign content: an attribute
// (`acme:owner`) and a child element (`acme:vendorMetadata`), under a prefix
// declared only by the source file.
constexpr std::string_view kForeignNamespaceDocument =
    R"(<?xml version="1.0" encoding="UTF-8"?>)"
    R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
    R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
    R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" )"
    R"(xmlns:acme="http://acme.example/toolchain" xmi:version="2.0" xmi:id="acp_1" )"
    R"(acme:owner="alice"><name content="Vendor Extended Case"/>)"
    R"(<acme:vendorMetadata reviewCycle="Q3-2026"/></sacm:AssuranceCasePackage>)";

// Preserving vendor content on one save is not preserving it: the compatibility
// writer re-emits `acme:owner` verbatim, so unless the saved file also declares
// the `acme` prefix, the next load cannot tell the attribute is foreign (an
// undeclared prefix resolves to no namespace) and drops it. The loss therefore
// only shows up on the *second* save, which is what this exercises.
TEST(Sacm23RoundTrip, SACM23_COMPAT_001_PreservedForeignAttributeSurvivesTwoRoundTrips) {
    const LoadResult first = sacm::io::load_xmi_string(kForeignNamespaceDocument);
    ASSERT_TRUE(first.ok);
    ASSERT_EQ(first.document->roots().size(), 1u);
    ASSERT_EQ(first.document->roots().front()->preserved_attributes().size(), 1u)
        << "fixture no longer carries a foreign attribute; the test measures nothing";

    const sacm::io::SaveResult once =
        sacm::io::save_xmi_string(*first.document, sacm::io::SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(once.ok);
    EXPECT_NE(once.xml.find(R"(acme:owner="alice")"), std::string::npos);

    const LoadResult second = sacm::io::load_xmi_string(once.xml);
    ASSERT_TRUE(second.ok);
    ASSERT_EQ(second.document->roots().size(), 1u);
    EXPECT_EQ(second.document->roots().front()->preserved_attributes().size(), 1u)
        << "the foreign attribute was dropped when the saved file was read back";

    const sacm::io::SaveResult twice =
        sacm::io::save_xmi_string(*second.document, sacm::io::SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(twice.ok);
    EXPECT_NE(twice.xml.find(R"(acme:owner="alice")"), std::string::npos)
        << "the foreign attribute did not survive a second save:\n"
        << twice.xml;
    EXPECT_NE(twice.xml.find("vendorMetadata"), std::string::npos);

    // The same loss must be visible to semantic_compare, or every round-trip
    // assertion over vendor content passes without ever looking at it.
    const auto differences = sacm::compare::semantic_compare(*first.document, *second.document);
    for (const auto& difference : differences) {
        ADD_FAILURE() << "[" << difference.category << "] " << difference.path << ": "
                      << difference.message;
    }
}

// Compatibility output that re-emits a prefixed fragment must declare the
// prefix; otherwise the file this library writes is not namespace-well-formed,
// whatever a downstream consumer does with it.
TEST(Sacm23RoundTrip, SACM23_COMPAT_001_SavedOutputDeclaresPreservedForeignNamespaces) {
    const LoadResult loaded = sacm::io::load_xmi_string(kForeignNamespaceDocument);
    ASSERT_TRUE(loaded.ok);

    // The declaration is recorded on the document, not rediscovered from the
    // fragments -- the fragments are opaque text by design.
    const auto& foreign = loaded.document->foreign_namespaces();
    const auto acme = foreign.find("acme");
    ASSERT_NE(acme, foreign.end()) << "the foreign namespace declaration was not recorded";
    EXPECT_EQ(acme->second, "http://acme.example/toolchain");

    const sacm::io::SaveResult compat =
        sacm::io::save_xmi_string(*loaded.document, sacm::io::SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(compat.ok);
    EXPECT_NE(compat.xml.find(R"(xmlns:acme="http://acme.example/toolchain")"), std::string::npos)
        << "compatibility output re-emits the `acme` prefix without declaring it:\n"
        << compat.xml;

    // Strict output stays clean: it refuses the document rather than emitting a
    // declaration for content it will not write (SACM23-LIB-003, SACM23-XMI-004).
    const sacm::io::SaveResult strict = sacm::io::save_xmi_string(*loaded.document);
    EXPECT_FALSE(strict.ok);
    EXPECT_TRUE(has_code(strict.diagnostics, sacm::validation::codes::kXmiStrictSaveRefused));

    // A document with nothing preserved gains no declaration, so ordinary output
    // is unchanged by this mechanism.
    const LoadResult clean = sacm::io::load_xmi_file(fixture("package-minimal-valid.sacm.xmi"));
    ASSERT_TRUE(clean.ok);
    const sacm::io::SaveResult clean_compat =
        sacm::io::save_xmi_string(*clean.document, sacm::io::SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(clean_compat.ok);
    EXPECT_EQ(clean_compat.xml.find("acme"), std::string::npos);
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

// The import half of the strict/compatibility mode boundary. The COMPAT-001
// tests all assert tolerant-load-preserves + strict-save-refuses; without this,
// nothing pins that a STRICT LOAD refuses vendor content and keeps none of it.
// "Compatibility behaviour is not default strict behaviour" is a claim about
// both directions, and warn-and-keep on the strict path would satisfy every
// other test in the row while violating the requirement.
TEST(Sacm23RoundTrip, SACM23_COMPAT_001_StrictLoadRefusesVendorContent) {
    const std::filesystem::path path = fixture("vendor-extension-valid.sacm.xmi");

    // Tolerant is the preserving path (the control: without this the strict
    // assertions below could pass simply because nothing was there to keep).
    const LoadResult tolerant = sacm::io::load_xmi_file(path);
    ASSERT_TRUE(tolerant.ok);
    ASSERT_FALSE(tolerant.document->roots().front()->preserved_content().empty())
        << "fixture no longer carries vendor content; the test measures nothing";

    const LoadResult strict = sacm::io::load_xmi_file(path, LoadOptions{.mode = Mode::Strict});
    EXPECT_FALSE(strict.ok) << "strict load accepted a document carrying vendor content";
    EXPECT_TRUE(std::ranges::any_of(strict.diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == sacm::validation::Severity::Error;
    })) << "strict load reported no error for unknown content";
    // The half that matters: refusing is not enough if the content is kept
    // anyway. A strict document carries no compatibility content at all.
    if (strict.document.has_value()) {
        strict.document->for_each_element([](const sacm::model::SACMElement& element) {
            EXPECT_TRUE(element.preserved_content().empty())
                << "strict load kept preserved content on " << element.id().value();
            EXPECT_TRUE(element.preserved_attributes().empty())
                << "strict load kept a preserved attribute on " << element.id().value();
        });
        EXPECT_TRUE(strict.document->preserved_element_ids().empty())
            << "strict load recorded preserved-element identity";
    }

    // Same for a foreign ATTRIBUTE, which travels a different reader path.
    const LoadResult strict_attribute =
        sacm::io::load_xmi_string(kForeignNamespaceDocument, LoadOptions{.mode = Mode::Strict});
    EXPECT_FALSE(strict_attribute.ok);
    if (strict_attribute.document.has_value()) {
        strict_attribute.document->for_each_element([](const sacm::model::SACMElement& element) {
            EXPECT_TRUE(element.preserved_attributes().empty())
                << "strict load kept a vendor attribute on " << element.id().value();
        });
    }
}

// A minted id must not collide with an id living inside preserved content.
// This cannot be caught by any other test: `build_index` and `validate` walk
// the index, and preserved content is deliberately not indexed, so a collision
// emits two elements with the same xmi:id and nothing goes red. Removing the
// guard in peek_generated_id must fail here or the guard is untraceable.
TEST(Sacm23RoundTrip, SACM23_COMPAT_002_MintedIdDoesNotCollideWithPreservedContent) {
    // The preserved fragment carries `Claim_1` -- exactly the id the Claim
    // generator produces first.
    LoadResult loaded = sacm::io::load_xmi_string(
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
        R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
        R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" )"
        R"(xmlns:gsn="http://scsc.acwg.gsn/2.0" xmi:version="2.0" xmi:id="acp_m">)"
        R"(<name content="Mint Collision"/><argumentPackage xmi:id="ap_m"><name content="M"/>)"
        R"(<argumentElement xsi:type="gsn:Context" xmi:id="claim_1"><name content="Preserved"/>)"
        R"(</argumentElement></argumentPackage></sacm:AssuranceCasePackage>)");
    ASSERT_TRUE(loaded.ok);
    ASSERT_TRUE(loaded.document->has_preserved_element(ElementId{"claim_1"}))
        << "fixture no longer preserves an element under the generator's first id";

    Document document = std::move(*loaded.document);
    const sacm::commands::MutationResult created =
        document.apply(CreateClaim{.parent = ElementId{"ap_m"}, .name = "New Claim"});
    ASSERT_TRUE(created.applied)
        << (created.diagnostics.empty() ? "" : created.diagnostics.front().message);
    ASSERT_EQ(created.created_ids().size(), 1u);
    EXPECT_NE(created.created_ids().front().value(), "claim_1")
        << "the generator minted an id already used by preserved content; a compatibility save "
           "would emit two elements with the same xmi:id and nothing would detect it";

    // Proof at the byte level: the saved document must not carry the id twice.
    const sacm::io::SaveResult saved =
        sacm::io::save_xmi_string(document, sacm::io::SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(saved.ok);
    const std::string needle = R"(xmi:id="claim_1")";
    const std::size_t first = saved.xml.find(needle);
    ASSERT_NE(first, std::string::npos);
    EXPECT_EQ(saved.xml.find(needle, first + 1), std::string::npos)
        << "duplicate xmi:id in compatibility output:\n" << saved.xml;
}

// --- sacm::compat::adopt_preserved_content ---------------------------------
//
// A client that rebuilds a document from a lossy intermediate -- a projection
// whose structs have no field for unknown XML -- would otherwise drop everything
// a tolerant load preserved, even though the content is still in the document it
// rebuilt FROM. This is the library's answer, and it is exercised here from the
// library side: a standalone consumer must be able to rely on it without an
// application's test suite standing in for its coverage.
TEST(Sacm23RoundTrip, SACM23_LIB_002_AdoptPreservedContentRestoresWhatAProjectionDrops) {
    LoadResult source = sacm::io::load_xmi_string(kReferenceToPreservedElementDocument);
    ASSERT_TRUE(source.ok);
    ASSERT_TRUE(source.document->has_preserved_element(ElementId{"c1"}))
        << "fixture no longer preserves anything; the test measures nothing";

    // Stand in for a lossy intermediate: a STRICT reload of the same document
    // keeps the typed model and nothing else, exactly as a POD projection would.
    const sacm::io::SaveResult intermediate_bytes = sacm::io::save_xmi_string(
        *source.document, sacm::io::SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(intermediate_bytes.ok);
    LoadResult rebuilt = sacm::io::load_xmi_string(intermediate_bytes.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(rebuilt.document.has_value());
    bool rebuilt_carries_anything = false;
    rebuilt.document->for_each_element([&](const sacm::model::SACMElement& element) {
        rebuilt_carries_anything = rebuilt_carries_anything ||
                                   !element.preserved_content().empty() ||
                                   !element.preserved_attributes().empty();
    });
    ASSERT_FALSE(rebuilt_carries_anything) << "the stand-in intermediate was not lossy";

    const std::size_t adopted =
        sacm::compat::adopt_preserved_content(*rebuilt.document, *source.document);
    EXPECT_EQ(adopted, 1u);

    // (a) the fragment is back
    bool restored = false;
    rebuilt.document->for_each_element([&](const sacm::model::SACMElement& element) {
        for (const sacm::model::PreservedFragment& fragment : element.preserved_content()) {
            restored = restored || fragment.xml.find("gsn:Context") != std::string::npos;
        }
    });
    EXPECT_TRUE(restored) << "the preserved fragment was not restored";

    // (b) and so is the identity that makes it legible. Without this the
    // rebuilt document reports a hard SACM-REF-001 Error against an element its
    // own output still carries.
    EXPECT_TRUE(rebuilt.document->has_preserved_element(ElementId{"c1"}));
    const std::vector<sacm::validation::Diagnostic> problems =
        sacm::validation::validate(*rebuilt.document);
    EXPECT_FALSE(has_code(problems, sacm::validation::codes::kRefDangling))
        << "adoption restored the bytes but not the knowledge that makes them legible";
    EXPECT_TRUE(has_code(problems, sacm::validation::codes::kRefPreservedTarget));

    // (c) the foreign namespace declaration travels, or the re-emitted fragment
    // is not namespace-well-formed on the way back out.
    EXPECT_FALSE(rebuilt.document->foreign_namespaces().empty());

    // (d) adoption does not overwrite content the rebuild already produced --
    // that content came from the more recent parse.
    LoadResult already_has = sacm::io::load_xmi_string(kReferenceToPreservedElementDocument);
    ASSERT_TRUE(already_has.ok);
    const std::size_t before_second =
        sacm::compat::adopt_preserved_content(*already_has.document, *source.document);
    EXPECT_EQ(before_second, 0u)
        << "adoption overwrote preserved content the target already had";

    // (e) an id present only in the source does not invent an element.
    const std::size_t count_before = rebuilt.document->element_count();
    sacm::compat::adopt_preserved_content(*rebuilt.document, *source.document);
    EXPECT_EQ(rebuilt.document->element_count(), count_before);
}
