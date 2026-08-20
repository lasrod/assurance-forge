#include "core/reviews/review_proposal.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

// An operation is a change to a safety argument, so a key the parser does not
// know has to be refused rather than dropped. The case that produced these
// tests: a glossary staged over MCP arrived with every term carrying its word,
// its category and its external reference, and no definition at all. The
// operations had said `"definition"`, the parser reads `"new_value"`, and
// nothing between the two said so.

namespace {

nlohmann::json CreateTermWith(const std::string& key, const std::string& value) {
    return nlohmann::json{{"type", "CreateTerm"}, {"create_ref", "$term"}, {"text", "ALARP"}, {key, value}};
}

} // namespace

TEST(PatchOperationParsingTest, ATermDefinitionUnderTheWrongKeyIsRefusedRatherThanDropped) {
    core::reviews::PatchOperation operation;
    std::string error;

    const bool parsed = core::reviews::ParsePatchOperationJson(
        CreateTermWith("definition", "As low as reasonably practicable."), operation, error);

    ASSERT_FALSE(parsed) << "a definition the tool will not store must not parse as a defined term";
    EXPECT_NE(error.find("definition"), std::string::npos) << "the refusal must name the key it refused: " << error;
    EXPECT_NE(error.find("new_value"), std::string::npos)
        << "the refusal must say where the definition belongs: " << error;
}

TEST(PatchOperationParsingTest, AnUnknownKeyIsNamedAndTheAcceptedOnesListed) {
    core::reviews::PatchOperation operation;
    std::string error;

    const bool parsed = core::reviews::ParsePatchOperationJson(CreateTermWith("nonsense", "value"), operation, error);

    ASSERT_FALSE(parsed);
    EXPECT_NE(error.find("nonsense"), std::string::npos) << error;
    // Listed, because an agent that cannot see the vocabulary guesses again.
    EXPECT_NE(error.find("new_value"), std::string::npos) << error;
    EXPECT_NE(error.find("translations"), std::string::npos) << error;
}

// The refusal must not cost the vocabulary that works: every key the parser
// reads has to keep parsing, or this guard trades a silent drop for a wall.
TEST(PatchOperationParsingTest, EveryAcceptedKeyStillParses) {
    const nlohmann::json source{
        {"type", "UpdateTerm"},
        {"create_ref", "$ignored"},
        {"element", {{"id", "T1"}}},
        {"source", {{"id", "G1"}}},
        {"target", {{"ref", "$goal"}}},
        {"field", "definition"},
        {"old_value", "The old definition."},
        {"new_value", "The new definition."},
        {"text", "ALARP"},
        {"translations", {{"ja", "合理的に実行可能な限り低く"}}},
    };

    core::reviews::PatchOperation operation;
    std::string error;
    ASSERT_TRUE(core::reviews::ParsePatchOperationJson(source, operation, error)) << error;
    EXPECT_EQ(operation.field, "definition");
    EXPECT_EQ(operation.new_value, "The new definition.");
    EXPECT_EQ(operation.text, "ALARP");
    ASSERT_TRUE(operation.element.has_value());
    EXPECT_EQ(operation.element->existing_id.value_or(""), "T1");
    EXPECT_EQ(operation.translations.at("ja"), "合理的に実行可能な限り低く");
}

// A definition stated the way the schema documents still lands, so the guard
// cannot be mistaken for "CreateTerm no longer takes a definition".
TEST(PatchOperationParsingTest, ADefinitionInNewValueParsesAsTheTermsDefinition) {
    core::reviews::PatchOperation operation;
    std::string error;

    ASSERT_TRUE(core::reviews::ParsePatchOperationJson(
        CreateTermWith("new_value", "As low as reasonably practicable."), operation, error))
        << error;
    EXPECT_EQ(operation.type, core::reviews::PatchOperationType::CreateTerm);
    EXPECT_EQ(operation.text, "ALARP");
    EXPECT_EQ(operation.new_value, "As low as reasonably practicable.");
}
