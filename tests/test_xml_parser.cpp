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
    EXPECT_EQ(result.assurance_case.elements[1].type, "argumentreasoning");
    EXPECT_EQ(result.assurance_case.elements[2].type, "artifactreference");
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

// Test that relationship elements have source/target refs parsed
TEST(XmlParserTest, ParseRelationshipSourceTargetRefs) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage
    xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    id="TEST" name="Test" description="Test">
    <argumentPackage id="AP1" name="Args">
        <claim id="cl_top" name="Top" content="Top claim"/>
        <claim id="cl_sub" name="Sub" content="Sub claim"/>
        <artifactReference id="ctx_1" name="Context1"/>
        <artifactReference id="ev_1" name="Evidence1"/>
        <assertedInference id="inf_1" name="Inf1" reasoning="ar_1">
            <source ref="cl_sub"/>
            <target ref="cl_top"/>
        </assertedInference>
        <assertedContext id="acx_1" name="Ctx1">
            <source ref="ctx_1"/>
            <target ref="cl_top"/>
        </assertedContext>
        <assertedEvidence id="ae_1" name="Ev1">
            <source ref="ev_1"/>
            <target ref="cl_sub"/>
        </assertedEvidence>
    </argumentPackage>
</sacm:AssuranceCasePackage>)";

    ParseResult result = parse_sacm_xml_string(xml);
    ASSERT_TRUE(result.success);

    // Find the assertedInference element
    const SacmElement* inf = nullptr;
    const SacmElement* ctx = nullptr;
    const SacmElement* ev = nullptr;
    for (const auto& e : result.assurance_case.elements) {
        if (e.id == "inf_1") inf = &e;
        if (e.id == "acx_1") ctx = &e;
        if (e.id == "ae_1") ev = &e;
    }

    ASSERT_NE(inf, nullptr);
    EXPECT_EQ(inf->type, "assertedinference");
    ASSERT_EQ(inf->source_refs.size(), 1);
    EXPECT_EQ(inf->source_refs[0], "cl_sub");
    ASSERT_EQ(inf->target_refs.size(), 1);
    EXPECT_EQ(inf->target_refs[0], "cl_top");
    EXPECT_EQ(inf->reasoning_ref, "ar_1");

    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(ctx->type, "assertedcontext");
    ASSERT_EQ(ctx->source_refs.size(), 1);
    EXPECT_EQ(ctx->source_refs[0], "ctx_1");

    ASSERT_NE(ev, nullptr);
    EXPECT_EQ(ev->type, "assertedevidence");
    ASSERT_EQ(ev->source_refs.size(), 1);
    EXPECT_EQ(ev->source_refs[0], "ev_1");
}

// Test parsing assertionDeclaration attribute
TEST(XmlParserTest, ParseAssertionDeclaration) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage
    xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    id="TEST" name="Test" description="Test">
    <argumentPackage id="AP1" name="Args">
        <claim id="cl_1" name="Asserted" assertionDeclaration="asserted"/>
        <claim id="cl_2" name="Assumed" assertionDeclaration="assumed"/>
    </argumentPackage>
</sacm:AssuranceCasePackage>)";

    ParseResult result = parse_sacm_xml_string(xml);
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.assurance_case.elements.size(), 2);
    EXPECT_EQ(result.assurance_case.elements[0].assertion_declaration, "asserted");
    EXPECT_EQ(result.assurance_case.elements[1].assertion_declaration, "assumed");
}

// Test parsing href attributes with # prefix (OASC-style XML)
TEST(XmlParserTest, ParseHrefWithHashPrefix) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage
    xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    id="TEST" name="Test" description="Test">
    <argumentPackage id="AP1" name="Args">
        <claim id="G1" name="Goal 1" content="Top claim"/>
        <claim id="G3" name="Goal 3" content="Sub claim"/>
        <argumentReasoning id="S2" name="Strategy 2" content="Appeal"/>
        <artifactReference id="CTX1" name="Context 1"/>
        <artifactReference id="EV1" name="Evidence 1"/>
        <assertedInference id="AI1" name="Inf1">
            <source href="#G3"/>
            <target href="#G1"/>
            <reasoning href="#S2"/>
        </assertedInference>
        <assertedContext id="AC1" name="Ctx1">
            <source href="#CTX1"/>
            <target href="#S2"/>
        </assertedContext>
        <assertedEvidence id="AE1" name="Ev1">
            <source href="#EV1"/>
            <target href="#G3"/>
        </assertedEvidence>
    </argumentPackage>
</sacm:AssuranceCasePackage>)";

    ParseResult result = parse_sacm_xml_string(xml);
    ASSERT_TRUE(result.success);

    const SacmElement* inf = nullptr;
    const SacmElement* ctx = nullptr;
    const SacmElement* ev = nullptr;
    for (const auto& e : result.assurance_case.elements) {
        if (e.id == "AI1") inf = &e;
        if (e.id == "AC1") ctx = &e;
        if (e.id == "AE1") ev = &e;
    }

    // AssertedInference with href and reasoning child element
    ASSERT_NE(inf, nullptr);
    ASSERT_EQ(inf->source_refs.size(), 1);
    EXPECT_EQ(inf->source_refs[0], "G3");
    ASSERT_EQ(inf->target_refs.size(), 1);
    EXPECT_EQ(inf->target_refs[0], "G1");
    EXPECT_EQ(inf->reasoning_ref, "S2");

    // AssertedContext with href
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(ctx->source_refs.size(), 1);
    EXPECT_EQ(ctx->source_refs[0], "CTX1");
    ASSERT_EQ(ctx->target_refs.size(), 1);
    EXPECT_EQ(ctx->target_refs[0], "S2");

    // AssertedEvidence with href
    ASSERT_NE(ev, nullptr);
    ASSERT_EQ(ev->source_refs.size(), 1);
    EXPECT_EQ(ev->source_refs[0], "EV1");
    ASSERT_EQ(ev->target_refs.size(), 1);
    EXPECT_EQ(ev->target_refs[0], "G3");
}

// Test parsing elements across multiple argument packages
TEST(XmlParserTest, ParseCrossPackageReferences) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage
    xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    id="TEST" name="Test" description="Test">
    <argumentPackage id="AP_Main" name="Main">
        <claim id="G1" name="Goal 1" content="Top claim"/>
        <argumentReasoning id="S2" name="Strategy" content="Appeal"/>
        <assertedInference id="AI1" name="Inf1">
            <source href="#G3"/>
            <target href="#G1"/>
            <reasoning href="#S2"/>
        </assertedInference>
    </argumentPackage>
    <argumentPackage id="AP_Sub" name="Sub">
        <claim id="G3" name="Goal 3" content="Sub claim"/>
    </argumentPackage>
</sacm:AssuranceCasePackage>)";

    ParseResult result = parse_sacm_xml_string(xml);
    ASSERT_TRUE(result.success);

    // Should find elements from both packages
    bool found_g1 = false, found_g3 = false, found_s2 = false, found_ai1 = false;
    for (const auto& e : result.assurance_case.elements) {
        if (e.id == "G1") found_g1 = true;
        if (e.id == "G3") found_g3 = true;
        if (e.id == "S2") found_s2 = true;
        if (e.id == "AI1") {
            found_ai1 = true;
            // Cross-package ref should be parsed
            ASSERT_EQ(e.source_refs.size(), 1);
            EXPECT_EQ(e.source_refs[0], "G3");
        }
    }
    EXPECT_TRUE(found_g1);
    EXPECT_TRUE(found_g3);
    EXPECT_TRUE(found_s2);
    EXPECT_TRUE(found_ai1);
}

// Test multi-language description parsing
TEST(XmlParserTest, ParseMultiLangDescription) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage
    xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    id="TEST" name="Test" description="Test">
    <argumentPackage id="AP1" name="Args">
        <claim id="G1" name="Goal 1" content="System is safe.">
            <description>
                <content lang="en">Top safety claim</content>
                <content lang="ja">&#12488;&#12483;&#12503;&#23433;&#20840;&#20027;&#24373;</content>
            </description>
        </claim>
    </argumentPackage>
</sacm:AssuranceCasePackage>)";

    ParseResult result = parse_sacm_xml_string(xml);
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.assurance_case.elements.size(), 1);

    const auto& claim = result.assurance_case.elements[0];
    EXPECT_EQ(claim.description, "Top safety claim");

    // description_langs should have both en and ja
    auto en_it = claim.description_langs.find("en");
    ASSERT_NE(en_it, claim.description_langs.end());
    EXPECT_EQ(en_it->second, "Top safety claim");

    auto ja_it = claim.description_langs.find("ja");
    ASSERT_NE(ja_it, claim.description_langs.end());
    EXPECT_FALSE(ja_it->second.empty());
}

// Test that description without lang attribute defaults to en
TEST(XmlParserTest, ParseDescriptionDefaultLang) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage
    xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    id="TEST" name="Test" description="Test">
    <argumentPackage id="AP1" name="Args">
        <claim id="G1" name="Goal 1" content="Safe.">
            <description>Simple description</description>
        </claim>
    </argumentPackage>
</sacm:AssuranceCasePackage>)";

    ParseResult result = parse_sacm_xml_string(xml);
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.assurance_case.elements.size(), 1);
    const auto& claim = result.assurance_case.elements[0];
    EXPECT_EQ(claim.description, "Simple description");
}
