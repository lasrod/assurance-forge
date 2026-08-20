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

std::filesystem::path RepoRoot() {
    std::filesystem::path directory = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        if (std::filesystem::exists(directory / "tests" / "data" / "fixture_terminology_parity.sacm.xml"))
            return directory;
        if (!directory.has_parent_path() || directory.parent_path() == directory)
            break;
        directory = directory.parent_path();
    }
    return {};
}

} // namespace

TEST(TermDefinitionSurvivesSave, ADefinitionIsInTheSerializedDocument) {
    const std::filesystem::path root = RepoRoot();
    if (root.empty())
        GTEST_SKIP() << "fixture not found from " << std::filesystem::current_path();

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
    if (root.empty())
        GTEST_SKIP() << "fixture not found from " << std::filesystem::current_path();

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
