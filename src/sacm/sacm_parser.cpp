#include "sacm/sacm_parser.h"
#include <pugixml.hpp>
#include <algorithm>
#include <cctype>

namespace sacm {

namespace {

// Strip namespace prefix and lowercase the local name.
static std::string local_name(const char* name) {
    if (!name) return {};
    std::string s(name);
    auto pos = s.find(':');
    std::string local = (pos != std::string::npos) ? s.substr(pos + 1) : s;
    std::transform(local.begin(), local.end(), local.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return local;
}

// Find a direct child element by its lowercased local name.
static pugi::xml_node find_child(pugi::xml_node node, const char* name) {
    for (auto child : node.children()) {
        if (local_name(child.name()) == name) return child;
    }
    return {};
}

// Read a ref from an element: try "ref" then "href", strip leading '#'.
static std::string get_ref(pugi::xml_node node) {
    std::string ref = node.attribute("ref").as_string();
    if (ref.empty()) ref = node.attribute("href").as_string();
    if (!ref.empty() && ref[0] == '#') ref = ref.substr(1);
    return ref;
}

// Extract description text from a node. Handles:
//   <description><content lang="en">text</content></description>
//   <description>text</description>
//   description="text" attribute
static std::string extract_description(pugi::xml_node node) {
    auto desc_node = find_child(node, "description");
    if (desc_node) {
        auto content_node = find_child(desc_node, "content");
        if (content_node) {
            return content_node.text().as_string();
        }
        return desc_node.text().as_string();
    }
    return node.attribute("description").as_string();
}

// Parse base SacmElement fields.
static void parse_base(pugi::xml_node node, SacmElement& elem) {
    elem.id = node.attribute("id").as_string();
    elem.name = node.attribute("name").as_string();
    elem.description = extract_description(node);
}

// Parse source and target child refs into an AssertedRelationship.
static void parse_relationship_refs(pugi::xml_node node, AssertedRelationship& rel) {
    for (auto child : node.children()) {
        std::string ln = local_name(child.name());
        if (ln == "source") {
            std::string r = get_ref(child);
            if (!r.empty()) rel.sources.push_back(r);
        } else if (ln == "target") {
            std::string r = get_ref(child);
            if (!r.empty()) rel.targets.push_back(r);
        }
    }
    rel.assertionDeclaration = node.attribute("assertionDeclaration").as_string();
}

// ===== Container parsers =====

static Expression parse_expression(pugi::xml_node node) {
    Expression e;
    parse_base(node, e);
    e.value = node.attribute("value").as_string();
    return e;
}

static TerminologyPackage parse_terminology_package(pugi::xml_node node) {
    TerminologyPackage tp;
    parse_base(node, tp);
    for (auto child : node.children()) {
        if (local_name(child.name()) == "expression") {
            tp.expressions.push_back(parse_expression(child));
        }
    }
    return tp;
}

static Artifact parse_artifact(pugi::xml_node node) {
    Artifact a;
    parse_base(node, a);
    return a;
}

static ArtifactPackage parse_artifact_package(pugi::xml_node node) {
    ArtifactPackage ap;
    parse_base(node, ap);
    for (auto child : node.children()) {
        if (local_name(child.name()) == "artifact") {
            ap.artifacts.push_back(parse_artifact(child));
        }
    }
    return ap;
}

static Claim parse_claim(pugi::xml_node node) {
    Claim c;
    parse_base(node, c);
    c.content = node.attribute("content").as_string();
    // Fallback: read from <statement> child element
    if (c.content.empty()) {
        auto stmt = find_child(node, "statement");
        if (stmt) c.content = stmt.text().as_string();
    }
    c.assertionDeclaration = node.attribute("assertionDeclaration").as_string();
    return c;
}

static ArgumentReasoning parse_argument_reasoning(pugi::xml_node node) {
    ArgumentReasoning ar;
    parse_base(node, ar);
    ar.content = node.attribute("content").as_string();
    return ar;
}

static ArtifactReference parse_artifact_reference(pugi::xml_node node) {
    ArtifactReference ar;
    parse_base(node, ar);
    ar.referencedArtifact = node.attribute("referencedArtifact").as_string();
    return ar;
}

static AssertedInference parse_asserted_inference(pugi::xml_node node) {
    AssertedInference ai;
    parse_base(node, ai);
    parse_relationship_refs(node, ai);

    // Reasoning: try child element first, then attribute
    auto reasoning_child = find_child(node, "reasoning");
    if (reasoning_child) {
        ai.reasoning = get_ref(reasoning_child);
    }
    if (ai.reasoning.empty()) {
        std::string attr = node.attribute("reasoning").as_string();
        if (!attr.empty()) {
            if (attr[0] == '#') attr = attr.substr(1);
            ai.reasoning = attr;
        }
    }
    return ai;
}

static AssertedContext parse_asserted_context(pugi::xml_node node) {
    AssertedContext ac;
    parse_base(node, ac);
    parse_relationship_refs(node, ac);
    return ac;
}

static AssertedEvidence parse_asserted_evidence(pugi::xml_node node) {
    AssertedEvidence ae;
    parse_base(node, ae);
    parse_relationship_refs(node, ae);
    return ae;
}

static ArgumentPackage parse_argument_package(pugi::xml_node node) {
    ArgumentPackage pkg;
    parse_base(node, pkg);

    for (auto child : node.children()) {
        std::string ln = local_name(child.name());
        if (ln == "claim")                pkg.claims.push_back(parse_claim(child));
        else if (ln == "argumentreasoning") pkg.argumentReasonings.push_back(parse_argument_reasoning(child));
        else if (ln == "artifactreference") pkg.artifactReferences.push_back(parse_artifact_reference(child));
        else if (ln == "assertedinference") pkg.assertedInferences.push_back(parse_asserted_inference(child));
        else if (ln == "assertedcontext")   pkg.assertedContexts.push_back(parse_asserted_context(child));
        else if (ln == "assertedevidence")  pkg.assertedEvidences.push_back(parse_asserted_evidence(child));
    }
    return pkg;
}

// Extract namespace prefix and URI from the root element.
static void extract_namespace(pugi::xml_node root, std::string& prefix, std::string& uri) {
    // Default values
    prefix = "sacm";
    uri = "http://www.omg.org/spec/SACM/2.2/Argumentation";

    for (auto attr : root.attributes()) {
        std::string attr_name = attr.name();
        if (attr_name.rfind("xmlns:", 0) == 0) {
            std::string candidate_prefix = attr_name.substr(6);
            std::string candidate_uri = attr.as_string();
            // Take the first xmlns: that looks SACM-related
            std::string lower_uri = candidate_uri;
            std::transform(lower_uri.begin(), lower_uri.end(), lower_uri.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lower_uri.find("sacm") != std::string::npos) {
                prefix = candidate_prefix;
                uri = candidate_uri;
                return;
            }
        }
    }
}

static SacmParseResult parse_document(pugi::xml_document& doc) {
    SacmParseResult result;

    // Find root AssuranceCasePackage (namespace-agnostic)
    pugi::xml_node root;
    for (auto child : doc.children()) {
        if (local_name(child.name()) == "assurancecasepackage") {
            root = child;
            break;
        }
    }
    if (!root) {
        result.error_message = "Root element 'AssuranceCasePackage' not found";
        return result;
    }

    auto& pkg = result.package;
    parse_base(root, pkg);
    extract_namespace(root, pkg.namespace_prefix, pkg.namespace_uri);

    // Parse child containers
    for (auto child : root.children()) {
        std::string ln = local_name(child.name());
        if (ln == "terminologypackage") {
            pkg.terminologyPackages.push_back(parse_terminology_package(child));
        } else if (ln == "artifactpackage") {
            pkg.artifactPackages.push_back(parse_artifact_package(child));
        } else if (ln == "argumentpackage") {
            pkg.argumentPackages.push_back(parse_argument_package(child));
        }
    }

    result.success = true;
    return result;
}

}  // namespace

SacmParseResult parse_sacm(const std::string& file_path) {
    SacmParseResult result;
    pugi::xml_document doc;
    auto status = doc.load_file(file_path.c_str());
    if (!status) {
        result.error_message = "XML parse error: " + std::string(status.description());
        return result;
    }
    return parse_document(doc);
}

SacmParseResult parse_sacm_string(const std::string& xml_content) {
    SacmParseResult result;
    pugi::xml_document doc;
    auto status = doc.load_string(xml_content.c_str());
    if (!status) {
        result.error_message = "XML parse error: " + std::string(status.description());
        return result;
    }
    return parse_document(doc);
}

}  // namespace sacm
