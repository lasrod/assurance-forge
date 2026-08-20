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

// The same drop, arriving through a key the parser does read. `StringArgument`
// returned an empty string for any non-string, so a definition sent as a number
// reached the applier as "no definition" and the operation reported success.
TEST(PatchOperationParsingTest, ADefinitionOfTheWrongTypeIsRefusedRatherThanEmptied) {
    core::reviews::PatchOperation operation;
    std::string error;

    const nlohmann::json source{
        {"type", "CreateTerm"}, {"create_ref", "$term"}, {"text", "ALARP"}, {"new_value", 12345}};

    ASSERT_FALSE(core::reviews::ParsePatchOperationJson(source, operation, error))
        << "a number cannot be a definition, and silently becoming an empty one is the defect";
    EXPECT_NE(error.find("new_value"), std::string::npos) << error;
    EXPECT_NE(error.find("string"), std::string::npos) << error;
}

TEST(PatchOperationParsingTest, EveryScalarFieldRefusesAWrongType) {
    for (const char* field : {"type", "create_ref", "field", "old_value", "new_value", "text"}) {
        nlohmann::json source{{"type", "UpdateElementText"}, {"element", {{"id", "G1"}}}, {"new_value", "text"}};
        source[field] = nlohmann::json::array({"not", "a", "string"});

        core::reviews::PatchOperation operation;
        std::string error;
        EXPECT_FALSE(core::reviews::ParsePatchOperationJson(source, operation, error))
            << "\"" << field << "\" accepted an array";
        EXPECT_NE(error.find(field), std::string::npos) << "the refusal must name the field: " << error;
    }
}

// Absent and null keep meaning absent -- the operation type decides what it
// needs, and a null is how a client says "nothing here".
TEST(PatchOperationParsingTest, ANullScalarIsTreatedAsAbsent) {
    core::reviews::PatchOperation operation;
    std::string error;

    const nlohmann::json source{{"type", "UpdateElementText"},
                                {"element", {{"id", "G1"}}},
                                {"new_value", "The revised claim."},
                                {"old_value", nullptr}};

    ASSERT_TRUE(core::reviews::ParsePatchOperationJson(source, operation, error)) << error;
    EXPECT_EQ(operation.old_value, "");
    EXPECT_EQ(operation.new_value, "The revised claim.");
}
