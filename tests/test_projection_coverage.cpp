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
#include "legacy_sacm/sacm_serializer.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ios>
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
        "argumentgroup",           // clause 11.2
        "assertedartifactsupport", // clause 11.17
        "assertedartifactcontext", // clause 11.18
        "terminologygroup",        // clause 10.3
        // clause 12.4. Added when a fixture first put an Event in a document
        // the bridge can round-trip. The only Event-bearing fixture before it
        // was artifact-full-valid, which the bridge rejects outright -- so this
        // loss was never measured, exactly as the note on KnownRejectedFixtures
        // below warns. It is a new disclosure, not a new regression: the
        // projection has never carried Event (issue #292).
        "event",
        // Clauses 12.2 (Artifact), 12.3 (Activity), 10.2 (Term) and 10.5
        // (Category). Surfaced by the SACM23-CP-002/003/004 interchange-unit
        // fixtures added for issue #295: those are rooted at a bare
        // ArtifactPackage/TerminologyPackage, which no earlier fixture was, and
        // the only artifact-rich fixture before them (artifact-full-valid) is on
        // the rejected list below -- so these losses were never measured. Exactly
        // the blind spot the note on KnownRejectedFixtures warns about.
        //
        // New disclosures, not new regressions: the legacy POD has never carried
        // any of the four. They are disclosed rather than fixed because carrying
        // them means growing the POD to cover SACM 2.3 -- the model the library
        // migration exists to retire -- and because the bridge already REFUSES a
        // command whose document holds unrepresentable content rather than
        // dropping it silently (SACM23_LIB_002_BridgedEditRefusesRatherThanDelete-
        // UnrepresentableElements). Tracked on SACM23-LIB-002.
        "artifact",
        "activity",
        "term",
        "category",
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
        "artifact-full-valid.sacm.xmi", // full clause 12 artifact model
    };
    return files;
}

// Attributes the bridge round trip DROPS from an element that survives it,
// keyed `kind.attribute` so a loss on one class cannot mask the same attribute
// still being carried on another.
//
// Every entry here was found the day the attribute sweep below was written --
// the kind sweep could not see them, because the element itself survives and the
// inventory stays balanced. They are new DISCLOSURES, not new regressions: the
// legacy POD has never had a field for any of them. They are disclosed rather
// than fixed for the same reason as the lost kinds: carrying them means growing
// a model the library migration exists to retire.
//
// The difference from the lost kinds matters and is stated on SACM23-LIB-002:
// the bridge's guard sweeps ELEMENTS, so a document that would lose only
// attributes is NOT refused -- it goes through, and the attribute is gone. That
// is the silent half these four occupy, and closing it is bridge-retirement work
// (issue #350), not a list entry.
const std::set<std::string>& KnownLostAttributes() {
    static const std::set<std::string> attributes = {
        // clause 8.2. A concrete element's link to the abstract pattern element
        // it instantiates.
        "claim.abstractForm",
        // clause 8.2. The pair that makes an element a CITATION of another
        // rather than an assertion in its own right -- dropping them turns a
        // reference into an original claim.
        "claim.isCitation",
        "claim.citedElement",
        // clause 8.2. SACM's model-global identifier on the case package. The
        // POD carries gid on elements but has no field for it on the package.
        "assurancecasepackage.gid",
        // clause 10.10. The ExpressionElements a structured Expression is built
        // from; without them the production rule references names that resolve
        // to nothing.
        "expression.element",
    };
    return attributes;
}

// A `name=value;name=value;` fingerprint split into its fields, so a dropped
// attribute and a changed one can be told apart -- comparing whole fingerprints
// reports every field of an element as different when only one of them moved.
std::map<std::string, std::string> ParseFields(const std::string& fingerprint) {
    std::map<std::string, std::string> fields;
    std::size_t start = 0;
    while (start < fingerprint.size()) {
        const std::size_t end = fingerprint.find(';', start);
        if (end == std::string::npos)
            break;
        const std::size_t equals = fingerprint.find('=', start);
        if (equals != std::string::npos && equals < end)
            fields[fingerprint.substr(start, equals - start)] = fingerprint.substr(equals + 1, end - equals - 1);
        start = end + 1;
    }
    return fields;
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

        for (const sacm_adapter::DocumentElement& element : sacm_adapter::list_document_elements(*loaded.document)) {
            // A fixture may legitimately contribute nothing -- vendor-extension-valid
            // is packages plus preserved content -- so non-vacuity is checked over the
            // corpus below rather than per file.
            if (element.is_package || element.is_utility)
                continue;
            ++checked;
            covered_kinds.insert(element.kind);
            EXPECT_TRUE(projected_ids.count(element.id) > 0)
                << fixture.filename().string() << ": the projection drops " << element.kind << " '" << element.id
                << "', which is neither a package nor a utility element";
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
        const sacm::AssuranceCasePackage package = core::project_library_package_with_tags(*loaded.document);
        if (!sacm_adapter::reload_document_keeping_compatibility_content(*loaded.document,
                                                                         sacm::serialize_sacm(package))) {
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
            << "no fixture contains a '" << known << "', so listing it as known-unrepresentable is unfalsifiable here";
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

// The kind sweep above cannot see an ELEMENT SURVIVE WITH ITS MEANING CHANGED.
// That is not hypothetical here: `isCounter` was dropped by the bridge while the
// relationship itself survived, re-serializing a rebuttal of the top claim as an
// inference supporting it, with the inventory perfectly balanced. Attribute
// fidelity was hand-maintained per test afterwards -- one assertion per attribute
// somebody thought to write -- which is the arrangement that let the first one
// through (#347, sweep depth).
//
// This sweeps every attribute of every surviving element across the same round
// trip, so a newly-lost attribute fails here rather than waiting to be noticed.
TEST(ProjectionCoverage, SACM23_LIB_002_BridgeRoundTripKeepsEveryAttributeOfASurvivingElement) {
    const std::vector<std::filesystem::path> fixtures = ConformingFixtures();
    ASSERT_FALSE(fixtures.empty()) << "no conforming SACM 2.3 fixtures found; this test measures nothing";

    std::size_t compared = 0;
    std::set<std::string> attributes_seen;
    std::set<std::string> lost_attributes;

    for (const std::filesystem::path& fixture : fixtures) {
        sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(fixture);
        ASSERT_TRUE(loaded.ok) << fixture.filename().string();
        ASSERT_NE(loaded.document, nullptr) << fixture.filename().string();

        std::map<std::string, std::string> before;
        for (const sacm_adapter::DocumentElement& element : sacm_adapter::list_document_elements(*loaded.document)) {
            if (element.id.empty())
                continue;
            before[element.id] = element.attributes;
            for (const auto& [field, value] : ParseFields(element.attributes))
                attributes_seen.insert(field);
        }

        const sacm::AssuranceCasePackage package = core::project_library_package_with_tags(*loaded.document);
        if (!sacm_adapter::reload_document_keeping_compatibility_content(*loaded.document,
                                                                         sacm::serialize_sacm(package))) {
            // Already measured, and named, by the kind sweep above.
            continue;
        }

        for (const sacm_adapter::DocumentElement& element : sacm_adapter::list_document_elements(*loaded.document)) {
            const auto it = before.find(element.id);
            // An element the round trip DELETED is the kind sweep's business;
            // this one only speaks for elements that survived.
            if (it == before.end())
                continue;
            ++compared;
            if (it->second == element.attributes)
                continue;

            const std::map<std::string, std::string> was = ParseFields(it->second);
            const std::map<std::string, std::string> is = ParseFields(element.attributes);
            for (const auto& [field, value] : was) {
                const std::string key = element.kind + "." + field;
                if (const auto after = is.find(field); after != is.end()) {
                    // Present on both sides: the attribute survived, so what
                    // matters is whether it still says the same thing. A CHANGED
                    // value is never disclosed and never acceptable -- the
                    // element still claims the attribute and now claims
                    // something different by it.
                    EXPECT_EQ(value, after->second)
                        << fixture.filename().string() << ": a bridged edit CHANGES '" << key << "' on '" << element.id
                        << "' from '" << value << "' to '" << after->second << "'.";
                    continue;
                }
                EXPECT_TRUE(KnownLostAttributes().count(key) > 0)
                    << fixture.filename().string() << ": a bridged edit now DROPS '" << key << "' from '" << element.id
                    << "', which SURVIVED the round trip, and it is not on the known-lost list. An element that "
                    << "survives with an attribute missing is a silent reinterpretation of the argument -- worse "
                    << "than deleting it visibly, because the inventory still balances. Either teach the "
                    << "projection to carry it, or add it to the list AND to the SACM23-LIB-002 disclosure.";
                lost_attributes.insert(key);
            }
            // An attribute the round trip INVENTED. Never disclosable, for the
            // same reason a changed value is not: `isCounter` is emitted only
            // when true, so a false->true addition here is the original
            // rebuttal-becomes-support defect running in reverse, and checking
            // only the fields present beforehand would not see it.
            for (const auto& [field, value] : is) {
                if (was.count(field) > 0)
                    continue;
                ADD_FAILURE() << fixture.filename().string() << ": a bridged edit ADDS '" << element.kind << "."
                              << field << "' = '" << value << "' to '" << element.id
                              << "', which had no such attribute before. The projection round trip must not invent "
                              << "an attribute: the element now asserts something the source document did not.";
            }
        }
    }

    // The list must not outlive the loss: an attribute that starts surviving
    // has to come off it, or the disclosure overstates the damage forever.
    for (const std::string& known : KnownLostAttributes()) {
        EXPECT_TRUE(lost_attributes.count(known) > 0)
            << "'" << known
            << "' now survives the bridge round trip (or no fixture carries it any more), but it is still listed "
               "as known-lost. Remove it from the list and from the SACM23-LIB-002 disclosure.";
    }

    EXPECT_GT(compared, 0u) << "the sweep compared no elements at all";
    // Non-vacuity in the dimension that matters: the corpus must actually carry
    // the meaning-bearing attributes, or an all-empty fingerprint would compare
    // equal forever and prove nothing.
    for (const char* meaningful : {"isCounter", "assertionDeclaration", "source", "target"}) {
        EXPECT_TRUE(attributes_seen.count(meaningful) > 0)
            << "no fixture carries '" << meaningful << "', so this sweep no longer measures it";
    }
}

// The sweep above is only as trustworthy as its `name=value;` encoding. The
// values in it are user data -- a Term's externalReference is a URL, and a query
// string routinely carries both `=` and `;` -- so an unescaped delimiter in a
// payload would shift every field after it and make the sweep report losses that
// did not happen, or miss ones that did.
TEST(ProjectionCoverage, SACM23_LIB_002_AttributeFingerprintSurvivesDelimitersInValues) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "af-fingerprint-delimiters.sacm.xmi";
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << R"(<?xml version="1.0" encoding="UTF-8"?>)"
               R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
               R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
               R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">)"
               R"(<name content="Case"/>)"
               R"(<terminologyPackage xmi:id="tp_1"><name content="Vocabulary"/>)"
               R"(<terminologyElement xsi:type="sacm:Term" xmi:id="term_nasty" value="a=b;c=d" )"
               R"(externalReference="https://example.org/s?id=68383;rev=2"><name content="Nasty"/>)"
               R"(</terminologyElement></terminologyPackage></sacm:AssuranceCasePackage>)";
    }
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    std::filesystem::remove(path);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);

    std::string fingerprint;
    for (const sacm_adapter::DocumentElement& element : sacm_adapter::list_document_elements(*loaded.document)) {
        if (element.id == "term_nasty")
            fingerprint = element.attributes;
    }
    ASSERT_FALSE(fingerprint.empty()) << "the fixture element was not found; this test measures nothing";

    // Non-vacuity: the raw delimiters must be gone from the rendered value, or
    // the encoder did nothing and the parse below succeeds by luck.
    EXPECT_EQ(fingerprint.find("a=b;c=d"), std::string::npos) << fingerprint;

    const std::map<std::string, std::string> fields = ParseFields(fingerprint);
    // Two fields, not the five an unescaped `a=b;c=d` plus the URL would split
    // into.
    EXPECT_EQ(fields.size(), 2u) << fingerprint;
    ASSERT_TRUE(fields.count("value") > 0) << fingerprint;
    EXPECT_EQ(fields.at("value"), "a%3Db%3Bc%3Dd");
    ASSERT_TRUE(fields.count("externalReference") > 0) << fingerprint;
    EXPECT_EQ(fields.at("externalReference"), "https://example.org/s?id%3D68383%3Brev%3D2");
}
