// Phase 9 Stage 5: the edit seam onto the library-owned document.
//
// Stage 4 made the library the load source of truth (the app projects
// `loaded_case` from a library document). Editing still runs on the legacy
// models, which the audit log serializes and hashes and the replayer
// re-applies. Stage 5 routes edits through the library one operation at a
// time; each must be proven to reproduce the legacy edit before the live path
// depends on it. This test is that proof for the first operation, `SetName`.
//
// It applies the *same* logical edit -- rename element G1 -- through both
// paths: `sacm_adapter::apply_set_name` on the library document, and
// `core::SetElementTextField` on the legacy models. It then asserts the
// library-edited projection carries the same name the legacy edit produces.
// If the library operation ever stops mapping onto the legacy behaviour, this
// fails before the live edit path is switched over.

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

// fixture_acp_parity carries a claim `G1` named "Top Goal" and loads cleanly
// through both the library and the legacy parser, so it isolates the edit
// behaviour from any load-side difference.
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

} // namespace

// SACM23-INT-001, edit slice: the library's SetName operation, projected back
// into the POD model, must match what the legacy name edit produces.
TEST(SacmLibraryEdit, SACM23_INT_001_SetNameReproducesLegacyNameEdit) {
    const std::filesystem::path path = fixture_path();
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();

    const std::string kNewName = "Revised Top Goal";

    // --- Library path: load, project baseline, apply SetName, re-project. ---
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);

    const core::AssuranceCase before = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* before_g1 = find_element(before, "G1");
    ASSERT_NE(before_g1, nullptr);
    // Guards the test against vacuity: the edit must actually change the name.
    ASSERT_EQ(before_g1->name, "Top Goal");
    ASSERT_NE(before_g1->name, kNewName);

    const sacm_adapter::EditOutcome edit =
        sacm_adapter::apply_set_name(*loaded.document, "G1", kNewName, "en");
    ASSERT_TRUE(edit.applied) << (edit.diagnostics.empty() ? "" : edit.diagnostics.front().message);

    const core::AssuranceCase after = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* after_g1 = find_element(after, "G1");
    ASSERT_NE(after_g1, nullptr);
    EXPECT_EQ(after_g1->name, kNewName);
    ASSERT_TRUE(after_g1->name_langs.contains("en"));
    EXPECT_EQ(after_g1->name_langs.at("en"), kNewName);

    // --- Legacy path: the same edit through element_factory. ---
    const auto legacy_case = parser::parse_sacm_xml(path.string());
    ASSERT_TRUE(legacy_case.has_value()) << legacy_case.error();
    const auto legacy_package = sacm::parse_sacm(path.string());
    ASSERT_TRUE(legacy_package.has_value()) << legacy_package.error();

    core::AssuranceCase legacy_edited = *legacy_case;
    sacm::AssuranceCasePackage legacy_pkg = *legacy_package;
    std::string old_value;
    std::string error;
    ASSERT_TRUE(core::SetElementTextField(legacy_edited, &legacy_pkg, "G1",
                                          core::ElementTextField::Name, "en", kNewName, old_value,
                                          error))
        << error;
    EXPECT_EQ(old_value, "Top Goal");

    const core::SacmElement* legacy_g1 = find_element(legacy_edited, "G1");
    ASSERT_NE(legacy_g1, nullptr);

    // --- Equivalence: the two paths agree on the edited name. ---
    EXPECT_EQ(after_g1->name, legacy_g1->name);
    ASSERT_TRUE(legacy_g1->name_langs.contains("en"));
    EXPECT_EQ(after_g1->name_langs.at("en"), legacy_g1->name_langs.at("en"));
}

// A rename targeting an id the document does not contain must fail cleanly:
// the library reports it and leaves the document unchanged, so a bad edit can
// never silently corrupt the source of truth.
TEST(SacmLibraryEdit, SACM23_INT_001_SetNameOnMissingElementFailsUnchanged) {
    const std::filesystem::path path = fixture_path();
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);

    const sacm_adapter::EditOutcome edit =
        sacm_adapter::apply_set_name(*loaded.document, "NO_SUCH_ID", "whatever", "en");
    EXPECT_FALSE(edit.applied);
    EXPECT_FALSE(edit.diagnostics.empty());

    // The rest of the document is untouched: G1 keeps its original name.
    const core::AssuranceCase projected = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* g1 = find_element(projected, "G1");
    ASSERT_NE(g1, nullptr);
    EXPECT_EQ(g1->name, "Top Goal");
}
