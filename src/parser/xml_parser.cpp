#include "parser/xml_parser.h"
#include <pugixml.hpp>
#include <string>
#include <algorithm>
#include <cctype>

namespace parser {

namespace {

static std::string get_local_name(const char* name) {
    if (!name) return std::string();
    std::string s(name);
    size_t pos = s.find(':');
    std::string local = (pos != std::string::npos) ? s.substr(pos + 1) : s;
    std::transform(local.begin(), local.end(), local.begin(), [](unsigned char c){ return std::tolower(c); });
    return local;
}

// Read a reference from an element, trying "ref" then "href", stripping leading '#'
static std::string get_ref(pugi::xml_node node) {
    std::string ref = node.attribute("ref").as_string();
    if (ref.empty()) ref = node.attribute("href").as_string();
    if (!ref.empty() && ref[0] == '#') ref = ref.substr(1);
    return ref;
}

static pugi::xml_node find_child_by_local_name(pugi::xml_node node, const char* local_name) {
    for (pugi::xml_node child : node.children()) {
        if (get_local_name(child.name()) == local_name) return child;
    }
    return pugi::xml_node();
}

// Extract description text and multi-language variants from a node.
// Populates element.description (primary) and element.description_langs (all languages).
static void extract_description(pugi::xml_node child, SacmElement& element) {
    pugi::xml_node desc_node = find_child_by_local_name(child, "description");
    if (desc_node) {
        bool found_content = false;
        for (pugi::xml_node content_child : desc_node.children()) {
            if (get_local_name(content_child.name()) == "content") {
                std::string lang = content_child.attribute("lang").as_string();
                if (lang.empty()) lang = "en";
                std::string text = content_child.text().as_string();
                if (!text.empty()) {
                    element.description_langs[lang] = text;
                }
                found_content = true;
            }
        }
        if (found_content) {
            // Primary description: prefer "en", fall back to first
            auto it = element.description_langs.find("en");
            if (it != element.description_langs.end()) {
                element.description = it->second;
            } else if (!element.description_langs.empty()) {
                element.description = element.description_langs.begin()->second;
            }
        } else {
            element.description = desc_node.text().as_string();
            if (!element.description.empty()) {
                element.description_langs["en"] = element.description;
            }
        }
    } else {
        element.description = child.attribute("description").as_string();
        if (!element.description.empty()) {
            element.description_langs["en"] = element.description;
        }
    }
}

// Extract multi-language name from <name> child element (same pattern as description).
// Falls back to the "name" attribute as primary "en" text.
static void extract_name(pugi::xml_node child, SacmElement& element) {
    pugi::xml_node name_node = find_child_by_local_name(child, "name");
    if (name_node) {
        bool found_content = false;
        for (pugi::xml_node content_child : name_node.children()) {
            if (get_local_name(content_child.name()) == "content") {
                std::string lang = content_child.attribute("lang").as_string();
                if (lang.empty()) lang = "en";
                std::string text = content_child.text().as_string();
                if (!text.empty()) {
                    element.name_langs[lang] = text;
                }
                found_content = true;
            }
        }
        if (found_content) {
            auto it = element.name_langs.find("en");
            if (it != element.name_langs.end()) {
                element.name = it->second;
            } else if (!element.name_langs.empty()) {
                element.name = element.name_langs.begin()->second;
            }
        } else {
            // <name> element without <content> children — use text directly
            std::string text = name_node.text().as_string();
            if (!text.empty()) {
                element.name = text;
                element.name_langs["en"] = text;
            }
        }
    }
    // If no <name> child, the name was already set from attribute.
    // Ensure it's in name_langs.
    if (element.name_langs.empty() && !element.name.empty()) {
        element.name_langs["en"] = element.name;
    }
}

// Extract multi-language content from <content lang="xx"> direct children of the element.
// These are distinct from <content> children inside <description> (which are description translations).
// If found, overrides the content attribute value.
static void extract_content(pugi::xml_node child, SacmElement& element) {
    bool found_lang_content = false;
    for (pugi::xml_node content_child : child.children()) {
        if (get_local_name(content_child.name()) == "content") {
            std::string lang = content_child.attribute("lang").as_string();
            if (lang.empty()) lang = "en";
            std::string text = content_child.text().as_string();
            if (!text.empty()) {
                element.content_langs[lang] = text;
                found_lang_content = true;
            }
        }
    }
    if (found_lang_content) {
        // Override content attribute with primary language from child elements
        auto it = element.content_langs.find("en");
        if (it != element.content_langs.end()) {
            element.content = it->second;
        } else if (!element.content_langs.empty()) {
            element.content = element.content_langs.begin()->second;
        }
    }
}

void extract_elements_recursive(pugi::xml_node node, std::vector<SacmElement>& elements) {
    for (pugi::xml_node child : node.children()) {
        std::string local_name = get_local_name(child.name());

        if (local_name.empty()) continue;

        bool is_relevant =
            local_name == "claim" ||
            local_name == "argumentreasoning" ||
            local_name == "artifact" ||
            local_name == "artifactreference" ||
            local_name == "expression" ||
            local_name == "assertedinference" ||
            local_name == "assertedcontext" ||
            local_name == "assertedevidence";

        if (is_relevant) {
            SacmElement element;
            element.id = child.attribute("id").as_string();
            element.name = child.attribute("name").as_string();
            element.type = local_name;
            element.content = child.attribute("content").as_string();
            element.assertion_declaration = child.attribute("assertionDeclaration").as_string();

            if (local_name == "expression") {
                element.content = child.attribute("value").as_string();
            }

            // Store content in multilang map
            if (!element.content.empty()) {
                element.content_langs["en"] = element.content;
            }

            // Description: extract with multi-language support
            extract_description(child, element);

            // Name: extract with multi-language support (overrides attribute if <name> child exists)
            extract_name(child, element);

            // Content: extract with multi-language support (overrides attribute if <content> children exist)
            extract_content(child, element);

            // Relationship elements: extract source/target refs and reasoning
            bool is_relationship_element =
                local_name == "assertedinference" ||
                local_name == "assertedcontext" ||
                local_name == "assertedevidence";

            if (is_relationship_element) {
                for (pugi::xml_node ref_child : child.children()) {
                    std::string ref_local_name = get_local_name(ref_child.name());
                    if (ref_local_name == "source") {
                        std::string ref = get_ref(ref_child);
                        if (!ref.empty()) element.source_refs.push_back(ref);
                    } else if (ref_local_name == "target") {
                        std::string ref = get_ref(ref_child);
                        if (!ref.empty()) element.target_refs.push_back(ref);
                    } else if (ref_local_name == "reasoning") {
                        std::string ref = get_ref(ref_child);
                        if (!ref.empty()) element.reasoning_ref = ref;
                    }
                }
                // Also check reasoning as attribute: reasoning="ar_1"
                if (element.reasoning_ref.empty()) {
                    std::string attr_reasoning = child.attribute("reasoning").as_string();
                    if (!attr_reasoning.empty()) {
                        if (attr_reasoning[0] == '#') attr_reasoning = attr_reasoning.substr(1);
                        element.reasoning_ref = attr_reasoning;
                    }
                }
            }

            elements.push_back(element);
        }

        extract_elements_recursive(child, elements);
    }
}

ParseResult parse_document(pugi::xml_document& doc) {
    ParseResult result;

    // Find the root AssuranceCasePackage element (namespace- and case-agnostic)
    pugi::xml_node root = find_child_by_local_name(doc, "assurancecasepackage");
    if (!root) {
        result.success = false;
        result.error_message = "Root element 'AssuranceCasePackage' not found";
        return result;
    }

    result.assurance_case.id = root.attribute("id").as_string();
    result.assurance_case.name = root.attribute("name").as_string();
    result.assurance_case.description = root.attribute("description").as_string();

    // Extract all relevant elements recursively
    extract_elements_recursive(root, result.assurance_case.elements);

    result.success = true;
    return result;
}

}  // namespace

ParseResult parse_sacm_xml(const std::string& file_path) {
    ParseResult result;
    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_file(file_path.c_str());

    if (!parse_result) {
        result.success = false;
        result.error_message = "XML parse error: " + std::string(parse_result.description());
        return result;
    }

    return parse_document(doc);
}

ParseResult parse_sacm_xml_string(const std::string& xml_content) {
    ParseResult result;
    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_string(xml_content.c_str());

    if (!parse_result) {
        result.success = false;
        result.error_message = "XML parse error: " + std::string(parse_result.description());
        return result;
    }

    return parse_document(doc);
}

}  // namespace parser
