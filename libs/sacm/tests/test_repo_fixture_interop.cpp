// In-tree interoperability checks against the Assurance Forge repository's
// existing SACM fixtures (legacy app dialect). Skipped in standalone builds
// where the repository fixtures are unavailable.

#include "sacm/compare/semantic_compare.h"
#include "sacm/io/xmi.h"
#include "sacm/validation/validate.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

using sacm::io::LoadResult;
using sacm::io::Mode;
using sacm::io::SaveOptions;

std::filesystem::path repo_fixture(std::string_view relative) {
#ifdef SACM_REPO_FIXTURES_DIR
    return std::filesystem::path(SACM_REPO_FIXTURES_DIR) / relative;
#else
    (void)relative;
    return {};
#endif
}

// Tolerant load -> compatibility save -> reload -> semantic equality.
// Compatibility mode because legacy files may carry preserved content.
void expect_repo_roundtrip(std::string_view relative) {
    const std::filesystem::path path = repo_fixture(relative);
    if (path.empty() || !std::filesystem::exists(path)) {
        GTEST_SKIP() << "repository fixture unavailable: " << relative;
    }
    const LoadResult first = sacm::io::load_xmi_file(path);
    ASSERT_TRUE(first.ok) << (first.diagnostics.empty() ? "" : first.diagnostics.front().message);
    const auto saved =
        sacm::io::save_xmi_string(*first.document, SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(saved.ok);
    const LoadResult second = sacm::io::load_xmi_string(saved.xml);
    ASSERT_TRUE(second.ok);
    const auto differences = sacm::compare::semantic_compare(*first.document, *second.document);
    for (const auto& difference : differences) {
        ADD_FAILURE() << relative << " [" << difference.category << "] " << difference.path << ": "
                      << difference.message;
    }
}

TEST(Sacm23Interop, SACM23_RT_001_RepoCoreArgumentFixtureRoundTrips) {
    expect_repo_roundtrip("tests/data/fixture_roundtrip_core_argument.sacm.xml");
}

TEST(Sacm23Interop, SACM23_RT_001_RepoSampleFixtureRoundTrips) {
    expect_repo_roundtrip("tests/data/fixture_roundtrip_sample.sacm.xml");
}

TEST(Sacm23Interop, SACM23_RT_001_RepoOpenAutonomyFixtureRoundTrips) {
    expect_repo_roundtrip("tests/data/fixture_roundtrip_open_autonomy.sacm.xml");
}

TEST(Sacm23Interop, SACM23_RT_001_RepoSanitizedStrictFixtureRoundTrips) {
    expect_repo_roundtrip("tests/data/fixture_roundtrip_sanitized_strict.sacm.xml");
}

TEST(Sacm23Interop, SACM23_BASE_001_JapaneseMultiLanguageFixtureRoundTrips) {
    expect_repo_roundtrip("data/oasc-ja.xml");
}

TEST(Sacm23Interop, SACM23_RT_001_RepoFullSafetyCaseRoundTrips) {
    expect_repo_roundtrip("data/open-autonomy-safety-case.sacm.xml");
}

}  // namespace
