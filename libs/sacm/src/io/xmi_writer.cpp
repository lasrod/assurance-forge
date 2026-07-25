#include "sacm/io/xmi.h"

#include "io/name_tables.h"

#include "sacm/metadata/namespaces.h"
#include "sacm/model/assurance_case.h"
#include "sacm/validation/codes.h"

#include <pugixml.hpp>

#include <format>
#include <fstream>
#include <map>
#include <string>

namespace sacm::io {

namespace {

using model::ElementId;
using model::ElementKind;
using model::SACMElement;
namespace ns = metadata::namespaces;

std::string qualified_class_name(ElementKind kind) {
    return std::format("{}:{}", ns::kSacmPrefix, metadata::kind_name(kind));
}

std::string idrefs(const std::vector<ElementId>& ids) {
    std::string joined;
    for (const ElementId& id : ids) {
        if (!joined.empty()) {
            joined.push_back(' ');
        }
        joined += id.value();
    }
    return joined;
}

void write_element(pugi::xml_node parent, const SACMElement& element, std::string_view node_name,
                   std::optional<ElementKind> declared_kind);
std::string qualified_class_name_for_expression_lang_string();

const std::map<std::string, std::string> kNoForeignNamespaces;

// <value lang=".." content=".."/> entries of a MultiLangString.
void write_multi_lang(pugi::xml_node owner, std::string_view wrapper_name,
                      const model::MultiLangString& value) {
    if (value.values.empty()) {
        return;
    }
    pugi::xml_node wrapper = owner.append_child(std::string(wrapper_name).c_str());
    for (const model::LangString& entry : value.values) {
        pugi::xml_node node = wrapper.append_child("value");
        if (entry.expression_ref.has_value()) {
            node.append_attribute("xsi:type") =
                qualified_class_name_for_expression_lang_string().c_str();
        }
        if (!entry.lang.empty()) {
            node.append_attribute("lang") = entry.lang.c_str();
        }
        if (!entry.content.empty()) {
            node.append_attribute("content") = entry.content.c_str();
        }
        if (entry.expression_ref.has_value()) {
            node.append_attribute("expression") = entry.expression_ref->value().c_str();
        }
    }
}

std::string qualified_class_name_for_expression_lang_string() {
    return std::format("{}:ExpressionLangString", ns::kSacmPrefix);
}

void write_common_attributes(pugi::xml_node node, const SACMElement& element) {
    node.append_attribute("xmi:id") = element.id().value().c_str();
    if (element.gid().has_value()) {
        node.append_attribute("gid") = element.gid()->c_str();
    }
    if (element.is_citation()) {
        node.append_attribute("isCitation") = "true";
    }
    if (element.is_abstract()) {
        node.append_attribute("isAbstract") = "true";
    }
    if (element.cited_element().has_value()) {
        node.append_attribute("citedElement") = element.cited_element()->value().c_str();
    }
    if (element.abstract_form().has_value()) {
        node.append_attribute("abstractForm") = element.abstract_form()->value().c_str();
    }
}

void write_kind_specific_attributes(pugi::xml_node node, const SACMElement& element) {
    if (const auto* assertion = dynamic_cast<const model::Assertion*>(&element)) {
        if (assertion->assertion_declaration() != model::AssertionDeclaration::Asserted) {
            node.append_attribute("assertionDeclaration") =
                std::string(model::assertion_declaration_name(assertion->assertion_declaration()))
                    .c_str();
        }
        if (!assertion->meta_claims().empty()) {
            node.append_attribute("metaClaim") = idrefs(assertion->meta_claims()).c_str();
        }
    }
    if (const auto* rel = dynamic_cast<const model::AssertedRelationship*>(&element)) {
        if (rel->is_counter()) {
            node.append_attribute("isCounter") = "true";
        }
        if (rel->reasoning().has_value()) {
            node.append_attribute("reasoning") = rel->reasoning()->value().c_str();
        }
        if (!rel->sources().empty()) {
            node.append_attribute("source") = idrefs(rel->sources()).c_str();
        }
        if (!rel->targets().empty()) {
            node.append_attribute("target") = idrefs(rel->targets()).c_str();
        }
    }
    if (const auto* rel = dynamic_cast<const model::ArtifactAssetRelationship*>(&element)) {
        if (!rel->sources().empty()) {
            node.append_attribute("source") = idrefs(rel->sources()).c_str();
        }
        if (!rel->targets().empty()) {
            node.append_attribute("target") = idrefs(rel->targets()).c_str();
        }
    }
    if (const auto* reference = dynamic_cast<const model::ArtifactReference*>(&element)) {
        if (!reference->referenced_artifact_elements().empty()) {
            node.append_attribute("referencedArtifactElement") =
                idrefs(reference->referenced_artifact_elements()).c_str();
        }
    }
    if (const auto* reasoning = dynamic_cast<const model::ArgumentReasoning*>(&element)) {
        if (reasoning->structure().has_value()) {
            node.append_attribute("structure") = reasoning->structure()->value().c_str();
        }
    }
    if (const auto* group = dynamic_cast<const model::ArgumentGroup*>(&element)) {
        if (!group->argument_elements().empty()) {
            node.append_attribute("argumentElement") = idrefs(group->argument_elements()).c_str();
        }
    }
    if (const auto* group = dynamic_cast<const model::ArtifactGroup*>(&element)) {
        if (!group->artifact_elements().empty()) {
            node.append_attribute("artifactElement") = idrefs(group->artifact_elements()).c_str();
        }
    }
    if (const auto* group = dynamic_cast<const model::TerminologyGroup*>(&element)) {
        if (!group->terminology_elements().empty()) {
            node.append_attribute("terminologyElement") =
                idrefs(group->terminology_elements()).c_str();
        }
    }
    if (const auto* category = dynamic_cast<const model::Category*>(&element)) {
        if (!category->categories().empty()) {
            node.append_attribute("category") = idrefs(category->categories()).c_str();
        }
    }
    if (const auto* expr_element = dynamic_cast<const model::ExpressionElement*>(&element)) {
        if (!expr_element->value().empty()) {
            node.append_attribute("value") = expr_element->value().c_str();
        }
        if (!expr_element->categories().empty()) {
            node.append_attribute("category") = idrefs(expr_element->categories()).c_str();
        }
    }
    if (const auto* expression = dynamic_cast<const model::Expression*>(&element)) {
        if (!expression->elements().empty()) {
            node.append_attribute("element") = idrefs(expression->elements()).c_str();
        }
    }
    if (const auto* term = dynamic_cast<const model::Term*>(&element)) {
        if (!term->external_reference().empty()) {
            node.append_attribute("externalReference") = term->external_reference().c_str();
        }
        if (term->origin().has_value()) {
            node.append_attribute("origin") = term->origin()->value().c_str();
        }
    }
    if (const auto* artifact = dynamic_cast<const model::Artifact*>(&element)) {
        if (!artifact->version().empty()) {
            node.append_attribute("version") = artifact->version().c_str();
        }
        if (!artifact->date().empty()) {
            node.append_attribute("date") = artifact->date().c_str();
        }
    }
    if (const auto* activity = dynamic_cast<const model::Activity*>(&element)) {
        if (!activity->start_time().empty()) {
            node.append_attribute("startTime") = activity->start_time().c_str();
        }
        if (!activity->end_time().empty()) {
            node.append_attribute("endTime") = activity->end_time().c_str();
        }
    }
    if (const auto* event = dynamic_cast<const model::Event*>(&element)) {
        if (!event->date().empty()) {
            node.append_attribute("date") = event->date().c_str();
        }
    }

    // Package reference ends.
    const auto write_interface_refs = [&node](const std::vector<ElementId>& interfaces) {
        if (!interfaces.empty()) {
            node.append_attribute("interface") = idrefs(interfaces).c_str();
        }
    };
    const auto write_implements = [&node](const std::optional<ElementId>& implements) {
        if (implements.has_value()) {
            node.append_attribute("implements") = implements->value().c_str();
        }
    };
    const auto write_participants = [&node](const std::vector<ElementId>& participants) {
        if (!participants.empty()) {
            node.append_attribute("participantPackage") = idrefs(participants).c_str();
        }
    };
    if (const auto* acp = dynamic_cast<const model::AssuranceCasePackage*>(&element)) {
        write_interface_refs(acp->interfaces());
    } else if (const auto* pkg = dynamic_cast<const model::ArgumentPackage*>(&element)) {
        write_interface_refs(pkg->interfaces());
    } else if (const auto* pkg = dynamic_cast<const model::ArtifactPackage*>(&element)) {
        write_interface_refs(pkg->interfaces());
    } else if (const auto* pkg = dynamic_cast<const model::TerminologyPackage*>(&element)) {
        write_interface_refs(pkg->interfaces());
    }
    if (const auto* iface = dynamic_cast<const model::AssuranceCasePackageInterface*>(&element)) {
        write_implements(iface->implements());
    } else if (const auto* iface = dynamic_cast<const model::ArgumentPackageInterface*>(&element)) {
        write_implements(iface->implements());
    } else if (const auto* iface = dynamic_cast<const model::ArtifactPackageInterface*>(&element)) {
        write_implements(iface->implements());
    } else if (const auto* iface =
                   dynamic_cast<const model::TerminologyPackageInterface*>(&element)) {
        write_implements(iface->implements());
    }
    if (const auto* binding = dynamic_cast<const model::AssuranceCasePackageBinding*>(&element)) {
        write_participants(binding->participant_packages());
    } else if (const auto* binding = dynamic_cast<const model::ArgumentPackageBinding*>(&element)) {
        write_participants(binding->participant_packages());
    } else if (const auto* binding = dynamic_cast<const model::ArtifactPackageBinding*>(&element)) {
        write_participants(binding->participant_packages());
    } else if (const auto* binding =
                   dynamic_cast<const model::TerminologyPackageBinding*>(&element)) {
        write_participants(binding->participant_packages());
    }
}

void write_utility(pugi::xml_node parent, std::string_view role,
                   const model::UtilityElement& element) {
    pugi::xml_node node = parent.append_child(std::string(role).c_str());
    write_common_attributes(node, element);
    if (const auto* tagged = dynamic_cast<const model::TaggedValue*>(&element)) {
        write_multi_lang(node, "key", tagged->key());
    }
    write_multi_lang(node, "content", element.content());
}

void write_model_element_children(pugi::xml_node node, const model::ModelElement& element) {
    // name: LangString[1] — always emitted for determinism and conformance.
    pugi::xml_node name = node.append_child("name");
    if (element.name().expression_ref.has_value()) {
        name.append_attribute("xsi:type") = qualified_class_name_for_expression_lang_string().c_str();
    }
    if (!element.name().lang.empty()) {
        name.append_attribute("lang") = element.name().lang.c_str();
    }
    if (!element.name().content.empty()) {
        name.append_attribute("content") = element.name().content.c_str();
    }
    if (element.name().expression_ref.has_value()) {
        name.append_attribute("expression") = element.name().expression_ref->value().c_str();
    }

    for (const auto& description : element.descriptions()) {
        write_utility(node, "description", *description);
    }
    for (const auto& constraint : element.implementation_constraints()) {
        write_utility(node, "implementationConstraint", *constraint);
    }
    for (const auto& note : element.notes()) {
        write_utility(node, "note", *note);
    }
    for (const auto& tagged : element.tagged_values()) {
        write_utility(node, "taggedValue", *tagged);
    }
}

// Writes the containment children specific to the element's family.
void write_containment_children(pugi::xml_node node, const SACMElement& element) {
    if (const auto* asset = dynamic_cast<const model::ArtifactAsset*>(&element)) {
        for (const auto& property : asset->properties()) {
            write_element(node, *property, "property", ElementKind::Property);
        }
    }
    if (const auto* acp = dynamic_cast<const model::AssuranceCasePackage*>(&element)) {
        for (const auto& child : acp->assurance_case_packages()) {
            write_element(node, *child, "assuranceCasePackage", ElementKind::AssuranceCasePackage);
        }
        for (const auto& child : acp->argument_packages()) {
            write_element(node, *child, "argumentPackage", ElementKind::ArgumentPackage);
        }
        for (const auto& child : acp->artifact_packages()) {
            write_element(node, *child, "artifactPackage", ElementKind::ArtifactPackage);
        }
        for (const auto& child : acp->terminology_packages()) {
            write_element(node, *child, "terminologyPackage", ElementKind::TerminologyPackage);
        }
        return;
    }
    if (const auto* pkg = dynamic_cast<const model::ArgumentPackage*>(&element)) {
        for (const auto& child : pkg->argument_elements()) {
            write_element(node, *child, "argumentElement", std::nullopt);
        }
        return;
    }
    if (const auto* pkg = dynamic_cast<const model::ArtifactPackage*>(&element)) {
        for (const auto& child : pkg->artifact_elements()) {
            write_element(node, *child, "artifactElement", std::nullopt);
        }
        return;
    }
    if (const auto* pkg = dynamic_cast<const model::TerminologyPackage*>(&element)) {
        for (const auto& child : pkg->terminology_elements()) {
            write_element(node, *child, "terminologyElement", std::nullopt);
        }
        return;
    }
}

// Compat-mode re-emission of preserved unknown content (set only while
// saving in tolerant/compatibility mode).
thread_local bool g_emit_preserved_content = false;

// Re-emits vendor attributes as `name="value"` on the element. pugixml has no
// raw-attribute API, so the fragment is split back into name and value.
void write_preserved_attributes(pugi::xml_node node, const SACMElement& element) {
    if (!g_emit_preserved_content) {
        return;
    }
    for (const std::string& fragment : element.preserved_attributes()) {
        const std::size_t equals = fragment.find('=');
        if (equals == std::string::npos || fragment.size() < equals + 3) {
            continue;
        }
        const std::string name = fragment.substr(0, equals);
        // Strip the surrounding quotes captured with the value.
        const std::string value = fragment.substr(equals + 2, fragment.size() - equals - 3);
        node.append_attribute(name.c_str()) = value.c_str();
    }
}

void write_preserved_content(pugi::xml_node node, const SACMElement& element) {
    if (!g_emit_preserved_content) {
        return;
    }
    for (const std::string& fragment : element.preserved_content()) {
        node.append_buffer(fragment.data(), fragment.size());
    }
}

// Re-declares the prefixes the preserved fragments use. Without this the output
// is not namespace-well-formed, and worse, a re-load cannot classify a
// preserved *attribute*: an undeclared prefix resolves to no namespace, so the
// attribute stops looking foreign and is skipped -- the vendor data survives one
// save and is lost on the next (SACM23-COMPAT-001). Prefixes the writer already
// declared win, so a foreign document cannot rebind `sacm`, `xmi` or `xsi`.
void write_namespace_declarations(pugi::xml_node node,
                                  const std::map<std::string, std::string>& foreign_namespaces) {
    for (const auto& [prefix, uri] : foreign_namespaces) {
        if (prefix.empty()) {
            continue;
        }
        const std::string declaration = std::format("xmlns:{}", prefix);
        if (node.attribute(declaration.c_str())) {
            continue;
        }
        node.append_attribute(declaration.c_str()) = uri.c_str();
    }
}

void write_element(pugi::xml_node parent, const SACMElement& element, std::string_view node_name,
                   std::optional<ElementKind> declared_kind) {
    pugi::xml_node node = parent.append_child(std::string(node_name).c_str());
    // xsi:type whenever the concrete class differs from the declared role
    // type (XMI rule); abstract roles always need it.
    if (!declared_kind.has_value() || *declared_kind != element.kind()) {
        node.append_attribute("xsi:type") = qualified_class_name(element.kind()).c_str();
    }
    write_common_attributes(node, element);
    write_kind_specific_attributes(node, element);
    if (const auto* model_element = dynamic_cast<const model::ModelElement*>(&element)) {
        write_model_element_children(node, *model_element);
    }
    write_containment_children(node, element);
    write_preserved_attributes(node, element);
    write_preserved_content(node, element);
}

void write_root(pugi::xml_node parent, const SACMElement& element, bool declare_namespaces,
                std::string_view sacm_namespace,
                const std::map<std::string, std::string>& foreign_namespaces) {
    pugi::xml_node node = parent.append_child(qualified_class_name(element.kind()).c_str());
    if (declare_namespaces) {
        node.append_attribute("xmlns:sacm") = std::string(sacm_namespace).c_str();
        node.append_attribute("xmlns:xmi") = std::string(ns::kXmi).c_str();
        node.append_attribute("xmlns:xsi") = std::string(ns::kXsi).c_str();
        write_namespace_declarations(node, foreign_namespaces);
        node.append_attribute("xmi:version") = std::string(ns::kXmiVersion).c_str();
    }
    write_common_attributes(node, element);
    write_kind_specific_attributes(node, element);
    if (const auto* model_element = dynamic_cast<const model::ModelElement*>(&element)) {
        write_model_element_children(node, *model_element);
    }
    write_containment_children(node, element);
    write_preserved_attributes(node, element);
    write_preserved_content(node, element);
}

struct StringWriter final : pugi::xml_writer {
    std::string output;
    void write(const void* data, size_t size) override {
        output.append(static_cast<const char*>(data), size);
    }
};

}  // namespace

SaveResult save_xmi_string(const model::Document& document, const SaveOptions& options) {
    SaveResult result;

    // Strict save must not emit compatibility-only content — and must not
    // silently drop it either: refuse with SACM-XMI-006 (settled decision
    // #9/#10). Compatibility save re-emits preserved fragments verbatim.
    std::vector<model::ElementId> carriers;
    document.for_each_element([&carriers](const SACMElement& element) {
        if (!element.preserved_content().empty() || !element.preserved_attributes().empty()) {
            carriers.push_back(element.id());
        }
    });
    if (options.mode == Mode::Strict) {
        if (!carriers.empty()) {
            result.diagnostics.push_back(validation::Diagnostic{
                .code = std::string(validation::codes::kXmiStrictSaveRefused),
                .severity = validation::Severity::Error,
                .requirement_id = "SACM23-XMI-004",
                .operation = "",
                .affected = std::move(carriers),
                .location = std::nullopt,
                .message = "document carries preserved compatibility content; save in "
                           "compatibility mode or remove the preserved content",
            });
            return result;
        }
    }
    g_emit_preserved_content = options.mode != Mode::Strict;

    // Foreign namespace declarations exist to make the preserved fragments
    // readable, so they follow the fragments: emitted only in compatibility mode
    // and only when something is actually preserved. Strict output therefore
    // stays free of them, and a document carrying nothing unknown serializes
    // exactly as before.
    const std::map<std::string, std::string>& foreign_namespaces =
        g_emit_preserved_content && !carriers.empty() ? document.foreign_namespaces()
                                                      : kNoForeignNamespaces;

    // An empty override means the pinned default. SACM 2.3 determines no
    // instance namespace, so this is a project choice a caller may override.
    const std::string_view sacm_namespace =
        options.namespace_uri.empty() ? ns::kSacm : std::string_view(options.namespace_uri);

    pugi::xml_document xml;
    pugi::xml_node declaration = xml.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";

    const std::size_t root_count = document.roots().size() + document.other_roots().size();
    if (root_count == 1) {
        if (!document.roots().empty()) {
            write_root(xml, *document.roots().front(), true, sacm_namespace, foreign_namespaces);
        } else {
            write_root(xml, *document.other_roots().front(), true, sacm_namespace,
                       foreign_namespaces);
        }
    } else {
        pugi::xml_node wrapper = xml.append_child("xmi:XMI");
        wrapper.append_attribute("xmlns:sacm") = std::string(sacm_namespace).c_str();
        wrapper.append_attribute("xmlns:xmi") = std::string(ns::kXmi).c_str();
        wrapper.append_attribute("xmlns:xsi") = std::string(ns::kXsi).c_str();
        write_namespace_declarations(wrapper, foreign_namespaces);
        wrapper.append_attribute("xmi:version") = std::string(ns::kXmiVersion).c_str();
        for (const auto& root : document.roots()) {
            write_root(wrapper, *root, false, sacm_namespace, kNoForeignNamespaces);
        }
        for (const auto& root : document.other_roots()) {
            write_root(wrapper, *root, false, sacm_namespace, kNoForeignNamespaces);
        }
    }

    StringWriter writer;
    xml.save(writer, "  ", pugi::format_default, pugi::encoding_utf8);
    result.xml = std::move(writer.output);
    result.ok = true;
    return result;
}

SaveResult save_xmi_file(const std::filesystem::path& path, const model::Document& document,
                         const SaveOptions& options) {
    SaveResult result = save_xmi_string(document, options);
    if (!result.ok) {
        return result;
    }
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        result.ok = false;
        result.diagnostics.push_back(validation::Diagnostic{
            .code = std::string(validation::codes::kXmlMalformed),
            .severity = validation::Severity::Error,
            .requirement_id = "SACM23-XMI-004",
            .operation = "",
            .affected = {},
            .location = std::nullopt,
            .message = std::format("cannot write file: {}", path.string()),
        });
        return result;
    }
    stream << result.xml;
    return result;
}

}  // namespace sacm::io
