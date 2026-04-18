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

static pugi::xml_node find_child_by_local_name(pugi::xml_node node, const char* local_name) {
    for (pugi::xml_node child : node.children()) {
        if (get_local_name(child.name()) == local_name) return child;
    }
    return pugi::xml_node();
}

void extract_elements_recursive(pugi::xml_node node, std::vector<SacmElement>& elements) {
    for (pugi::xml_node child : node.children()) {
        std::string node_name = get_local_name(child.name());

        // Skip text nodes and processing instructions
        if (node_name.empty()) {
            continue;
        }

        // Check if this is an element we care about (match local-name so prefixes and case don't matter)
        bool is_relevant =
            node_name == "claim" ||
            node_name == "argumentreasoning" ||
            node_name == "artifact" ||
            node_name == "artifactreference" ||
            node_name == "expression" ||
            node_name == "assertedinference" ||
            node_name == "assertedcontext" ||
            node_name == "assertedevidence";

        if (is_relevant) {
            SacmElement elem;
            elem.id = child.attribute("id").as_string();
            elem.name = child.attribute("name").as_string();
            elem.type = node_name;
            elem.content = child.attribute("content").as_string();

            // For expression elements, content is in 'value' attribute
            if (node_name == "expression") {
                elem.content = child.attribute("value").as_string();
            }

            // Get description from child element (namespace-agnostic) or attribute
            pugi::xml_node desc_node = find_child_by_local_name(child, "description");
            if (desc_node) {
                elem.description = desc_node.text().as_string();
            } else {
                elem.description = child.attribute("description").as_string();
            }

            elements.push_back(elem);
        }

        // Recurse into child nodes
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
