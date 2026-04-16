#pragma once

#include <string>
#include <vector>
#include <optional>

namespace parser {

// Represents a SACM element (claim, strategy, evidence, etc.)
struct SacmElement {
    std::string id;
    std::string name;
    std::string type;  // "claim", "argumentReasoning", "artifact", etc.
    std::string content;
    std::string description;
};

// Represents a parsed SACM assurance case
struct AssuranceCase {
    std::string id;
    std::string name;
    std::string description;
    std::vector<SacmElement> elements;
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

}  // namespace parser
