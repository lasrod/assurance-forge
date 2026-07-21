// Phase 9 Stage 5: the edit seam onto the library-owned document.
//
// Stage 4 made the library the load source of truth (the app projects
// `loaded_case` from a library document). Editing still runs on the legacy
// models, which the audit log serializes/hashes and the replayer re-applies.
// Stage 5 routes edits through the library one operation at a time; each must
// be proven to reproduce the legacy edit before the live path depends on it.
// These tests are that proof for the text edits wired so far (Name, Content).
//
// Each applies the *same* logical edit through both paths --
// `sacm_adapter::apply_text_edit` on the library document and
// `core::SetElementTextField` on the legacy models -- and asserts the two
// agree on the *edited field*. Whole-case equality is deliberately not
// asserted: the library projection already diverges from the legacy parser on
// unrelated fields (recorded in the parallel-load baseline, e.g. a claim's
// `content=` statement surfacing as its Description per clause 8.9). The edit
// contract is that the field the user changed lands identically.

#include "sacm_adapter/document_edit.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

#include "core/element_factory.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_parser.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace {

std::filesystem::path repo_root() { return std::filesystem::path(AF_REPO_ROOT); }

// fixture_acp_parity carries claim `G1` ("Top Goal", content "The system is
// acceptably safe.") and asserted inference `R1`, and loads cleanly through
// both the library and the legacy parser, so it isolates edit behaviour from
// any load-side difference.
std::filesystem::path fixture_path() {
    return repo_root() / "tests" / "data" / "fixture_acp_parity.sacm.xml";
}

const core::SacmElement* find_element(const core::AssuranceCase& assurance_case,
                                      const std::string& id) {
    for (const core::SacmElement& element : assurance_case.elements) {
        if (element.id == id) {
            return &element;
        }
    }
    return nullptr;
}

// Loads the fixture through the library; fails the calling test if it cannot.
sacm_adapter::LoadOutcome load_fixture() {
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(fixture_path());
    EXPECT_TRUE(loaded.ok);
    EXPECT_NE(loaded.document, nullptr);
    return loaded;
}

// Runs the same field edit through the legacy models and returns the edited
// element, so a test can compare it against the library-projected result.
core::AssuranceCase legacy_edit(const std::string& element_id, core::ElementTextField field,
                                const std::string& language, const std::string& value) {
    const auto legacy_case = parser::parse_sacm_xml(fixture_path().string());
    EXPECT_TRUE(legacy_case.has_value());
    const auto legacy_package = sacm::parse_sacm(fixture_path().string());
    EXPECT_TRUE(legacy_package.has_value());

    core::AssuranceCase edited = *legacy_case;
    sacm::AssuranceCasePackage package = *legacy_package;
    std::string old_value;
    std::string error;
    EXPECT_TRUE(core::SetElementTextField(edited, &package, element_id, field, language, value,
                                          old_value, error))
        << error;
    return edited;
}

} // namespace

// SACM23-INT-001, edit slice: the library's SetName operation, projected back
// into the POD model, must match what the legacy name edit produces.
TEST(SacmLibraryEdit, SACM23_INT_001_SetNameReproducesLegacyNameEdit) {
    const std::string kNewName = "Revised Top Goal";

    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);

    const core::AssuranceCase before = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* before_g1 = find_element(before, "G1");
    ASSERT_NE(before_g1, nullptr);
    ASSERT_EQ(before_g1->name, "Top Goal");   // guards against a vacuous edit
    ASSERT_NE(before_g1->name, kNewName);

    const sacm_adapter::EditOutcome edit =
        sacm_adapter::apply_text_edit(*loaded.document, "G1", sacm_adapter::TextField::Name, "en",
                                      kNewName);
    ASSERT_TRUE(edit.supported);
    ASSERT_TRUE(edit.applied) << (edit.diagnostics.empty() ? "" : edit.diagnostics.front().message);

    const core::AssuranceCase after = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* after_g1 = find_element(after, "G1");
    ASSERT_NE(after_g1, nullptr);
    EXPECT_EQ(after_g1->name, kNewName);
    ASSERT_TRUE(after_g1->name_langs.contains("en"));
    EXPECT_EQ(after_g1->name_langs.at("en"), kNewName);

    const core::AssuranceCase legacy = legacy_edit("G1", core::ElementTextField::Name, "en", kNewName);
    const core::SacmElement* legacy_g1 = find_element(legacy, "G1");
    ASSERT_NE(legacy_g1, nullptr);
    EXPECT_EQ(after_g1->name, legacy_g1->name);
    ASSERT_TRUE(legacy_g1->name_langs.contains("en"));
    EXPECT_EQ(after_g1->name_langs.at("en"), legacy_g1->name_langs.at("en"));
}

// SACM23-INT-001, edit slice: a claim's `content` (its statement) maps to the
// library's SetDescription (clause 8.9); the edited content must match the
// legacy content edit.
TEST(SacmLibraryEdit, SACM23_INT_001_ContentEditReproducesLegacyStatementEdit) {
    const std::string kNewContent = "Hazards are controlled to an acceptable level.";

    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);

    const core::AssuranceCase before = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* before_g1 = find_element(before, "G1");
    ASSERT_NE(before_g1, nullptr);
    ASSERT_EQ(before_g1->content, "The system is acceptably safe.");  // vacuity guard
    ASSERT_NE(before_g1->content, kNewContent);

    const sacm_adapter::EditOutcome edit = sacm_adapter::apply_text_edit(
        *loaded.document, "G1", sacm_adapter::TextField::Content, "en", kNewContent);
    ASSERT_TRUE(edit.supported);
    ASSERT_TRUE(edit.applied) << (edit.diagnostics.empty() ? "" : edit.diagnostics.front().message);

    const core::AssuranceCase after = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* after_g1 = find_element(after, "G1");
    ASSERT_NE(after_g1, nullptr);
    EXPECT_EQ(after_g1->content, kNewContent);
    ASSERT_TRUE(after_g1->content_langs.contains("en"));
    EXPECT_EQ(after_g1->content_langs.at("en"), kNewContent);

    const core::AssuranceCase legacy =
        legacy_edit("G1", core::ElementTextField::Content, "en", kNewContent);
    const core::SacmElement* legacy_g1 = find_element(legacy, "G1");
    ASSERT_NE(legacy_g1, nullptr);
    EXPECT_EQ(after_g1->content, legacy_g1->content);
    ASSERT_TRUE(legacy_g1->content_langs.contains("en"));
    EXPECT_EQ(after_g1->content_langs.at("en"), legacy_g1->content_langs.at("en"));
}

// Content maps to a Description only for claim-like elements. On a relationship
// (AssertedInference R1) there is no such mapping, so the seam must report it
// unsupported and leave the document untouched rather than silently writing a
// Description the projection would never read back as content.
TEST(SacmLibraryEdit, SACM23_INT_001_ContentEditUnsupportedForRelationship) {
    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);

    const sacm_adapter::EditOutcome edit = sacm_adapter::apply_text_edit(
        *loaded.document, "R1", sacm_adapter::TextField::Content, "en", "not applicable");
    EXPECT_FALSE(edit.supported);
    EXPECT_FALSE(edit.applied);
}

// A rename targeting an id the document does not contain must fail cleanly: the
// mapping is supported but the library reports the operation and leaves the
// document unchanged, so a bad edit can never silently corrupt the source of
// truth.
TEST(SacmLibraryEdit, SACM23_INT_001_SetNameOnMissingElementFailsUnchanged) {
    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);

    const sacm_adapter::EditOutcome edit = sacm_adapter::apply_text_edit(
        *loaded.document, "NO_SUCH_ID", sacm_adapter::TextField::Name, "en", "whatever");
    EXPECT_TRUE(edit.supported);
    EXPECT_FALSE(edit.applied);
    EXPECT_FALSE(edit.diagnostics.empty());

    const core::AssuranceCase projected = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* g1 = find_element(projected, "G1");
    ASSERT_NE(g1, nullptr);
    EXPECT_EQ(g1->name, "Top Goal");
}
