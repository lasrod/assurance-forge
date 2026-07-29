#pragma once

// SACM data-model PODs used across parser, core, and ui layers.
//
// These types previously lived in `parser::xml_parser.h`, which forced every
// UI header that referenced a `SacmElement` or `AssuranceCase` to include the
// parser implementation header. Hosting them in `core::` keeps the UI layer
// free of parser-layer dependencies while parser/* keeps backwards-compatible
// type aliases (see parser/xml_parser.h).

#include <map>
#include <string>
#include <vector>

namespace core {

// Represents a SACM element (claim, strategy, evidence, etc.)
struct SacmElement {
    std::string id;
    std::string gid;
    std::string name;
    std::string type; // "claim", "argumentreasoning", "artifact", etc. (lowercased local-name)
    std::string content;
    std::string description;
    bool undeveloped = false;
    // SACMElement::isAbstract, used by GSN pattern notation to mean the
    // element remains to be instantiated.
    bool is_abstract = false;

    // Multi-language maps: lang code -> text (e.g. "en" -> "...", "ja" -> "...")
    std::map<std::string, std::string> name_langs;
    std::map<std::string, std::string> description_langs;
    std::map<std::string, std::string> content_langs;

    // Relationship fields (populated for assertedinference, assertedcontext, assertedevidence)
    std::vector<std::string> source_refs;     // ids from <source ref="..."/>
    std::vector<std::string> target_refs;     // ids from <target ref="..."/>
    std::vector<std::string> meta_claim_refs; // ids from <metaClaim ref="..."/>
    std::string reasoning_ref;                // from reasoning attribute (assertedinference)
    std::string assertion_declaration;        // from assertionDeclaration attribute
    bool is_counter = false;                  // from isCounter attribute (GSN dialectic challenge)
};

struct AcpRecord {
    std::string id;
    std::string name;
    std::string target_kind; // "relationship" or "element"
    std::string target_id;
    std::string resolution_kind; // "none", "text", or "topGoalReference"
    std::string text;
    std::string confidence_claim_id;
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

} // namespace core

// Transitional aliases: the SACM POD types historically lived in
// `namespace parser`. Until call sites are renamed to `core::Xxx`, expose the
// old spellings so consumers that previously included `parser/xml_parser.h`
// can switch to this header without source-wide renames.
namespace parser {
using SacmElement = core::SacmElement;
using AcpRecord = core::AcpRecord;
using AssuranceCase = core::AssuranceCase;
} // namespace parser
