#include "sacm/sacm_serializer.h"
#include <pugixml.hpp>
#include <fstream>
#include <sstream>

namespace sacm {

namespace {

// Writer that captures output into a std::string.
struct StringWriter : pugi::xml_writer {
    std::string result;
    void write(const void* data, size_t size) override {
        result.append(static_cast<const char*>(data), size);
    }
};

static std::string prefixed(const std::string& ns, const char* local) {
    if (ns.empty()) return local;
    return ns + ":" + local;
}

// Add description as child element: <description><content lang="en">text</content></description>
static void add_description(pugi::xml_node parent, const std::string& desc) {
    if (desc.empty()) return;
    auto desc_node = parent.append_child("description");
    auto content_node = desc_node.append_child("content");
    content_node.append_attribute("lang") = "en";
    content_node.text().set(desc.c_str());
}

// Add source/target refs as child elements with href="#id"
static void add_refs(pugi::xml_node parent, const char* tag,
                     const std::vector<std::string>& refs) {
    for (const auto& r : refs) {
        auto node = parent.append_child(tag);
        node.append_attribute("href") = ("#" + r).c_str();
    }
}

// Serialize common SacmElement attributes.
static void set_base(pugi::xml_node node, const SacmElement& elem) {
    if (!elem.id.empty())   node.append_attribute("id") = elem.id.c_str();
    if (!elem.name.empty()) node.append_attribute("name") = elem.name.c_str();
}

// ===== Element serializers =====

static void serialize_expression(pugi::xml_node parent, const Expression& e) {
    auto node = parent.append_child("expression");
    set_base(node, e);
    if (!e.value.empty()) node.append_attribute("value") = e.value.c_str();
}

static void serialize_terminology_package(pugi::xml_node parent, const TerminologyPackage& tp) {
    auto node = parent.append_child("terminologyPackage");
    set_base(node, tp);
    add_description(node, tp.description);
    for (const auto& e : tp.expressions) {
        serialize_expression(node, e);
    }
}

static void serialize_artifact(pugi::xml_node parent, const Artifact& a) {
    auto node = parent.append_child("artifact");
    set_base(node, a);
    add_description(node, a.description);
}

static void serialize_artifact_package(pugi::xml_node parent, const ArtifactPackage& ap) {
    auto node = parent.append_child("artifactPackage");
    set_base(node, ap);
    add_description(node, ap.description);
    for (const auto& a : ap.artifacts) {
        serialize_artifact(node, a);
    }
}

static void serialize_claim(pugi::xml_node parent, const Claim& c) {
    auto node = parent.append_child("claim");
    set_base(node, c);
    if (!c.assertionDeclaration.empty())
        node.append_attribute("assertionDeclaration") = c.assertionDeclaration.c_str();
    if (!c.content.empty())
        node.append_attribute("content") = c.content.c_str();
    add_description(node, c.description);
}

static void serialize_argument_reasoning(pugi::xml_node parent, const ArgumentReasoning& ar) {
    auto node = parent.append_child("argumentReasoning");
    set_base(node, ar);
    if (!ar.content.empty())
        node.append_attribute("content") = ar.content.c_str();
    add_description(node, ar.description);
}

static void serialize_artifact_reference(pugi::xml_node parent, const ArtifactReference& ar) {
    auto node = parent.append_child("artifactReference");
    set_base(node, ar);
    if (!ar.referencedArtifact.empty())
        node.append_attribute("referencedArtifact") = ar.referencedArtifact.c_str();
    add_description(node, ar.description);
}

static void serialize_relationship_common(pugi::xml_node node, const AssertedRelationship& rel) {
    if (!rel.assertionDeclaration.empty())
        node.append_attribute("assertionDeclaration") = rel.assertionDeclaration.c_str();
    add_description(node, rel.description);
    add_refs(node, "source", rel.sources);
    add_refs(node, "target", rel.targets);
}

static void serialize_asserted_inference(pugi::xml_node parent, const AssertedInference& ai) {
    auto node = parent.append_child("assertedInference");
    set_base(node, ai);
    serialize_relationship_common(node, ai);
    if (!ai.reasoning.empty()) {
        auto rnode = node.append_child("reasoning");
        rnode.append_attribute("href") = ("#" + ai.reasoning).c_str();
    }
}

static void serialize_asserted_context(pugi::xml_node parent, const AssertedContext& ac) {
    auto node = parent.append_child("assertedContext");
    set_base(node, ac);
    serialize_relationship_common(node, ac);
}

static void serialize_asserted_evidence(pugi::xml_node parent, const AssertedEvidence& ae) {
    auto node = parent.append_child("assertedEvidence");
    set_base(node, ae);
    serialize_relationship_common(node, ae);
}

static void serialize_argument_package(pugi::xml_node parent, const ArgumentPackage& pkg) {
    auto node = parent.append_child("argumentPackage");
    set_base(node, pkg);
    add_description(node, pkg.description);

    for (const auto& c : pkg.claims)              serialize_claim(node, c);
    for (const auto& ar : pkg.argumentReasonings)  serialize_argument_reasoning(node, ar);
    for (const auto& ar : pkg.artifactReferences)  serialize_artifact_reference(node, ar);
    for (const auto& ai : pkg.assertedInferences)  serialize_asserted_inference(node, ai);
    for (const auto& ac : pkg.assertedContexts)    serialize_asserted_context(node, ac);
    for (const auto& ae : pkg.assertedEvidences)   serialize_asserted_evidence(node, ae);
}

}  // namespace

std::string serialize_sacm(const AssuranceCasePackage& package) {
    pugi::xml_document doc;

    auto decl = doc.prepend_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";

    auto root = doc.append_child(prefixed(package.namespace_prefix, "AssuranceCasePackage").c_str());

    // xmlns:prefix="uri"
    std::string xmlns_attr = "xmlns:" + package.namespace_prefix;
    root.append_attribute(xmlns_attr.c_str()) = package.namespace_uri.c_str();

    set_base(root, package);
    add_description(root, package.description);

    for (const auto& tp : package.terminologyPackages)
        serialize_terminology_package(root, tp);
    for (const auto& ap : package.artifactPackages)
        serialize_artifact_package(root, ap);
    for (const auto& argp : package.argumentPackages)
        serialize_argument_package(root, argp);

    StringWriter writer;
    doc.save(writer, "  ");
    return writer.result;
}

bool serialize_sacm_to_file(const AssuranceCasePackage& package, const std::string& file_path) {
    std::string xml = serialize_sacm(package);
    std::ofstream ofs(file_path, std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs << xml;
    return ofs.good();
}

}  // namespace sacm
