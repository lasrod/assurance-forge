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

            // Description: try child element first, fall back to attribute
            pugi::xml_node desc_node = find_child_by_local_name(child, "description");
            if (desc_node) {
                element.description = desc_node.text().as_string();
                pugi::xml_node content_node = find_child_by_local_name(desc_node, "content");
                if (content_node) {
                    element.description = content_node.text().as_string();
                }
            } else {
                element.description = child.attribute("description").as_string();
            }

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
