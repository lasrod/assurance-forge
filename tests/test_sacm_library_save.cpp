// Phase 9 Stage 6: library-backed save.
//
// Before flipping the app's save path (and the audit readers) onto the library
// XMI writer, this proves the foundational invariant: serializing a library
// document to XMI and loading it back preserves the projection the application
// renders and audits from. If a save/load round-trip loses or reshapes content,
// the audit verifier would flag divergence once the saved bytes become XMI --
// so any difference here is a Stage 6 blocker, surfaced before the flip.
//
// The comparison is library-vs-library (save -> load), not library-vs-legacy,
// so it is independent of the known projection-vs-legacy baseline.

#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"
#include "sacm_adapter/projection_diff.h"

#include "core/audit/canonical_model_hash.h"
#include "core/library_package_projection.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

std::filesystem::path repo_root() { return std::filesystem::path(AF_REPO_ROOT); }

const std::vector<std::string>& fixtures() {
    static const std::vector<std::string> kFixtures = {
        "tests/data/fixture_roundtrip_core_argument.sacm.xml",
        "tests/data/fixture_roundtrip_sample.sacm.xml",
        "tests/data/fixture_roundtrip_open_autonomy.sacm.xml",
        "tests/data/fixture_acp_parity.sacm.xml",
        "data/oasc-ja.xml",
        "data/open-autonomy-safety-case.sacm.xml",
    };
    return kFixtures;
}

} // namespace

// Serializing to XMI (tolerant) and loading back must preserve the projected
// case exactly, for every fixture the library reads.
TEST(SacmLibrarySave, SACM23_INT_002_XmiSaveRoundTripsTheProjection) {
    for (const std::string& relative : fixtures()) {
        SCOPED_TRACE(relative);
        const std::filesystem::path path = repo_root() / relative;
        ASSERT_TRUE(std::filesystem::exists(path)) << path.string();

        sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
        ASSERT_TRUE(loaded.ok);
        ASSERT_NE(loaded.document, nullptr);
        const core::AssuranceCase before = sacm_adapter::project_case(*loaded.document);

        const sacm_adapter::SaveOutcome saved = sacm_adapter::save_document(*loaded.document);
        ASSERT_TRUE(saved.ok) << (saved.diagnostics.empty() ? "" : saved.diagnostics.front().message);
        ASSERT_FALSE(saved.xml.empty());

        sacm_adapter::LibraryDocument reloaded;
        ASSERT_TRUE(sacm_adapter::reload_document(reloaded, saved.xml));
        const core::AssuranceCase after = sacm_adapter::project_case(reloaded);

        const std::vector<sacm_adapter::ProjectionDifference> differences =
            sacm_adapter::diff_cases(before, after);
        for (const sacm_adapter::ProjectionDifference& difference : differences) {
            ADD_FAILURE() << difference.category << " " << difference.path << ": "
                          << difference.message;
        }
    }
}

// ACPs are vendor TaggedValues (clause 8.12); a library-backed save must not
// drop them, or the migration would lose a must-support feature.
TEST(SacmLibrarySave, SACM23_INT_002_XmiSavePreservesAcps) {
    const std::filesystem::path path = repo_root() / "tests" / "data" / "fixture_acp_parity.sacm.xml";
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);
    ASSERT_EQ(sacm_adapter::project_case(*loaded.document).acps.size(), 2u);

    const sacm_adapter::SaveOutcome saved = sacm_adapter::save_document(*loaded.document);
    ASSERT_TRUE(saved.ok) << (saved.diagnostics.empty() ? "" : saved.diagnostics.front().message);

    sacm_adapter::LibraryDocument reloaded;
    ASSERT_TRUE(sacm_adapter::reload_document(reloaded, saved.xml));
    EXPECT_EQ(sacm_adapter::project_case(reloaded).acps.size(), 2u);
}

// The library->package projection the audit will hash must capture the argument
// content and, crucially, produce a canonical hash that is STABLE across an XMI
// save/load. That stability is what makes the audit's on-disk and replayed sides
// converge once both route through this projection (Approach B).
TEST(SacmLibrarySave, SACM23_INT_002_LibraryPackageHashIsStableUnderXmiRoundTrip) {
    // Argument-only fixtures: terminology/artifact projection is a following
    // increment, so restrict the content assertion to fixtures without them.
    const std::filesystem::path path = repo_root() / "tests" / "data" / "fixture_acp_parity.sacm.xml";
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);

    const sacm::AssuranceCasePackage package = core::project_library_package(*loaded.document);
    ASSERT_FALSE(package.argumentPackages.empty());
    EXPECT_EQ(package.argumentPackages.front().claims.size(), 2u);  // G1, G2 captured
    const std::string hash_before = core::audit::CanonicalModelHash(package);
    EXPECT_FALSE(hash_before.empty());

    const sacm_adapter::SaveOutcome saved = sacm_adapter::save_document(*loaded.document);
    ASSERT_TRUE(saved.ok);
    sacm_adapter::LibraryDocument reloaded;
    ASSERT_TRUE(sacm_adapter::reload_document(reloaded, saved.xml));

    const std::string hash_after = core::audit::CanonicalModelHash(core::project_library_package(reloaded));
    EXPECT_EQ(hash_before, hash_after) << "projection hash must be stable across an XMI round-trip";
}
