#include <gtest/gtest.h>
#include "parser/xml_parser.h"

using namespace parser;

// Test parsing a minimal valid SACM XML
TEST(XmlParserTest, ParseMinimalValidXml) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage
    xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    id="TEST" name="Test Case" description="A test assurance case">
    <argumentPackage id="AP1" name="Main Argument">
        <claim id="G1" name="Goal 1" content="The system is safe.">
            <description>Top-level safety claim</description>
        </claim>
    </argumentPackage>
</sacm:AssuranceCasePackage>)";

    ParseResult result = parse_sacm_xml_string(xml);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.assurance_case.id, "TEST");
    EXPECT_EQ(result.assurance_case.name, "Test Case");
    EXPECT_EQ(result.assurance_case.description, "A test assurance case");
    EXPECT_EQ(result.assurance_case.elements.size(), 1);

    const auto& claim = result.assurance_case.elements[0];
    EXPECT_EQ(claim.id, "G1");
    EXPECT_EQ(claim.name, "Goal 1");
    EXPECT_EQ(claim.type, "claim");
    EXPECT_EQ(claim.content, "The system is safe.");
    EXPECT_EQ(claim.description, "Top-level safety claim");
}

// Test parsing XML with multiple element types
TEST(XmlParserTest, ParseMultipleElementTypes) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage
    xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    id="TEST" name="Test" description="Test">
    <argumentPackage id="AP1" name="Args">
        <claim id="G1" name="Goal" content="Safe"/>
        <argumentReasoning id="S1" name="Strategy" content="Appeal to evidence"/>
        <artifactReference id="EV1" name="Evidence" referencedArtifact="ART1"/>
    </argumentPackage>
</sacm:AssuranceCasePackage>)";

    ParseResult result = parse_sacm_xml_string(xml);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.assurance_case.elements.size(), 3);

    // Check types are correctly identified
    EXPECT_EQ(result.assurance_case.elements[0].type, "claim");
    EXPECT_EQ(result.assurance_case.elements[1].type, "argumentReasoning");
    EXPECT_EQ(result.assurance_case.elements[2].type, "artifactReference");
}

// Test parsing expression elements (terminology)
TEST(XmlParserTest, ParseTerminologyExpressions) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage
    xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    id="TEST" name="Test" description="Test">
    <terminologyPackage id="TP1" name="Terms">
        <expression id="TERM1" value="ISO 26262 - Functional Safety"/>
    </terminologyPackage>
</sacm:AssuranceCasePackage>)";

    ParseResult result = parse_sacm_xml_string(xml);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.assurance_case.elements.size(), 1);
    EXPECT_EQ(result.assurance_case.elements[0].type, "expression");
    EXPECT_EQ(result.assurance_case.elements[0].content, "ISO 26262 - Functional Safety");
}

// Test parsing invalid XML
TEST(XmlParserTest, ParseInvalidXml) {
    const char* xml = "This is not valid XML <>";

    ParseResult result = parse_sacm_xml_string(xml);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

// Test parsing XML without root element
TEST(XmlParserTest, ParseMissingRootElement) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<wrongElement id="TEST" name="Test"/>)";

    ParseResult result = parse_sacm_xml_string(xml);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error_message.find("AssuranceCasePackage") != std::string::npos);
}

// Test parsing empty elements list
TEST(XmlParserTest, ParseEmptyAssuranceCase) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage
    xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    id="EMPTY" name="Empty Case" description="No elements">
</sacm:AssuranceCasePackage>)";

    ParseResult result = parse_sacm_xml_string(xml);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.assurance_case.elements.size(), 0);
}

// Test file not found
TEST(XmlParserTest, ParseNonExistentFile) {
    ParseResult result = parse_sacm_xml("nonexistent_file_12345.xml");

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}
