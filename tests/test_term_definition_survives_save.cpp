#include "sacm_adapter/document_edit.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

// Does a term's definition reach the file?
//
// Reported twice from the running application: an agent defines a glossary, the
// terms appear in the accepted argument carrying their value, category and
// external reference, and every definition is empty. The in-memory path is
// covered -- SACM23_INT_001 asserts the projection holds the description -- and
// the serializer is not.

namespace {

// The repository as the build knows it, the way every other fixture-reading
// test finds it. Walking up from the working directory would make the answer
// depend on where CTest happened to start the binary, and a wrong turn there
// reads as "fixture missing" and skips -- a test that skips on a bad guess is a
// test that stops covering the thing it was written for.
std::filesystem::path RepoRoot() {
    return std::filesystem::path(AF_REPO_ROOT);
}

} // namespace

TEST(TermDefinitionSurvivesSave, ADefinitionIsInTheSerializedDocument) {
    const std::filesystem::path root = RepoRoot();
    // Present or the checkout is broken. Skipping here would quietly stop
    // covering the serialization this test exists for.
    ASSERT_TRUE(std::filesystem::exists(root / "tests" / "data" / "fixture_terminology_parity.sacm.xml"))
        << "fixture missing under " << root;

    sacm_adapter::LoadOutcome loaded =
        sacm_adapter::load_document(root / "tests" / "data" / "fixture_terminology_parity.sacm.xml");
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);

    const sacm_adapter::TerminologyTermFields fields{.value = "ALARP",
                                                     .description = "As low as reasonably practicable."};
    const sacm_adapter::TerminologyCreateOutcome created =
        sacm_adapter::apply_create_terminology_term(*loaded.document, "TP1", fields, "T_DEF");
    ASSERT_TRUE(created.applied) << (created.diagnostics.empty() ? "" : created.diagnostics.front().message);

    const sacm_adapter::SaveOutcome saved = sacm_adapter::save_document(*loaded.document);
    ASSERT_TRUE(saved.ok);

    EXPECT_NE(saved.xml.find("As low as reasonably practicable."), std::string::npos)
        << "the definition is not in the serialized document -- it exists only in memory";
    EXPECT_NE(saved.xml.find("ALARP"), std::string::npos) << "the term's value is missing too";
}

// The other way an agent sets a definition: create the term, then UpdateTerm
// with field "definition". That goes through `apply_text_edit`, not through the
// create's `fields.description`, so it is a different path to the same place.
TEST(TermDefinitionSurvivesSave, ADefinitionSetByUpdateTermIsInTheSerializedDocument) {
    const std::filesystem::path root = RepoRoot();
    // Present or the checkout is broken. Skipping here would quietly stop
    // covering the serialization this test exists for.
    ASSERT_TRUE(std::filesystem::exists(root / "tests" / "data" / "fixture_terminology_parity.sacm.xml"))
        << "fixture missing under " << root;

    sacm_adapter::LoadOutcome loaded =
        sacm_adapter::load_document(root / "tests" / "data" / "fixture_terminology_parity.sacm.xml");
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);

    const sacm_adapter::TerminologyTermFields fields{.value = "ALARP"};
    const sacm_adapter::TerminologyCreateOutcome created =
        sacm_adapter::apply_create_terminology_term(*loaded.document, "TP1", fields, "T_UPD");
    ASSERT_TRUE(created.applied) << (created.diagnostics.empty() ? "" : created.diagnostics.front().message);

    const sacm_adapter::EditOutcome edited = sacm_adapter::apply_text_edit(
        *loaded.document, "T_UPD", sacm_adapter::TextField::Description, "en", "As low as reasonably practicable.");
    EXPECT_TRUE(edited.supported) << "UpdateTerm's definition write is not supported for a term";
    EXPECT_TRUE(edited.applied) << (edited.diagnostics.empty() ? "" : edited.diagnostics.front().message);

    const sacm_adapter::SaveOutcome saved = sacm_adapter::save_document(*loaded.document);
    ASSERT_TRUE(saved.ok);
    EXPECT_NE(saved.xml.find("As low as reasonably practicable."), std::string::npos)
        << "a definition set through UpdateTerm never reached the document";
}
