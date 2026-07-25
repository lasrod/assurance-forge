// Opt-in interoperability corpus (SACM23-COMPAT-002).
//
// COMPAT-002 requires that files produced by an INDEPENDENT SACM tool import,
// validate and semantically round-trip. Every other fixture in this repository
// is our own reconstruction of a dialect, which can only prove the reader
// handles the shape we *believe* a tool emits -- not a detail we did not know to
// reproduce.
//
// The corpus itself is deliberately not committed: docs/sacm/sacm-interop-corpus.md
// keeps this repository free of third-party licensing obligations, and one of
// the most useful files found so far carries no declared licence at all, so it
// could not be vendored even if the rule were relaxed.
//
// Point SACM_INTEROP_CORPUS at a directory of such files and these run against
// every file in it. Absent, they SKIP -- visibly, so a green CI run is never
// mistaken for third-party evidence. See the corpus document for where to
// obtain the files and what each one exercises.

#include "sacm/compare/semantic_compare.h"
#include "sacm/metadata/element_kind.h"
#include "sacm/validation/codes.h"
#include "sacm/io/xmi.h"
#include "sacm/model/document.h"
#include "sacm/validation/validate.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using sacm::io::LoadOptions;
using sacm::io::LoadResult;
using sacm::io::Mode;

// Files a tolerant load is expected to handle. Extensions are open-ended
// because tools disagree: .sacm2 (ACEditor), .assurancecase (EMF/ACME),
// .xmi, .xml, .model, .integration (ODE containers embedding SACM).
std::vector<std::filesystem::path> corpus_files() {
    std::vector<std::filesystem::path> files;
    const char* root = std::getenv("SACM_INTEROP_CORPUS");
    if (root == nullptr || *root == '\0') {
        return files;
    }
    std::error_code ec;
    const std::filesystem::path directory(root);
    if (!std::filesystem::is_directory(directory, ec)) {
        return files;
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory, ec)) {
        if (entry.is_regular_file(ec)) {
            files.push_back(entry.path());
        }
    }
    return files;
}

}  // namespace

// Every corpus file must at least PARSE into SACM elements. The failure this
// guards is the one that motivated COMPAT-002: a file that reports VALID while
// every element landed in preserved content, so nothing signals that the
// argument was never read.
TEST(Sacm23InteropCorpus, SACM23_COMPAT_002_ThirdPartyFilesParseIntoSacmElements) {
    const std::vector<std::filesystem::path> files = corpus_files();
    if (files.empty()) {
        GTEST_SKIP() << "SACM_INTEROP_CORPUS is unset or empty -- no third-party evidence was "
                        "exercised by this run. See docs/sacm/sacm-interop-corpus.md.";
    }

    int parsed = 0;
    for (const std::filesystem::path& path : files) {
        const LoadResult loaded = sacm::io::load_xmi_file(path);
        if (!loaded.ok) {
            // Not every file in a corpus directory is a SACM interchange
            // document -- an ODE container, for instance, embeds SACM under a
            // root this library does not accept. Report rather than fail, so a
            // mixed directory stays usable.
            const std::string reason =
                loaded.diagnostics.empty() ? "load failed" : loaded.diagnostics.front().message;
            GTEST_LOG_(INFO) << path.filename().string() << ": not loadable (" << reason << ")";
            continue;
        }
        ASSERT_TRUE(loaded.document.has_value()) << path.string();
        std::size_t elements = 0;
        loaded.document->for_each_element([&elements](const sacm::model::SACMElement&) {
            ++elements;
        });
        EXPECT_GT(elements, 0u)
            << path.string() << " loaded but produced no SACM elements -- the file parsed as XML "
                                "and yielded nothing, which is the silent-failure mode this "
                                "requirement exists to catch";
        ++parsed;
    }
    EXPECT_GT(parsed, 0) << "no file in the corpus directory was a loadable SACM document";
}

// The requirement's own words: import, validate, and semantically round-trip.
// Round-trip is asserted in compatibility mode because a third-party file may
// legitimately carry content strict mode refuses.
TEST(Sacm23InteropCorpus, SACM23_COMPAT_002_ThirdPartyFilesSemanticallyRoundTrip) {
    const std::vector<std::filesystem::path> files = corpus_files();
    if (files.empty()) {
        GTEST_SKIP() << "SACM_INTEROP_CORPUS is unset or empty -- no third-party evidence was "
                        "exercised by this run. See docs/sacm/sacm-interop-corpus.md.";
    }

    const sacm::io::SaveOptions compat{.mode = Mode::Tolerant};
    for (const std::filesystem::path& path : files) {
        const LoadResult first = sacm::io::load_xmi_file(path);
        if (!first.ok) {
            continue;  // see the note in the parse test
        }
        const sacm::io::SaveResult saved = sacm::io::save_xmi_string(*first.document, compat);
        ASSERT_TRUE(saved.ok) << path.string() << ": compatibility save failed";
        const LoadResult second = sacm::io::load_xmi_string(saved.xml);
        ASSERT_TRUE(second.ok) << path.string() << ": our own output did not load back";

        for (const sacm::compare::SemanticDifference& difference :
             sacm::compare::semantic_compare(*first.document, *second.document)) {
            ADD_FAILURE() << path.filename().string() << " [" << difference.category << "] "
                          << difference.path << ": " << difference.message;
        }

        // Validation diagnostics are reported, not asserted clean: a real file
        // may violate SACM rules, and saying so is the library working. What
        // must not happen is a crash or a silent pass.
        const std::vector<sacm::validation::Diagnostic> problems =
            sacm::validation::validate(*first.document);
        GTEST_LOG_(INFO) << path.filename().string() << ": " << problems.size()
                         << " validation diagnostics";
    }
}

// --- Committed third-party fixtures ----------------------------------------
//
// These run in CI, unconditionally. They are the answer to the standing gap in
// SACM23-COMPAT-002: every other fixture in this repository is our own
// reconstruction of a dialect, and a reconstruction can only prove the reader
// handles the shape we BELIEVE a tool emits. These are the bytes themselves,
// unmodified, redistributed under EPL-2.0 -- see
// libs/sacm/tests/data/interop-thirdparty/NOTICE.md.

namespace {

std::filesystem::path third_party(std::string_view name) {
    return std::filesystem::path(SACM_TEST_DATA_DIR) / "interop-thirdparty" / name;
}

std::size_t count_code(const std::vector<sacm::validation::Diagnostic>& diagnostics,
                       std::string_view code) {
    return static_cast<std::size_t>(std::ranges::count_if(
        diagnostics, [&](const auto& diagnostic) { return diagnostic.code == code; }));
}

}  // namespace

// An ODE DDIPackage embedding a SACM assurance case alongside architecture and
// failure-logic models -- the shape a real toolchain produces when SACM is one
// model among several. Strict refuses it; the tolerant path reads the SACM out
// and says plainly what that costs, because a user whose vendor file opens
// cleanly has no other reason to expect the rest of it to vanish on save.
TEST(Sacm23InteropCorpus, SACM23_COMPAT_002_ThirdPartyOdeContainerImportsWithNonConformanceWarning) {
    const std::filesystem::path path = third_party("mobstr-safetycase.integration");
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();

    const LoadResult loaded = sacm::io::load_xmi_file(path);
    ASSERT_TRUE(loaded.ok) << (loaded.diagnostics.empty() ? "load failed"
                                                          : loaded.diagnostics.front().message);

    // The user is told, in one diagnostic, all three things they need: the file
    // is not conformant, content outside the SACM packages is not represented,
    // and a save writes conformant SACM rather than their container.
    const auto warning = std::ranges::find_if(loaded.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == sacm::validation::codes::kXmiForeignContainerRoot;
    });
    ASSERT_NE(warning, loaded.diagnostics.end())
        << "a non-conformant container was read silently; the user would not know that the "
           "non-SACM half of their file is about to disappear";
    EXPECT_EQ(warning->severity, sacm::validation::Severity::Warning);
    EXPECT_NE(warning->message.find("does not conform"), std::string::npos) << warning->message;
    EXPECT_NE(warning->message.find("NOT represented"), std::string::npos) << warning->message;
    EXPECT_NE(warning->message.find("saving"), std::string::npos) << warning->message;

    // The assurance case really was read: the AssuranceCasePackage level is
    // kept (the container spells its role in the plural, and descending past it
    // would silently drop that level), and the argument inside it parsed.
    ASSERT_EQ(loaded.document->roots().size(), 1u) << "the AssuranceCasePackage level was lost";
    int claims = 0;
    loaded.document->for_each_element([&claims](const sacm::model::SACMElement& element) {
        if (element.kind() == sacm::metadata::ElementKind::Claim) {
            ++claims;
        }
    });
    // Exact, because the fixture is byte-pinned: `claims > 0` would still pass
    // if a regression dumped 33 of the 34 claims into preserved content, and
    // semantic_compare cannot catch that either -- both sides of the round trip
    // would degrade equally. That is the silent-degradation mode this
    // requirement exists to catch.
    EXPECT_EQ(claims, 34) << "the container was accepted but the argument did not fully parse";

    // It validates, and it round-trips -- the requirement's own words.
    EXPECT_TRUE(sacm::validation::validate(*loaded.document).empty());
    const sacm::io::SaveResult saved =
        sacm::io::save_xmi_string(*loaded.document, sacm::io::SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml);
    ASSERT_TRUE(reloaded.ok);
    for (const sacm::compare::SemanticDifference& difference :
         sacm::compare::semantic_compare(*loaded.document, *reloaded.document)) {
        ADD_FAILURE() << "[" << difference.category << "] " << difference.path << ": "
                      << difference.message;
    }

    // The promise the warning makes: what we write IS a conformant SACM
    // interchange document, not the container we were handed.
    EXPECT_NE(saved.xml.find("AssuranceCasePackage"), std::string::npos) << saved.xml.substr(0, 400);
    EXPECT_EQ(saved.xml.find("DDIPackage"), std::string::npos)
        << "the save reproduced the foreign container instead of normalizing to SACM";

    // Strict refuses the original outright -- the tolerance is explicitly a
    // compatibility behaviour, not the default.
    const LoadResult strict = sacm::io::load_xmi_file(path, LoadOptions{.mode = Mode::Strict});
    EXPECT_FALSE(strict.ok) << "strict accepted a non-conformant container root";
}

// EMF/ACME output rooted at a SACM AssuranceCasePackage. It genuinely violates
// SACM rules (template placeholder content), and the point of the assertion is
// that the library SAYS SO while still round-tripping it: reading third-party
// content and correctly calling it invalid is the library working.
TEST(Sacm23InteropCorpus, SACM23_COMPAT_002_ThirdPartyEmfFileImportsAndReportsItsRealViolations) {
    const std::filesystem::path path = third_party("sysmline-easyexample.assurancecase");
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();

    const LoadResult loaded = sacm::io::load_xmi_file(path);
    ASSERT_TRUE(loaded.ok) << (loaded.diagnostics.empty() ? "load failed"
                                                          : loaded.diagnostics.front().message);
    ASSERT_EQ(loaded.document->roots().size(), 1u);

    // GSN types layered via xsi:type resolve to the SACM classes they specialize.
    int claims = 0;
    loaded.document->for_each_element([&claims](const sacm::model::SACMElement& element) {
        if (element.kind() == sacm::metadata::ElementKind::Claim) {
            ++claims;
        }
    });
    EXPECT_GT(claims, 0) << "no argument was read from a file that is nothing but an argument";

    // The violations are real and are reported, not smoothed over.
    const std::vector<sacm::validation::Diagnostic> problems =
        sacm::validation::validate(*loaded.document);
    EXPECT_FALSE(problems.empty())
        << "this file carries genuine SACM rule violations (implementationConstraint on "
           "non-abstract elements, duplicate language tags); reporting none would mean the "
           "validator is not looking";
    // Both codes, with counts, rather than an `||` over two generic codes:
    // losing SACM-CITE-001 entirely would satisfy an `||` and go unnoticed.
    // The file has 46 implementationConstraints and no isAbstract attribute at
    // all; 6 sit inside preserved gsn_:Context subtrees, leaving 40 of each.
    EXPECT_EQ(count_code(problems, sacm::validation::codes::kCitationInvalid), 40u)
        << "clause 8.6: implementationConstraint requires isAbstract";
    EXPECT_EQ(count_code(problems, sacm::validation::codes::kMultiplicityViolation), 40u)
        << "clause 8.5: each LangString in a value must have a unique lang";
    // Fragments, not ids: preserved_element_ids() counts every id inside a
    // preserved subtree (54 here), which is an artefact of how deep the
    // subtrees are. Six is the number of gsn_:Context elements this file has.
    std::size_t fragments = 0;
    loaded.document->for_each_element([&fragments](const sacm::model::SACMElement& element) {
        fragments += element.preserved_content().size();
    });
    EXPECT_EQ(fragments, 6u)
        << "the gsn_:Context subtrees this file carries were not preserved";

    // Invalid content still has to survive a round trip -- refusing to lose a
    // user's data does not depend on their data being correct.
    const sacm::io::SaveResult saved =
        sacm::io::save_xmi_string(*loaded.document, sacm::io::SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml);
    ASSERT_TRUE(reloaded.ok);
    for (const sacm::compare::SemanticDifference& difference :
         sacm::compare::semantic_compare(*loaded.document, *reloaded.document)) {
        ADD_FAILURE() << "[" << difference.category << "] " << difference.path << ": "
                      << difference.message;
    }
}

// The container-loss case. MobSTr's DDIPackage happens to hold nothing but the
// assurance case, so it cannot demonstrate what SACM-XMI-009 warns about. This
// ETCS model does: three `odeProductPackages` siblings sit next to the SACM,
// and loading drops all of them. Without this fixture the warning would be
// asserted but never actually earned.
TEST(Sacm23InteropCorpus, SACM23_COMPAT_002_ThirdPartyContainerLosesItsNonSacmSiblings) {
    const std::filesystem::path path = third_party("deis-etcs.model");
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();

    // Precondition, measured from the source rather than assumed: the container
    // really does carry non-SACM models. If upstream ever trims them, this test
    // stops proving anything and should say so rather than pass quietly.
    std::ifstream stream(path, std::ios::binary);
    const std::string source((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
    ASSERT_NE(source.find("odeProductPackages"), std::string::npos)
        << "fixture no longer carries non-SACM sibling models; the loss this test measures is gone";

    const LoadResult loaded = sacm::io::load_xmi_file(path);
    ASSERT_TRUE(loaded.ok) << (loaded.diagnostics.empty() ? "load failed"
                                                          : loaded.diagnostics.front().message);
    EXPECT_TRUE(std::ranges::any_of(loaded.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == sacm::validation::codes::kXmiForeignContainerRoot;
    })) << "the container was read with no warning that its other models are being dropped";

    // The SACM survives...
    EXPECT_TRUE(sacm::validation::validate(*loaded.document).empty());
    int claims = 0;
    loaded.document->for_each_element([&claims](const sacm::model::SACMElement& element) {
        if (element.kind() == sacm::metadata::ElementKind::Claim) {
            ++claims;
        }
    });
    EXPECT_GT(claims, 0);

    // ...and the non-SACM siblings do not, which is the whole point of the
    // warning. Asserting the drop rather than hoping the user reads the text:
    // if a future change started preserving them, this test says so.
    const sacm::io::SaveResult saved =
        sacm::io::save_xmi_string(*loaded.document, sacm::io::SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(saved.ok);
    EXPECT_EQ(saved.xml.find("odeProductPackages"), std::string::npos)
        << "non-SACM container content reappeared in the output; either the warning is now wrong "
           "or the save is no longer conformant SACM";
    EXPECT_EQ(saved.xml.find("DDIPackage"), std::string::npos)
        << "the save reproduced the foreign container instead of normalizing to SACM";
    EXPECT_NE(saved.xml.find("AssuranceCasePackage"), std::string::npos);

    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml);
    ASSERT_TRUE(reloaded.ok);
    for (const sacm::compare::SemanticDifference& difference :
         sacm::compare::semantic_compare(*loaded.document, *reloaded.document)) {
        ADD_FAILURE() << "[" << difference.category << "] " << difference.path << ": "
                      << difference.message;
    }
}
