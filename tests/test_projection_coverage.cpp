// Which SACM 2.3 element kinds survive the legacy-POD round trip the bridge
// performs (SACM23-LIB-002).
//
// `core::commands::BridgeLegacyMutationToLibrary` re-derives the live document
// from `core::project_library_package_with_tags` + `sacm::serialize_sacm`. Any
// element kind the legacy `sacm::AssuranceCasePackage` cannot express is
// therefore DELETED from the document by every bridged command -- text edits,
// terminology, ACP, package removal, tree reorder, proposals.
//
// Four such kinds were found by measurement, one at a time, after six separate
// fixes to the same defect had already landed. This file makes the coverage a
// machine-checked invariant instead: it round-trips the library's own conforming
// SACM 2.3 fixtures and compares the element inventory kind by kind. It fails in
// BOTH directions -- a newly-lost kind (the library grew one, or the projection
// regressed) and a kind that stopped being lost while still listed here.
//
// This is a fixture-driven measurement rather than a scan of
// `RebuildSacmArgumentPackageFromParser`'s branches, because the round trip is
// what actually reaches disk: terminology and artifact content bypasses that
// function entirely and is carried by separate projections.

#include "core/library_package_projection.h"
#include "sacm/sacm_serializer.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

// Element kinds the legacy POD cannot express, so a bridged edit deletes them.
// Every entry here is a live data-loss defect, disclosed on SACM23-LIB-002 and
// guarded at runtime by the bridge's representability check. This is a list of
// things that are WRONG -- shrinking it is the goal.
const std::set<std::string>& KnownUnrepresentableKinds() {
    static const std::set<std::string> kinds = {
        "argumentgroup",            // clause 11.2
        "assertedartifactsupport",  // clause 11.17
        "assertedartifactcontext",  // clause 11.18
        "terminologygroup",         // clause 10.3
    };
    return kinds;
}

// Fixtures the bridge re-derive REJECTS: the projected package cannot be loaded
// back at all, so the command fails rather than corrupting. Visible instead of
// silent, and therefore less bad -- but it means no bridged edit (a rename, a
// terminology change, an ACP edit) is possible on a document of that shape.
//
// A rejected fixture also MASKS loss: whatever it would have dropped is never
// measured, because the round trip does not complete. Emptying this list is a
// precondition for trusting the lost-kind list above to be complete.
const std::set<std::string>& KnownRejectedFixtures() {
    static const std::set<std::string> files = {
        "artifact-full-valid.sacm.xmi",  // full clause 12 artifact model
    };
    return files;
}

std::map<std::string, int> InventoryByKind(const sacm_adapter::LibraryDocument& document) {
    std::map<std::string, int> counts;
    const core::AssuranceCase projected = sacm_adapter::project_case(document);
    for (const core::SacmElement& element : projected.elements)
        ++counts[element.type];
    return counts;
}

std::vector<std::filesystem::path> ConformingFixtures() {
    const std::filesystem::path dir =
        std::filesystem::path(AF_REPO_ROOT) / "libs" / "sacm" / "tests" / "data" / "sacm23";
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(dir))
        return files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file())
            continue;
        const std::string name = entry.path().filename().string();
        // `*-valid` only: the invalid fixtures exist to be rejected, and the
        // interop dialects are covered by SACM23-COMPAT-002 -- a difference there
        // would be a dialect finding, not a projection-coverage one.
        if (name.find("-valid.sacm.xmi") == std::string::npos)
            continue;
        if (name.rfind("interop-", 0) == 0)
            continue;
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace

// SACM23-INT-001 claims the POD projection is "field-complete and lossless".
// The evidence behind that -- the Stage-3 parallel-load baseline -- compares the
// library against the LEGACY PARSER over six Assurance Forge files, so it can
// only speak for constructs those files contain, and they contain none of
// clause 11's groups or artifact relationships. The claim was true over that
// corpus and unproven beyond it.
//
// This measures the completeness half over conforming SACM 2.3 instead: every
// element the library read must appear in `project_case`. The projection is
// entitled to exactly two exclusions -- packages (containers, not drawn nodes)
// and clause 8.7 utility elements (metadata carried ON elements) -- and this
// fails if it quietly makes a third.
//
// Scope, stated because the INT-001 sentence overreached in exactly this way:
// this proves ELEMENT completeness, not FIELD completeness. Per-field fidelity
// is still only measured over the Stage-3 corpus.
TEST(ProjectionCoverage, SACM23_INT_001_ProjectionEmitsEveryNonContainerElement) {
    const std::vector<std::filesystem::path> fixtures = ConformingFixtures();
    ASSERT_FALSE(fixtures.empty()) << "no conforming SACM 2.3 fixtures found";

    std::size_t checked = 0;
    std::set<std::string> covered_kinds;

    for (const std::filesystem::path& fixture : fixtures) {
        sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(fixture);
        ASSERT_TRUE(loaded.ok) << fixture.filename().string();
        ASSERT_NE(loaded.document, nullptr) << fixture.filename().string();

        std::set<std::string> projected_ids;
        for (const core::SacmElement& element : sacm_adapter::project_case(*loaded.document).elements)
            projected_ids.insert(element.id);

        for (const sacm_adapter::DocumentElement& element :
             sacm_adapter::list_document_elements(*loaded.document)) {
            // A fixture may legitimately contribute nothing -- vendor-extension-valid
            // is packages plus preserved content -- so non-vacuity is checked over the
            // corpus below rather than per file.
            if (element.is_package || element.is_utility)
                continue;
            ++checked;
            covered_kinds.insert(element.kind);
            EXPECT_TRUE(projected_ids.count(element.id) > 0)
                << fixture.filename().string() << ": the projection drops " << element.kind << " '"
                << element.id << "', which is neither a package nor a utility element";
        }
    }

    EXPECT_GT(checked, 0u) << "the sweep checked no elements at all";
    // The whole point is reaching past the shapes Assurance Forge's own files
    // contain. If the corpus stopped carrying these, the test would still pass
    // while proving only what the Stage-3 baseline already proved.
    for (const char* beyond_gsn : {"argumentgroup", "assertedartifactsupport", "terminologygroup"}) {
        EXPECT_TRUE(covered_kinds.count(beyond_gsn) > 0)
            << "the corpus no longer exercises '" << beyond_gsn
            << "', so this test has stopped reaching beyond the Stage-3 corpus";
    }
}

TEST(ProjectionCoverage, SACM23_LIB_002_BridgeRoundTripLosesOnlyTheKnownKinds) {
    const std::vector<std::filesystem::path> fixtures = ConformingFixtures();
    ASSERT_FALSE(fixtures.empty()) << "no conforming SACM 2.3 fixtures found; this test measures nothing";

    std::set<std::string> lost_kinds;
    std::set<std::string> observed_kinds;
    std::set<std::string> rejected_fixtures;

    for (const std::filesystem::path& fixture : fixtures) {
        sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(fixture);
        ASSERT_TRUE(loaded.ok) << fixture.filename().string();
        ASSERT_NE(loaded.document, nullptr) << fixture.filename().string();

        const std::map<std::string, int> before = InventoryByKind(*loaded.document);
        for (const auto& [kind, count] : before)
            observed_kinds.insert(kind);

        // Exactly what the bridge does to the live document.
        const sacm::AssuranceCasePackage package =
            core::project_library_package_with_tags(*loaded.document);
        if (!sacm_adapter::reload_document_keeping_compatibility_content(
                *loaded.document, sacm::serialize_sacm(package))) {
            // A different failure mode from silent loss, and a less bad one: the
            // command fails visibly rather than corrupting. Still a defect --
            // every bridged edit is impossible on such a document -- so it is
            // measured and listed rather than swallowed.
            rejected_fixtures.insert(fixture.filename().string());
            continue;
        }

        const std::map<std::string, int> after = InventoryByKind(*loaded.document);
        for (const auto& [kind, count] : before) {
            const auto it = after.find(kind);
            if (it == after.end() || it->second < count)
                lost_kinds.insert(kind);
        }
    }

    // Rejections, both directions: a new one is a regression, and a fixture that
    // starts round-tripping must be taken off the list so its losses get measured.
    for (const std::string& rejected : rejected_fixtures) {
        EXPECT_TRUE(KnownRejectedFixtures().count(rejected) > 0)
            << "the bridge re-derive now REJECTS '" << rejected
            << "', so no bridged edit is possible on a document of that shape";
    }
    for (const std::string& known : KnownRejectedFixtures()) {
        EXPECT_TRUE(rejected_fixtures.count(known) > 0)
            << "'" << known
            << "' now round-trips; remove it from the known-rejected list so the kinds it "
               "carries are measured for loss";
    }

    // Non-vacuity: the sweep has to have seen the kinds it claims to be checking.
    for (const std::string& known : KnownUnrepresentableKinds()) {
        EXPECT_TRUE(observed_kinds.count(known) > 0)
            << "no fixture contains a '" << known
            << "', so listing it as known-unrepresentable is unfalsifiable here";
    }

    for (const std::string& lost : lost_kinds) {
        EXPECT_TRUE(KnownUnrepresentableKinds().count(lost) > 0)
            << "a bridged edit now DELETES '" << lost
            << "' from the document, and it is not on the known-unrepresentable list. Either "
               "teach the legacy projection to carry it, or add it to the list AND to the "
               "SACM23-LIB-002 disclosure -- silently widening the loss is what this test exists "
               "to prevent.";
    }
    for (const std::string& known : KnownUnrepresentableKinds()) {
        if (observed_kinds.count(known) == 0)
            continue;
        EXPECT_TRUE(lost_kinds.count(known) > 0)
            << "'" << known
            << "' now survives the bridge round trip but is still listed as unrepresentable. "
               "Remove it from the list and from the SACM23-LIB-002 disclosure.";
    }
}
