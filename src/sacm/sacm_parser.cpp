#include "sacm/sacm_parser.h"
#include <pugixml.hpp>
#include <algorithm>
#include <cctype>

// =============================================================================
// SACM XML parser
// -----------------------------------------------------------------------------
// Parses SACM 2.3-style XML into the in-memory model defined in sacm_model.h.
//
// XML element/attribute -> model field mapping (per SACM 2.3 clause):
//
//   AssuranceCasePackage  (9.2)  -> AssuranceCasePackage
//     id, name, description, gid, isCitation, isAbstract,
//     citedElement, abstractForm
//     terminologyPackage*  -> TerminologyPackage
//     artifactPackage*     -> ArtifactPackage
//     argumentPackage*     -> ArgumentPackage
//
//   ArgumentPackage       (11.4) -> ArgumentPackage
//     claim*               -> Claim
//     argumentReasoning*   -> ArgumentReasoning
//     artifactReference*   -> ArtifactReference
//     assertedInference*   -> AssertedInference
//     assertedContext*     -> AssertedContext
//     assertedEvidence*    -> AssertedEvidence
//
//   Claim                 (11.11)  content + assertionDeclaration
//   ArgumentReasoning     (11.12)  content
//   ArtifactReference     (11.9)   referencedArtifact (id)
//   AssertedRelationship  (11.13)  source*, target*, isCounter,
//                                   assertionDeclaration
//   AssertedInference     (11.14)  + reasoning (id of ArgumentReasoning)
//
// Multi-language text (SACM MultiLangString, 8.5) is read from any of:
//   - attribute on the element                     (single language, "en")
//   - <name>text</name> / <description>text</...>  (single language, "en")
//   - <name><content lang="..">..</content>...</name>   (multi-language)
//   - <content lang="..">..</content> direct children  (for <claim>/<argumentReasoning> content)
// =============================================================================

namespace sacm {

namespace {

// ---------- Generic XML helpers ---------------------------------------------

// Strip any namespace prefix and lowercase the local name.
// Used everywhere to make name comparisons namespace- and case-insensitive.
static std::string local_name(const char* qualified_name) {
    if (!qualified_name) return {};
    std::string s(qualified_name);
    auto colon_pos = s.find(':');
    std::string local = (colon_pos != std::string::npos) ? s.substr(colon_pos + 1) : s;
    std::transform(local.begin(), local.end(), local.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return local;
}

// Find a direct child element by its lowercased local name. Returns an
// empty node if not found.
static pugi::xml_node find_child(pugi::xml_node parent, const char* lowercase_local_name) {
    for (auto child : parent.children()) {
        if (local_name(child.name()) == lowercase_local_name) return child;
    }
    return {};
}

// Read an id reference from an element. Tries "ref" then "href" (XMI uses
// href="#id"); strips a leading '#' if present.
static std::string read_id_ref(pugi::xml_node node) {
    std::string ref = node.attribute("ref").as_string();
    if (ref.empty()) ref = node.attribute("href").as_string();
    if (!ref.empty() && ref[0] == '#') ref = ref.substr(1);
    return ref;
}

// Read an XML attribute as a boolean. SACM uses "true" / "false" strings; any
// other value (including empty) returns the supplied default.
static bool read_bool_attr(pugi::xml_node node, const char* attr_name, bool default_value) {
    const char* raw = node.attribute(attr_name).as_string(nullptr);
    if (!raw || !*raw) return default_value;
    std::string s(raw);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (s == "true" || s == "1") return true;
    if (s == "false" || s == "0") return false;
    return default_value;
}

// ---------- Multi-language text extraction ---------------------------------

// Read multi-language text following the conventions in this file's header.
//
// Lookup order:
//   1. <wrapper><content lang="..">..</content>...</wrapper> child elements
//      (any number of languages; missing lang defaults to "en").
//   2. <wrapper>plain text</wrapper>                       (single "en" entry)
//   3. attribute named `fallback_attr` on the parent       (single "en" entry)
//
// `wrapper_local_name` is the name of the wrapping child element to look for
// (e.g. "name" or "description"). Pass nullptr to skip wrapper lookup and
// only consider the attribute fallback. `fallback_attr` may be nullptr.
static MultiLangText extract_lang_text(pugi::xml_node parent,
                                       const char* wrapper_local_name,
                                       const char* fallback_attr) {
    MultiLangText out;

    if (wrapper_local_name) {
        auto wrapper = find_child(parent, wrapper_local_name);
        if (wrapper) {
            bool found_content_child = false;
            for (auto content_child : wrapper.children()) {
                if (local_name(content_child.name()) != "content") continue;
                found_content_child = true;
                std::string lang = content_child.attribute("lang").as_string();
                if (lang.empty()) lang = "en";
                std::string text = content_child.text().as_string();
                if (!text.empty()) out.set(lang, text);
            }
            if (!found_content_child) {
                std::string text = wrapper.text().as_string();
                if (!text.empty()) out.set("en", text);
            }
            return out;
        }
    }

    if (fallback_attr) {
        std::string text = parent.attribute(fallback_attr).as_string();
        if (!text.empty()) out.set("en", text);
    }
    return out;
}

// Extract multi-language content from <content lang="xx">..</content> elements
// that are direct children of the parent. This is the form used by Claim and
// ArgumentReasoning when content is written as child elements rather than as
// the "content" attribute.
static MultiLangText extract_inline_content(pugi::xml_node parent) {
    MultiLangText out;
    for (auto child : parent.children()) {
        if (local_name(child.name()) != "content") continue;
        std::string lang = child.attribute("lang").as_string();
        if (lang.empty()) lang = "en";
        std::string text = child.text().as_string();
        if (!text.empty()) out.set(lang, text);
    }
    return out;
}

// ---------- Common SACMElement parsing -------------------------------------

// Populate the SACMElement base fields (8.2) common to every element.
static void parse_element_base(pugi::xml_node node, SacmElement& element) {
    element.id = node.attribute("id").as_string();

    element.name_ml = extract_lang_text(node, "name", "name");
    element.name = element.name_ml.get_primary();

    element.description_ml = extract_lang_text(node, "description", "description");
    element.description = element.description_ml.get_primary();

    // SACM 2.3 SACMElement attributes (defaults match the spec).
    element.gid = node.attribute("gid").as_string();
    element.isCitation = read_bool_attr(node, "isCitation", false);
    element.isAbstract = read_bool_attr(node, "isAbstract", false);

    // citedElement / abstractForm may be expressed as attributes or as
    // dedicated child elements with href="#id".
    element.citedElement = node.attribute("citedElement").as_string();
    if (element.citedElement.empty()) {
        if (auto cited = find_child(node, "citedelement")) element.citedElement = read_id_ref(cited);
    }
    element.abstractForm = node.attribute("abstractForm").as_string();
    if (element.abstractForm.empty()) {
        if (auto abs_form = find_child(node, "abstractform")) element.abstractForm = read_id_ref(abs_form);
    }
}

// Parse the body of an AssertedRelationship (sources, targets, attributes).
static void parse_relationship_fields(pugi::xml_node node, AssertedRelationship& rel) {
    for (auto child : node.children()) {
        std::string ln = local_name(child.name());
        if (ln == "source") {
            std::string ref = read_id_ref(child);
            if (!ref.empty()) rel.sources.push_back(ref);
        } else if (ln == "target") {
            std::string ref = read_id_ref(child);
            if (!ref.empty()) rel.targets.push_back(ref);
        }
    }
    rel.assertionDeclaration = node.attribute("assertionDeclaration").as_string();
    rel.isCounter = read_bool_attr(node, "isCounter", false);
}

// Parse content for elements (Claim, ArgumentReasoning) where the spec puts
// human-readable content in either a "content" attribute or in
// <content lang="..">..</content> children.
static void parse_content_field(pugi::xml_node node,
                                std::string& out_primary,
                                MultiLangText& out_ml) {
    out_primary = node.attribute("content").as_string();
    if (!out_primary.empty()) out_ml.set("en", out_primary);

    MultiLangText inline_content = extract_inline_content(node);
    if (!inline_content.texts.empty()) {
        out_ml = inline_content;
        out_primary = out_ml.get_primary();
    }
}

// ---------- Per-element parsers --------------------------------------------

static Expression parse_expression(pugi::xml_node node) {
    Expression expr;
    parse_element_base(node, expr);
    expr.value = node.attribute("value").as_string();
    return expr;
}

static TerminologyPackage parse_terminology_package(pugi::xml_node node) {
    TerminologyPackage pkg;
    parse_element_base(node, pkg);
    for (auto child : node.children()) {
        if (local_name(child.name()) == "expression") {
            pkg.expressions.push_back(parse_expression(child));
        }
    }
    return pkg;
}

static Artifact parse_artifact(pugi::xml_node node) {
    Artifact artifact;
    parse_element_base(node, artifact);
    artifact.version = node.attribute("version").as_string();
    artifact.date = node.attribute("date").as_string();
    return artifact;
}

static ArtifactPackage parse_artifact_package(pugi::xml_node node) {
    ArtifactPackage pkg;
    parse_element_base(node, pkg);
    for (auto child : node.children()) {
        if (local_name(child.name()) == "artifact") {
            pkg.artifacts.push_back(parse_artifact(child));
        }
    }
    return pkg;
}

static Claim parse_claim(pugi::xml_node node) {
    Claim claim;
    parse_element_base(node, claim);
    parse_content_field(node, claim.content, claim.content_ml);
    // Tooling-specific fallback: some authoring tools write claim text inside a
    // <statement> child element instead of the "content" attribute.
    if (claim.content.empty()) {
        if (auto statement = find_child(node, "statement")) {
            claim.content = statement.text().as_string();
            if (!claim.content.empty()) claim.content_ml.set("en", claim.content);
        }
    }
    claim.assertionDeclaration = node.attribute("assertionDeclaration").as_string();
    claim.undeveloped = read_bool_attr(node, "undeveloped", false);
    return claim;
}

static ArgumentReasoning parse_argument_reasoning(pugi::xml_node node) {
    ArgumentReasoning reasoning;
    parse_element_base(node, reasoning);
    parse_content_field(node, reasoning.content, reasoning.content_ml);
    reasoning.undeveloped = read_bool_attr(node, "undeveloped", false);
    return reasoning;
}

static ArtifactReference parse_artifact_reference(pugi::xml_node node) {
    ArtifactReference ref;
    parse_element_base(node, ref);
    ref.referencedArtifact = node.attribute("referencedArtifact").as_string();
    return ref;
}

static AssertedInference parse_asserted_inference(pugi::xml_node node) {
    AssertedInference inference;
    parse_element_base(node, inference);
    parse_relationship_fields(node, inference);

    // The reasoning can be either a child element or an attribute; prefer the
    // child form (XMI convention) when both are present.
    if (auto reasoning_child = find_child(node, "reasoning")) {
        inference.reasoning = read_id_ref(reasoning_child);
    }
    if (inference.reasoning.empty()) {
        std::string attr = node.attribute("reasoning").as_string();
        if (!attr.empty() && attr[0] == '#') attr = attr.substr(1);
        inference.reasoning = attr;
    }
    return inference;
}

static AssertedContext parse_asserted_context(pugi::xml_node node) {
    AssertedContext ctx;
    parse_element_base(node, ctx);
    parse_relationship_fields(node, ctx);
    return ctx;
}

static AssertedEvidence parse_asserted_evidence(pugi::xml_node node) {
    AssertedEvidence evidence;
    parse_element_base(node, evidence);
    parse_relationship_fields(node, evidence);
    return evidence;
}

static ArgumentPackage parse_argument_package(pugi::xml_node node) {
    ArgumentPackage pkg;
    parse_element_base(node, pkg);

    // Dispatch each known child element to its parser.
    for (auto child : node.children()) {
        const std::string ln = local_name(child.name());
        if (ln == "claim") {
            pkg.claims.push_back(parse_claim(child));
        } else if (ln == "argumentreasoning") {
            pkg.argumentReasonings.push_back(parse_argument_reasoning(child));
        } else if (ln == "artifactreference") {
            pkg.artifactReferences.push_back(parse_artifact_reference(child));
        } else if (ln == "assertedinference") {
            pkg.assertedInferences.push_back(parse_asserted_inference(child));
        } else if (ln == "assertedcontext") {
            pkg.assertedContexts.push_back(parse_asserted_context(child));
        } else if (ln == "assertedevidence") {
            pkg.assertedEvidences.push_back(parse_asserted_evidence(child));
        }
        // Unknown children are ignored, but their XML survives if the model
        // elements that reference them are still valid.
    }
    return pkg;
}

// ---------- Top-level parsing ----------------------------------------------

// Discover the SACM XML namespace used by the document so the serializer can
// produce identical output. Falls back to the SACM 2.2 URI if none is found
// (this matches existing test fixtures; no external behaviour change).
static void detect_sacm_namespace(pugi::xml_node root,
                                  std::string& out_prefix,
                                  std::string& out_uri) {
    out_prefix = "sacm";
    out_uri = "http://www.omg.org/spec/SACM/2.2/Argumentation";

    for (auto attr : root.attributes()) {
        std::string attr_name = attr.name();
        if (attr_name.rfind("xmlns:", 0) != 0) continue;

        std::string candidate_prefix = attr_name.substr(6);
        std::string candidate_uri = attr.as_string();

        std::string lower_uri = candidate_uri;
        std::transform(lower_uri.begin(), lower_uri.end(), lower_uri.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (lower_uri.find("sacm") != std::string::npos) {
            out_prefix = candidate_prefix;
            out_uri = candidate_uri;
            return;
        }
    }
}

static SacmParseResult parse_document(pugi::xml_document& doc) {
    SacmParseResult result;

    // Find the root AssuranceCasePackage element; namespace prefix may vary.
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

    AssuranceCasePackage& pkg = result.package;
    parse_element_base(root, pkg);
    detect_sacm_namespace(root, pkg.namespace_prefix, pkg.namespace_uri);

    for (auto child : root.children()) {
        const std::string ln = local_name(child.name());
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
