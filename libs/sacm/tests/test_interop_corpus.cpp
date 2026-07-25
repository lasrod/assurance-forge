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
#include "sacm/io/xmi.h"
#include "sacm/model/document.h"
#include "sacm/validation/validate.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

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
