// Phase 9 Stage 3: passive parallel load.
//
// Loads each repository fixture through both the legacy parser and the SACM
// library, projects the library document into the same POD model the
// application renders from, and compares. Nothing in the application depends on
// the library yet; this test exists to measure how far apart they are before
// anything does.
//
// The two do not agree today, and pretending otherwise would stall the slice.
// So the test asserts against a recorded baseline: any *new* difference fails,
// and a baseline entry that no longer occurs also fails, so the list can only
// shrink. Stage 4 (making the library the source of truth) is gated on it
// reaching zero.

#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"
#include "sacm_adapter/projection_diff.h"

#include "parser/xml_parser.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>

namespace {

std::filesystem::path repo_root() { return std::filesystem::path(AF_REPO_ROOT); }

// The same six files libs/sacm already round-trips, so any difference here is a
// projection gap rather than a parse failure.
const std::vector<std::string>& fixtures() {
    static const std::vector<std::string> kFixtures = {
        "tests/data/fixture_roundtrip_core_argument.sacm.xml",
        "tests/data/fixture_roundtrip_sample.sacm.xml",
        "tests/data/fixture_roundtrip_open_autonomy.sacm.xml",
        "tests/data/fixture_roundtrip_sanitized_strict.sacm.xml",
        "data/oasc-ja.xml",
        "data/open-autonomy-safety-case.sacm.xml",
    };
    return kFixtures;
}

// Categories of difference already understood and accepted for now, keyed by
// fixture. Each entry is a category name plus the count observed when the
// baseline was recorded. Recorded rather than suppressed: the numbers are the
// backlog Stage 4 has to burn down, and they are visible in the file.
//
// See docs/sacm/sacm-stage3-projection-baseline.md for what each category means
// and which are projection bugs versus deliberate legacy behaviour.
struct BaselineKey {
    std::string fixture;
    std::string category;
    friend auto operator<=>(const BaselineKey&, const BaselineKey&) = default;
};

// Baseline lives beside the test as JSON so updating it is a reviewable diff
// rather than a code edit buried in a test body.
std::map<BaselineKey, std::size_t> load_baseline() {
    const std::filesystem::path path =
        repo_root() / "tests" / "data" / "sacm_parallel_load_baseline.json";
    std::map<BaselineKey, std::size_t> baseline;
    if (!std::filesystem::exists(path)) {
        return baseline;
    }
    std::ifstream stream(path);
    if (!stream) {
        ADD_FAILURE() << "baseline exists but could not be opened: " << path.string();
        return baseline;
    }
    nlohmann::json json;
    try {
        stream >> json;
    } catch (const nlohmann::json::exception& error) {
        ADD_FAILURE() << "baseline is not valid JSON (" << path.string()
                      << "): " << error.what();
        return baseline;
    }
    for (const auto& [fixture, categories] : json.items()) {
        for (const auto& [category, count] : categories.items()) {
            baseline[BaselineKey{fixture, category}] = count.get<std::size_t>();
        }
    }
    return baseline;
}

std::map<std::string, std::size_t> count_by_category(
    const std::vector<sacm_adapter::ProjectionDifference>& differences) {
    std::map<std::string, std::size_t> counts;
    for (const sacm_adapter::ProjectionDifference& difference : differences) {
        ++counts[difference.category];
    }
    return counts;
}

} // namespace

// ACP synthesis parity. No repo fixture carries Assurance Claim Points, so the
// baseline test above never exercises them. This loads a fixture that does
// through both the legacy parser and the library projection and asserts the
// synthesized ACP records match exactly -- ACP support is a hard requirement of
// the migration, so a projection that dropped or mis-synthesized them must fail.
TEST(SacmLibraryParallelLoad, SACM23_INT_001_ProjectionSynthesizesAcpsLikeLegacy) {
    const std::filesystem::path path = repo_root() / "tests" / "data" / "fixture_acp_parity.sacm.xml";
    ASSERT_TRUE(std::filesystem::exists(path));

    const auto legacy = parser::parse_sacm_xml(path.string());
    ASSERT_TRUE(legacy.has_value()) << legacy.error();
    ASSERT_EQ(legacy->acps.size(), 2u) << "the fixture must actually carry ACPs";

    const sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);
    const core::AssuranceCase projected = sacm_adapter::project_case(*loaded.document);

    const std::vector<sacm_adapter::ProjectionDifference> differences =
        sacm_adapter::diff_cases(*legacy, projected);
    for (const sacm_adapter::ProjectionDifference& difference : differences) {
        if (difference.category.rfind("acp", 0) == 0) {
            ADD_FAILURE() << difference.category << " " << difference.path << ": "
                          << difference.message;
        }
    }
    ASSERT_EQ(projected.acps.size(), 2u) << "projection dropped ACPs";

    // Spot-check the field-level synthesis on the element ACP and the
    // relationship ACP.
    const auto find_acp = [&](const std::string& id) -> const core::AcpRecord* {
        for (const core::AcpRecord& acp : projected.acps) {
            if (acp.id == id) {
                return &acp;
            }
        }
        return nullptr;
    };
    const core::AcpRecord* acp1 = find_acp("ACP1");
    ASSERT_NE(acp1, nullptr);
    EXPECT_EQ(acp1->target_kind, "element");
    EXPECT_EQ(acp1->target_id, "G1");
    EXPECT_EQ(acp1->resolution_kind, "text");
    EXPECT_EQ(acp1->confidence_claim_id, "CC1");
    const core::AcpRecord* acp2 = find_acp("ACP2");
    ASSERT_NE(acp2, nullptr);
    EXPECT_EQ(acp2->target_kind, "relationship");
    EXPECT_EQ(acp2->target_id, "R1");
    EXPECT_EQ(acp2->top_goal_id, "CONF_TOP");
}

TEST(SacmLibraryParallelLoad, SACM23_INT_001_ProjectionMatchesLegacyWithinBaseline) {
    const std::map<BaselineKey, std::size_t> baseline = load_baseline();
    std::map<BaselineKey, std::size_t> observed;

    for (const std::string& relative : fixtures()) {
        const std::filesystem::path path = repo_root() / relative;
        ASSERT_TRUE(std::filesystem::exists(path)) << "fixture missing: " << path.string();

        const auto legacy = parser::parse_sacm_xml(path.string());
        ASSERT_TRUE(legacy.has_value()) << relative << ": legacy parse failed: " << legacy.error();

        const sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
        if (!loaded.ok || loaded.document == nullptr) {
            // The whole point of this test is to be diagnosable from CI logs, so
            // surface why the library declined the file rather than a bare flag.
            std::string detail;
            for (const sacm_adapter::LoadDiagnostic& diagnostic : loaded.diagnostics) {
                detail += "\n    " + diagnostic.severity + " " + diagnostic.code + ": " +
                          diagnostic.message;
            }
            FAIL() << relative << ": library load failed (namespace '" << loaded.source_namespace
                   << "', version " << loaded.source_version << ")" << detail;
        }

        const core::AssuranceCase projected = sacm_adapter::project_case(*loaded.document);
        const std::vector<sacm_adapter::ProjectionDifference> differences =
            sacm_adapter::diff_cases(*legacy, projected);

        for (const auto& [category, count] : count_by_category(differences)) {
            observed[BaselineKey{relative, category}] = count;
        }

        // Print the first few so a regression is diagnosable from CI output
        // alone rather than requiring a local repro.
        std::size_t printed = 0;
        for (const sacm_adapter::ProjectionDifference& difference : differences) {
            if (printed++ >= 5) {
                break;
            }
            std::cout << "  [" << relative << "] " << difference.category << " "
                      << difference.path << ": " << difference.message << "\n";
        }
    }

    // New difference categories, or more differences than recorded, are
    // regressions.
    for (const auto& [key, count] : observed) {
        const auto recorded = baseline.find(key);
        if (recorded == baseline.end()) {
            ADD_FAILURE() << "new difference category '" << key.category << "' in " << key.fixture
                          << " (" << count << " occurrences); the projection regressed or the "
                          << "baseline needs a deliberate update";
            continue;
        }
        EXPECT_LE(count, recorded->second)
            << key.fixture << " / " << key.category << ": " << count
            << " differences, baseline allows " << recorded->second;
    }

    // A baseline entry that no longer occurs must be removed, so the list can
    // only shrink and cannot be padded.
    for (const auto& [key, count] : baseline) {
        if (!observed.contains(key)) {
            ADD_FAILURE() << "stale baseline entry: " << key.fixture << " / " << key.category
                          << " no longer differs; remove it from the baseline";
        }
    }
}
