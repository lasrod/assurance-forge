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

// Add description as child element with multi-language support.
// Writes all languages from description_ml. Falls back to single desc string if ml is empty.
static void add_description(pugi::xml_node parent, const std::string& desc, const MultiLangText& desc_ml) {
    if (desc_ml.texts.empty() && desc.empty()) return;
    auto desc_node = parent.append_child("description");
    if (!desc_ml.texts.empty()) {
        for (const auto& [lang, text] : desc_ml.texts) {
            if (text.empty()) continue;
            auto content_node = desc_node.append_child("content");
            content_node.append_attribute("lang") = lang.c_str();
            content_node.text().set(text.c_str());
        }
    } else if (!desc.empty()) {
        auto content_node = desc_node.append_child("content");
        content_node.append_attribute("lang") = "en";
        content_node.text().set(desc.c_str());
    }
}

// Add name as child element when translations exist.
// Only emits a <name> child when name_ml has non-"en" entries (translations).
// The primary "en" name is already written as an attribute by set_base().
static void add_name_ml(pugi::xml_node parent, const MultiLangText& name_ml) {
    // Only add <name> element if there are translations beyond "en"
    bool has_translations = false;
    for (const auto& [lang, text] : name_ml.texts) {
        if (lang != "en" && !text.empty()) { has_translations = true; break; }
    }
    if (!has_translations) return;

    auto name_node = parent.append_child("name");
    for (const auto& [lang, text] : name_ml.texts) {
        if (text.empty()) continue;
        auto content_node = name_node.append_child("content");
        content_node.append_attribute("lang") = lang.c_str();
        content_node.text().set(text.c_str());
    }
}

// Add content as child elements when translations exist, otherwise as attribute.
static void add_content_ml(pugi::xml_node parent, const std::string& content, const MultiLangText& content_ml) {
    bool has_translations = false;
    for (const auto& [lang, text] : content_ml.texts) {
        if (lang != "en" && !text.empty()) { has_translations = true; break; }
    }
    if (!has_translations) {
        if (!content.empty())
            parent.append_attribute("content") = content.c_str();
        return;
    }
    // Has translations: write as child elements instead of attribute
    for (const auto& [lang, text] : content_ml.texts) {
        if (text.empty()) continue;
        auto content_node = parent.append_child("content");
        content_node.append_attribute("lang") = lang.c_str();
        content_node.text().set(text.c_str());
    }
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
// Serialize common SacmElement attributes and name translations.
static void set_base(pugi::xml_node node, const SacmElement& elem) {
    if (!elem.id.empty())   node.append_attribute("id") = elem.id.c_str();
    if (!elem.name.empty()) node.append_attribute("name") = elem.name.c_str();
    add_name_ml(node, elem.name_ml);
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
    add_description(node, tp.description, tp.description_ml);
    for (const auto& e : tp.expressions) {
        serialize_expression(node, e);
    }
}

static void serialize_artifact(pugi::xml_node parent, const Artifact& a) {
    auto node = parent.append_child("artifact");
    set_base(node, a);
    add_description(node, a.description, a.description_ml);
}

static void serialize_artifact_package(pugi::xml_node parent, const ArtifactPackage& ap) {
    auto node = parent.append_child("artifactPackage");
    set_base(node, ap);
    add_description(node, ap.description, ap.description_ml);
    for (const auto& a : ap.artifacts) {
        serialize_artifact(node, a);
    }
}

static void serialize_claim(pugi::xml_node parent, const Claim& c) {
    auto node = parent.append_child("claim");
    set_base(node, c);
    if (!c.assertionDeclaration.empty())
        node.append_attribute("assertionDeclaration") = c.assertionDeclaration.c_str();
    add_content_ml(node, c.content, c.content_ml);
    add_description(node, c.description, c.description_ml);
}

static void serialize_argument_reasoning(pugi::xml_node parent, const ArgumentReasoning& ar) {
    auto node = parent.append_child("argumentReasoning");
    set_base(node, ar);
    add_content_ml(node, ar.content, ar.content_ml);
    add_description(node, ar.description, ar.description_ml);
}

static void serialize_artifact_reference(pugi::xml_node parent, const ArtifactReference& ar) {
    auto node = parent.append_child("artifactReference");
    set_base(node, ar);
    if (!ar.referencedArtifact.empty())
        node.append_attribute("referencedArtifact") = ar.referencedArtifact.c_str();
    add_description(node, ar.description, ar.description_ml);
}

static void serialize_relationship_common(pugi::xml_node node, const AssertedRelationship& rel) {
    if (!rel.assertionDeclaration.empty())
        node.append_attribute("assertionDeclaration") = rel.assertionDeclaration.c_str();
    add_description(node, rel.description, rel.description_ml);
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
    add_description(node, pkg.description, pkg.description_ml);

    for (const auto& c : pkg.claims)              serialize_claim(node, c);
    for (const auto& ar : pkg.argumentReasonings)  serialize_argument_reasoning(node, ar);
    for (const auto& ar : pkg.artifactReferences)  serialize_artifact_reference(node, ar);
    for (const auto& ai : pkg.assertedInferences)  serialize_asserted_inference(node, ai);
    for (const auto& ac : pkg.assertedContexts)    serialize_asserted_context(node, ac);
    for (const auto& ae : pkg.assertedEvidences)    serialize_asserted_evidence(node, ae);
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
    add_description(root, package.description, package.description_ml);

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
