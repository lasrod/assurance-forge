#include "sacm/sacm_package_tree.h"

#include <algorithm>
#include <cctype>
#include <pugixml.hpp>

namespace sacm {
namespace {

std::string local_name(const char* qualified_name) {
    if (!qualified_name)
        return {};
    std::string value(qualified_name);
    const auto colon = value.find(':');
    if (colon != std::string::npos)
        value = value.substr(colon + 1);
    return value;
}

std::string normalized_local_name(const char* qualified_name) {
    std::string value = local_name(qualified_name);
    std::transform(
        value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool is_package_like_name(const std::string& lower_local_name) {
    return lower_local_name.size() >= 7 && lower_local_name.find("package") != std::string::npos;
}

SacmPackageNodeType classify_package_node(const std::string& lower_local_name) {
    if (lower_local_name == "assurancecasepackage")
        return SacmPackageNodeType::AssuranceCasePackage;
    if (lower_local_name == "argumentpackage")
        return SacmPackageNodeType::ArgumentPackage;
    if (lower_local_name == "artifactpackage")
        return SacmPackageNodeType::ArtifactPackage;
    if (lower_local_name == "terminologypackage")
        return SacmPackageNodeType::TerminologyPackage;
    if (lower_local_name == "assurancecasepackageinterface")
        return SacmPackageNodeType::AssuranceCasePackageInterface;
    if (lower_local_name == "argumentpackageinterface")
        return SacmPackageNodeType::ArgumentPackageInterface;
    if (lower_local_name == "artifactpackageinterface")
        return SacmPackageNodeType::ArtifactPackageInterface;
    if (lower_local_name == "terminologypackageinterface")
        return SacmPackageNodeType::TerminologyPackageInterface;
    if (lower_local_name == "assurancecasepackagebinding")
        return SacmPackageNodeType::AssuranceCasePackageBinding;
    if (lower_local_name == "argumentpackagebinding")
        return SacmPackageNodeType::ArgumentPackageBinding;
    if (lower_local_name == "artifactpackagebinding")
        return SacmPackageNodeType::ArtifactPackageBinding;
    if (lower_local_name == "terminologypackagebinding")
        return SacmPackageNodeType::TerminologyPackageBinding;
    if (is_package_like_name(lower_local_name))
        return SacmPackageNodeType::UnknownPackage;
    return SacmPackageNodeType::UnknownPackage;
}

bool is_supported_package_root(const std::string& lower_local_name) {
    const SacmPackageNodeType type = classify_package_node(lower_local_name);
    return type != SacmPackageNodeType::UnknownPackage || is_package_like_name(lower_local_name);
}

std::string child_text(pugi::xml_node node, const char* lower_local_name) {
    for (auto child : node.children()) {
        if (normalized_local_name(child.name()) == lower_local_name) {
            std::string text = child.text().as_string();
            if (!text.empty())
                return text;
            for (auto grandchild : child.children()) {
                if (normalized_local_name(grandchild.name()) == "content") {
                    text = grandchild.text().as_string();
                    if (!text.empty())
                        return text;
                }
            }
        }
    }
    return {};
}

std::string read_name(pugi::xml_node node) {
    std::string name = node.attribute("name").as_string();
    if (!name.empty())
        return name;
    return child_text(node, "name");
}

std::string read_description(pugi::xml_node node) {
    std::string description = node.attribute("description").as_string();
    if (!description.empty())
        return description;
    return child_text(node, "description");
}

std::string fallback_display_name(pugi::xml_node node, const std::string& xml_local_name) {
    std::string name = read_name(node);
    if (!name.empty())
        return name;
    name = node.attribute("id").as_string();
    if (!name.empty())
        return name;
    name = node.attribute("gid").as_string();
    if (!name.empty())
        return name;
    return xml_local_name;
}

SacmPackageTreeNode parse_package_node(pugi::xml_node xml_node) {
    const std::string lower = normalized_local_name(xml_node.name());
    SacmPackageTreeNode node;
    node.id = xml_node.attribute("id").as_string();
    node.gid = xml_node.attribute("gid").as_string();
    node.xmlLocalName = local_name(xml_node.name());
    node.displayName = fallback_display_name(xml_node, node.xmlLocalName);
    node.description = read_description(xml_node);
    node.type = classify_package_node(lower);

    for (auto child : xml_node.children()) {
        const std::string child_lower = normalized_local_name(child.name());
        if (!is_supported_package_root(child_lower))
            continue;
        node.children.push_back(parse_package_node(child));
    }

    return node;
}

SacmPackageTreeResult parse_document(pugi::xml_document& doc, const std::string& file_display_name) {
    SacmPackageTreeResult result;
    result.root.type = SacmPackageNodeType::SacmFile;
    result.root.displayName = file_display_name.empty() ? "SACM file" : file_display_name;
    result.root.xmlLocalName = "SacmFile";

    for (auto child : doc.children()) {
        const std::string lower = normalized_local_name(child.name());
        if (!is_supported_package_root(lower))
            continue;
        result.root.children.push_back(parse_package_node(child));
    }

    if (result.root.children.empty()) {
        result.error_message = "No SACM package root found";
        return result;
    }

    result.success = true;
    return result;
}

} // namespace

const char* SacmPackageNodeTypeToString(SacmPackageNodeType type) {
    switch (type) {
    case SacmPackageNodeType::SacmFile:
        return "SacmFile";
    case SacmPackageNodeType::AssuranceCasePackage:
        return "AssuranceCasePackage";
    case SacmPackageNodeType::ArgumentPackage:
        return "ArgumentPackage";
    case SacmPackageNodeType::ArtifactPackage:
        return "ArtifactPackage";
    case SacmPackageNodeType::TerminologyPackage:
        return "TerminologyPackage";
    case SacmPackageNodeType::AssuranceCasePackageInterface:
        return "AssuranceCasePackageInterface";
    case SacmPackageNodeType::ArgumentPackageInterface:
        return "ArgumentPackageInterface";
    case SacmPackageNodeType::ArtifactPackageInterface:
        return "ArtifactPackageInterface";
    case SacmPackageNodeType::TerminologyPackageInterface:
        return "TerminologyPackageInterface";
    case SacmPackageNodeType::AssuranceCasePackageBinding:
        return "AssuranceCasePackageBinding";
    case SacmPackageNodeType::ArgumentPackageBinding:
        return "ArgumentPackageBinding";
    case SacmPackageNodeType::ArtifactPackageBinding:
        return "ArtifactPackageBinding";
    case SacmPackageNodeType::TerminologyPackageBinding:
        return "TerminologyPackageBinding";
    case SacmPackageNodeType::UnknownPackage:
        return "UnknownPackage";
    }
    return "UnknownPackage";
}

const char* SacmPackageNodeTypeToDisplayString(SacmPackageNodeType type) {
    return SacmPackageNodeTypeToString(type);
}

SacmPackageTreeResult build_sacm_package_tree(const std::filesystem::path& file_path) {
    SacmPackageTreeResult result;
    pugi::xml_document doc;
    const auto parse_result = doc.load_file(file_path.string().c_str());
    if (!parse_result) {
        result.error_message = "XML parse error: " + std::string(parse_result.description());
        result.source_path = file_path;
        return result;
    }

    result = parse_document(doc, file_path.filename().generic_string());
    result.source_path = file_path;
    result.root.id = file_path.generic_string();
    return result;
}

SacmPackageTreeResult build_sacm_package_tree_from_string(const std::string& xml_content,
                                                          const std::string& file_display_name) {
    SacmPackageTreeResult result;
    pugi::xml_document doc;
    const auto parse_result = doc.load_string(xml_content.c_str());
    if (!parse_result) {
        result.error_message = "XML parse error: " + std::string(parse_result.description());
        return result;
    }
    return parse_document(doc, file_display_name);
}

} // namespace sacm
