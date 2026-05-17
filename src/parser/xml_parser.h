#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace parser {

// Represents a SACM element (claim, strategy, evidence, etc.)
struct SacmElement {
    std::string id;
    std::string gid;
    std::string name;
    std::string type; // "claim", "argumentreasoning", "artifact", etc. (lowercased local-name)
    std::string content;
    std::string description;
    bool undeveloped = false;

    // Multi-language maps: lang code -> text (e.g. "en" -> "...", "ja" -> "...")
    std::map<std::string, std::string> name_langs;
    std::map<std::string, std::string> description_langs;
    std::map<std::string, std::string> content_langs;

    // Relationship fields (populated for assertedinference, assertedcontext, assertedevidence)
    std::vector<std::string> source_refs; // ids from <source ref="..."/>
    std::vector<std::string> target_refs; // ids from <target ref="..."/>
    std::string reasoning_ref;            // from reasoning attribute (assertedinference)
    std::string assertion_declaration;    // from assertionDeclaration attribute
};

struct AcpRecord {
    std::string id;
    std::string target_kind; // "relationship" or "element"
    std::string target_id;
    std::string resolution_kind; // "none", "text", or "topGoalReference"
    std::string text;
    std::string argument_package_id;
    std::string top_goal_id;
};

// Represents a parsed SACM assurance case
struct AssuranceCase {
    std::string id;
    std::string name;
    std::string description;
    std::vector<SacmElement> elements;
    std::vector<AcpRecord> acps;
};

// Result of parsing operation
struct ParseResult {
    bool success = false;
    std::string error_message;
    AssuranceCase assurance_case;
};

// Parses a SACM XML file and returns the result
ParseResult parse_sacm_xml(const std::string& file_path);

// Parses SACM XML from a string buffer
ParseResult parse_sacm_xml_string(const std::string& xml_content);

} // namespace parser
