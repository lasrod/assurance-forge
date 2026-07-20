#include "sacm/io/xmi.h"

#include "io/name_tables.h"
#include "model/access.h"
#include "model/traverse.h"

#include "sacm/metadata/namespaces.h"
#include "sacm/validation/codes.h"

#include <pugixml.hpp>

#include <format>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace sacm::io {

namespace {

using model::ElementId;
using model::ElementKind;
using model::SACMElement;
using sacm::detail::Access;
using validation::Diagnostic;
using validation::Severity;

std::string_view local_name(std::string_view qualified) {
    const std::size_t colon = qualified.find(':');
    return colon == std::string_view::npos ? qualified : qualified.substr(colon + 1);
}

// Containment role names as emitted by the EMF-based SACM tooling
// (github.com/wrwei/SACM and everything generated from it), mapped to the
// normative spelling from ptc/22-03-13. The ecosystem consistently uses
// `<owner>Element` where the normative model uses `<owner-stem>Element`; a file
// using these spellings is otherwise well-formed SACM, so treating them as
// unknown would silently move the entire argument into preserved content while
// still reporting the document valid.
std::string_view normalize_role(std::string_view role) {
    if (role == "argumentationElement") {
        return "argumentElement";
    }
    return role;
}

std::string node_to_string(const pugi::xml_node& node) {
    struct Writer final : pugi::xml_writer {
        std::string output;
        void write(const void* data, size_t size) override {
            output.append(static_cast<const char*>(data), size);
        }
    } writer;
    node.print(writer, "  ", pugi::format_raw);
    return writer.output;
}

std::string_view prefix_of(std::string_view qualified) {
    const std::size_t colon = qualified.find(':');
    return colon == std::string_view::npos ? std::string_view{} : qualified.substr(0, colon);
}

struct Reader {
    Mode mode = Mode::Tolerant;
    std::string source_file;
    const char* buffer = nullptr;
    std::vector<Diagnostic> diagnostics;
    // Innermost-first stack of xmlns scopes (prefix -> URI; "" = default ns).
    std::vector<std::unordered_map<std::string, std::string>> ns_scopes;
    std::string source_namespace;
    metadata::namespaces::StandardVersion source_version =
        metadata::namespaces::StandardVersion::Unknown;
    std::uint64_t generated_counter = 0;
    std::unordered_set<std::string> used_ids;

    bool strict() const { return mode == Mode::Strict; }

    std::optional<validation::SourceLocation> locate(const pugi::xml_node& node) const {
        if (buffer == nullptr) {
            return std::nullopt;
        }
        const std::ptrdiff_t offset = node.offset_debug();
        if (offset < 0) {
            return std::nullopt;
        }
        int line = 1;
        int column = 1;
        for (std::ptrdiff_t i = 0; i < offset && buffer[i] != '\0'; ++i) {
            if (buffer[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        return validation::SourceLocation{source_file, line, column};
    }

    void report(std::string_view code, Severity severity, std::string_view requirement,
                const pugi::xml_node& node, std::vector<ElementId> affected, std::string message) {
        diagnostics.push_back(Diagnostic{
            .code = std::string(code),
            .severity = severity,
            .requirement_id = std::string(requirement),
            .operation = "",
            .affected = std::move(affected),
            .location = locate(node),
            .message = std::move(message),
        });
    }

    // strict -> Error, tolerant -> Warning.
    Severity mode_severity() const { return strict() ? Severity::Error : Severity::Warning; }

    void push_scope(const pugi::xml_node& node) {
        std::unordered_map<std::string, std::string> scope;
        for (const pugi::xml_attribute& attr : node.attributes()) {
            const std::string_view name = attr.name();
            if (name == "xmlns") {
                scope[""] = attr.value();
            } else if (name.starts_with("xmlns:")) {
                scope[std::string(name.substr(6))] = attr.value();
            }
        }
        ns_scopes.push_back(std::move(scope));
    }

    void pop_scope() { ns_scopes.pop_back(); }

    std::string resolve_prefix(std::string_view prefix) const {
        for (auto it = ns_scopes.rbegin(); it != ns_scopes.rend(); ++it) {
            const auto found = it->find(std::string(prefix));
            if (found != it->end()) {
                return found->second;
            }
        }
        return {};
    }

    ElementId generate_id(ElementKind kind) {
        while (true) {
            ++generated_counter;
            std::string candidate = std::format("generated_{}", generated_counter);
            if (!used_ids.contains(candidate)) {
                used_ids.insert(candidate);
                return ElementId{std::move(candidate)};
            }
        }
    }
};

pugi::xml_attribute find_attr_local(const pugi::xml_node& node, std::string_view local) {
    // Prefer the exact xmi:-qualified attribute, then any prefix, then plain.
    for (const pugi::xml_attribute& attr : node.attributes()) {
        if (std::string_view(attr.name()) == std::format("xmi:{}", local)) {
            return attr;
        }
    }
    for (const pugi::xml_attribute& attr : node.attributes()) {
        const std::string_view name = attr.name();
        if (local_name(name) == local && !name.starts_with("xmlns")) {
            return attr;
        }
    }
    return {};
}

std::string strip_fragment(std::string_view value) {
    if (value.starts_with('#')) {
        value.remove_prefix(1);
    }
    return std::string(value);
}

bool is_external_href(std::string_view value) {
    return !value.empty() && !value.starts_with('#') &&
           value.find('#') != std::string_view::npos;
}

bool parse_bool(std::string_view value) { return value == "true" || value == "1"; }

// --- EMF positional-reference normalization -------------------------------
//
// EMF-produced SACM omits xmi:id entirely and refers between elements by
// containment path instead: source="//@argumentPackage.0/@argumentationElement.18"
// means "the 19th argumentationElement of the 1st argumentPackage". Since such
// files carry no ids at all, these paths are the *only* way their references
// work -- without resolving them every asserted relationship dangles and the
// argument structure is lost even though every element parses.
//
// Rather than thread a second reference form through the reader, we normalize
// the DOM first: give each path-addressed element a deterministic id and
// rewrite the paths to it. The reader then sees an ordinary id-based document.

bool looks_like_emf_path(std::string_view value) { return value.starts_with("//@"); }

// Deterministic, XML-name-safe id for an EMF containment path: each run of
// path punctuation becomes a single underscore, so
// "//@argumentPackage.0/@argumentationElement.18" gives
// "emf_argumentPackage_0_argumentationElement_18".
std::string id_from_emf_path(std::string_view path) {
    std::string id = "emf";
    bool pending_separator = true;
    for (const char c : path) {
        if (c == '/' || c == '@' || c == '.') {
            pending_separator = true;
            continue;
        }
        if (pending_separator) {
            id.push_back('_');
            pending_separator = false;
        }
        id.push_back(c);
    }
    return id;
}

bool document_uses_emf_paths(const pugi::xml_node& node) {
    for (const pugi::xml_attribute& attr : node.attributes()) {
        if (looks_like_emf_path(attr.value())) {
            return true;
        }
    }
    for (const pugi::xml_node& child : node.children()) {
        if (child.type() == pugi::node_element && document_uses_emf_paths(child)) {
            return true;
        }
    }
    return false;
}

// Assigns xmi:id to every element reachable by an EMF path, recording the
// mapping. `path` is the EMF path of `node` itself ("/" for the root object).
void assign_emf_path_ids(pugi::xml_node node, const std::string& path,
                         std::unordered_map<std::string, std::string>& path_to_id,
                         const std::unordered_set<std::string>& existing_ids) {
    std::unordered_map<std::string, int> next_index_by_role;
    for (pugi::xml_node child : node.children()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        // Index by the raw feature name, which is what the path spells.
        const std::string role(local_name(child.name()));
        const int index = next_index_by_role[role]++;
        const std::string child_path = std::format("{}/@{}.{}", path, role, index);

        pugi::xml_attribute id_attr = find_attr_local(child, "id");
        std::string id = id_attr ? std::string(id_attr.value()) : std::string{};
        if (id.empty()) {
            id = id_from_emf_path(child_path);
            // Never shadow an id the document already uses.
            if (!existing_ids.contains(id)) {
                child.append_attribute("xmi:id") = id.c_str();
            }
        }
        path_to_id.emplace(child_path, id);
        assign_emf_path_ids(child, child_path, path_to_id, existing_ids);
    }
}

void collect_existing_ids(const pugi::xml_node& node, std::unordered_set<std::string>& out) {
    if (const pugi::xml_attribute id = find_attr_local(node, "id")) {
        out.insert(id.value());
    }
    for (const pugi::xml_node& child : node.children()) {
        if (child.type() == pugi::node_element) {
            collect_existing_ids(child, out);
        }
    }
}

// Rewrites EMF paths in attribute values to the ids assigned above. Values may
// be space-separated lists (the IDREFS form), so each token is mapped
// independently and unmapped tokens are left alone for the reader to diagnose.
void rewrite_emf_paths(pugi::xml_node node,
                       const std::unordered_map<std::string, std::string>& path_to_id) {
    for (pugi::xml_attribute attr : node.attributes()) {
        const std::string value = attr.value();
        if (value.find("//@") == std::string::npos) {
            continue;
        }
        std::string rewritten;
        std::size_t start = 0;
        while (start <= value.size()) {
            const std::size_t end = value.find(' ', start);
            const std::string token =
                value.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!token.empty()) {
                const auto found = path_to_id.find(token);
                if (!rewritten.empty()) {
                    rewritten.push_back(' ');
                }
                rewritten += found != path_to_id.end() ? found->second : token;
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        attr.set_value(rewritten.c_str());
    }
    for (pugi::xml_node child : node.children()) {
        if (child.type() == pugi::node_element) {
            rewrite_emf_paths(child, path_to_id);
        }
    }
}

// No-op unless the document actually uses EMF paths, so ordinary id-based
// files are untouched.
void normalize_emf_references(pugi::xml_node root) {
    if (!document_uses_emf_paths(root)) {
        return;
    }
    std::unordered_set<std::string> existing_ids;
    collect_existing_ids(root, existing_ids);
    std::unordered_map<std::string, std::string> path_to_id;
    assign_emf_path_ids(root, "/", path_to_id, existing_ids);
    rewrite_emf_paths(root, path_to_id);
}

// xsi:type="sacm:Claim" -> class name, validating the prefix namespace.
std::optional<std::string> read_xsi_type(Reader& reader, const pugi::xml_node& node) {
    for (const pugi::xml_attribute& attr : node.attributes()) {
        const std::string_view name = attr.name();
        if (local_name(name) != "type" || name.starts_with("xmlns")) {
            continue;
        }
        const std::string_view attr_prefix = prefix_of(name);
        const std::string attr_ns = reader.resolve_prefix(attr_prefix);
        const bool is_xsi = attr_ns == metadata::namespaces::kXsi ||
                            (attr_ns.empty() && attr_prefix == metadata::namespaces::kXsiPrefix);
        if (!is_xsi) {
            continue;
        }
        const std::string_view value = attr.value();
        const std::string_view type_prefix = prefix_of(value);
        const std::string type_ns = reader.resolve_prefix(type_prefix);
        if (!type_ns.empty() && !metadata::namespaces::is_accepted_sacm_namespace(type_ns)) {
            // A metamodel that specializes SACM (GSN and friends) types its
            // elements in its own namespace; the element still *is* the SACM
            // class it inherits from, so resolve to that rather than treating
            // the whole subtree as foreign.
            if (const std::optional<detail::ExtensionType> extension =
                    detail::resolve_extension_type(type_ns, local_name(value))) {
                if (!extension->sacm_kind.has_value()) {
                    // Known extension type whose SACM supertype is abstract:
                    // there is nothing concrete to become. Preserve it rather
                    // than silently coercing it into an unrelated class.
                    reader.report(
                        validation::codes::kXmiUnknownElement, Severity::Warning, "SACM23-COMPAT-002",
                        node, {},
                        std::format("xsi:type '{}' extends an abstract SACM class and has no "
                                    "concrete equivalent; preserved as compatibility content",
                                    value));
                    return std::nullopt;
                }
                reader.report(validation::codes::kXmiUnknownElement, Severity::Info,
                              "SACM23-COMPAT-001", node, {},
                              std::format("xsi:type '{}' resolved to its SACM supertype '{}'", value,
                                          metadata::kind_name(*extension->sacm_kind)));
                return std::string(metadata::kind_name(*extension->sacm_kind));
            }
            // Distinguish "we know this metamodel but not this type" from
            // "we do not know this metamodel at all". Both end up preserved,
            // but only the first means our extension table is out of date, and
            // a reader cannot act on the difference if both say the same thing.
            if (detail::is_sacm_extension_namespace(type_ns)) {
                reader.report(
                    validation::codes::kXmiUnknownElement, reader.mode_severity(),
                    "SACM23-COMPAT-002", node, {},
                    std::format("xsi:type '{}' is an unrecognized type in the known SACM-extension "
                                "namespace '{}'; preserved as compatibility content",
                                value, type_ns));
                return std::nullopt;
            }
            reader.report(validation::codes::kXmiUnknownNamespace, reader.mode_severity(),
                          "SACM23-XMI-002", node, {},
                          std::format("xsi:type '{}' resolves to non-SACM namespace '{}'", value,
                                      type_ns));
            if (reader.strict()) {
                return std::nullopt;
            }
        }
        return std::string(local_name(value));
    }
    return std::nullopt;
}

std::unique_ptr<SACMElement> make_element(ElementKind kind, ElementId id) {
    using namespace model;
    switch (kind) {
        case ElementKind::Description:
            return std::make_unique<Description>(std::move(id));
        case ElementKind::ImplementationConstraint:
            return std::make_unique<ImplementationConstraint>(std::move(id));
        case ElementKind::Note:
            return std::make_unique<Note>(std::move(id));
        case ElementKind::TaggedValue:
            return std::make_unique<TaggedValue>(std::move(id));
        case ElementKind::AssuranceCasePackage:
            return std::make_unique<AssuranceCasePackage>(std::move(id));
        case ElementKind::AssuranceCasePackageInterface:
            return std::make_unique<AssuranceCasePackageInterface>(std::move(id));
        case ElementKind::AssuranceCasePackageBinding:
            return std::make_unique<AssuranceCasePackageBinding>(std::move(id));
        case ElementKind::TerminologyPackage:
            return std::make_unique<TerminologyPackage>(std::move(id));
        case ElementKind::TerminologyPackageInterface:
            return std::make_unique<TerminologyPackageInterface>(std::move(id));
        case ElementKind::TerminologyPackageBinding:
            return std::make_unique<TerminologyPackageBinding>(std::move(id));
        case ElementKind::TerminologyGroup:
            return std::make_unique<TerminologyGroup>(std::move(id));
        case ElementKind::Category:
            return std::make_unique<Category>(std::move(id));
        case ElementKind::Expression:
            return std::make_unique<Expression>(std::move(id));
        case ElementKind::Term:
            return std::make_unique<Term>(std::move(id));
        case ElementKind::ArgumentPackage:
            return std::make_unique<ArgumentPackage>(std::move(id));
        case ElementKind::ArgumentPackageInterface:
            return std::make_unique<ArgumentPackageInterface>(std::move(id));
        case ElementKind::ArgumentPackageBinding:
            return std::make_unique<ArgumentPackageBinding>(std::move(id));
        case ElementKind::ArgumentGroup:
            return std::make_unique<ArgumentGroup>(std::move(id));
        case ElementKind::Claim:
            return std::make_unique<Claim>(std::move(id));
        case ElementKind::ArgumentReasoning:
            return std::make_unique<ArgumentReasoning>(std::move(id));
        case ElementKind::ArtifactReference:
            return std::make_unique<ArtifactReference>(std::move(id));
        case ElementKind::AssertedInference:
            return std::make_unique<AssertedInference>(std::move(id));
        case ElementKind::AssertedEvidence:
            return std::make_unique<AssertedEvidence>(std::move(id));
        case ElementKind::AssertedContext:
            return std::make_unique<AssertedContext>(std::move(id));
        case ElementKind::AssertedArtifactSupport:
            return std::make_unique<AssertedArtifactSupport>(std::move(id));
        case ElementKind::AssertedArtifactContext:
            return std::make_unique<AssertedArtifactContext>(std::move(id));
        case ElementKind::ArtifactPackage:
            return std::make_unique<ArtifactPackage>(std::move(id));
        case ElementKind::ArtifactPackageInterface:
            return std::make_unique<ArtifactPackageInterface>(std::move(id));
        case ElementKind::ArtifactPackageBinding:
            return std::make_unique<ArtifactPackageBinding>(std::move(id));
        case ElementKind::ArtifactGroup:
            return std::make_unique<ArtifactGroup>(std::move(id));
        case ElementKind::Artifact:
            return std::make_unique<Artifact>(std::move(id));
        case ElementKind::ArtifactAssetRelationship:
            return std::make_unique<ArtifactAssetRelationship>(std::move(id));
        case ElementKind::Activity:
            return std::make_unique<Activity>(std::move(id));
        case ElementKind::Event:
            return std::make_unique<Event>(std::move(id));
        case ElementKind::Participant:
            return std::make_unique<Participant>(std::move(id));
        case ElementKind::Technique:
            return std::make_unique<Technique>(std::move(id));
        case ElementKind::Resource:
            return std::make_unique<Resource>(std::move(id));
        case ElementKind::Property:
            return std::make_unique<Property>(std::move(id));
    }
    return nullptr;
}

void populate(Reader& reader, SACMElement& element, const pugi::xml_node& node);

// Reads an element id (xmi:id preferred), generating one in tolerant mode.
std::optional<ElementId> read_element_id(Reader& reader, const pugi::xml_node& node,
                                         ElementKind kind) {
    const pugi::xml_attribute attr = find_attr_local(node, "id");
    if (attr) {
        reader.used_ids.insert(attr.value());
        return ElementId{attr.value()};
    }
    if (reader.strict()) {
        reader.report(validation::codes::kXmiMissingId, Severity::Error, "SACM23-XMI-001", node, {},
                      std::format("{} element has no xmi:id", metadata::kind_name(kind)));
        return std::nullopt;
    }
    reader.report(validation::codes::kXmiMissingId, Severity::Warning, "SACM23-XMI-001", node, {},
                  std::format("{} element has no xmi:id; generated one", metadata::kind_name(kind)));
    return reader.generate_id(kind);
}

// Creates and populates a child element of `kind` from `node`.
std::unique_ptr<SACMElement> read_element(Reader& reader, ElementKind kind,
                                          const pugi::xml_node& node) {
    const std::optional<ElementId> id = read_element_id(reader, node, kind);
    if (!id.has_value()) {
        return nullptr;
    }
    std::unique_ptr<SACMElement> element = make_element(kind, *id);
    populate(reader, *element, node);
    return element;
}

// --- LangString / MultiLangString ------------------------------------------

model::LangString read_lang_string(Reader& reader, const pugi::xml_node& node) {
    model::LangString value;
    // LangString is modelled as a value type: it generalizes Element (not
    // SACMElement), carries no gid, and nothing in SACM 2.3 references one --
    // every appearance in the metamodel is a containment role. So an xmi:id
    // here cannot be the target of any reference and is not preserved. Say so
    // rather than dropping it silently: external tooling may still anchor to
    // it, and unannounced loss is what the compliance policy forbids.
    if (const pugi::xml_attribute id = find_attr_local(node, "id")) {
        reader.report(validation::codes::kXmiUnknownElement, Severity::Info, "SACM23-XMI-001", node,
                      {}, std::format("LangString xmi:id '{}' is not preserved: LangString is a "
                                      "value type and nothing in SACM 2.3 can reference one",
                                      id.value()));
    }
    value.lang = node.attribute("lang").value();
    if (const pugi::xml_attribute content = node.attribute("content")) {
        value.content = content.value();
    } else {
        value.content = node.text().get();
    }
    if (const pugi::xml_attribute expression = find_attr_local(node, "expression")) {
        value.expression_ref = ElementId{strip_fragment(expression.value())};
    } else if (const pugi::xml_node child = node.find_child([](const pugi::xml_node& n) {
                   return local_name(n.name()) == "expression";
               })) {
        const pugi::xml_attribute ref = find_attr_local(child, "href")
                                            ? find_attr_local(child, "href")
                                            : find_attr_local(child, "ref");
        if (ref) {
            value.expression_ref = ElementId{strip_fragment(ref.value())};
        }
    }
    (void)reader;
    return value;
}

// Reads MultiLangString content from `wrapper` (a <content>/<key> node).
// Strict shape: <content><value lang=".." content=".."/></content>.
// App shape handled by the caller (sibling <content lang>text</content>
// nodes each contribute one LangString).
void read_multi_lang_values(Reader& reader, const pugi::xml_node& wrapper,
                            model::MultiLangString& out) {
    bool has_value_children = false;
    for (const pugi::xml_node& child : wrapper.children()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        if (local_name(child.name()) == "value") {
            has_value_children = true;
            out.values.push_back(read_lang_string(reader, child));
        }
    }
    if (!has_value_children) {
        // Single-language shorthand: lang/content attributes or inner text.
        model::LangString value = read_lang_string(reader, wrapper);
        if (!value.content.empty() || !value.lang.empty() || value.expression_ref.has_value()) {
            out.values.push_back(std::move(value));
        }
    }
}

// --- reference helpers -------------------------------------------------------

// Reads a reference child node: ref= / href="#id" / xmi:idref.
std::optional<ElementId> read_ref_node(Reader& reader, const pugi::xml_node& node) {
    for (const std::string_view attr_name : {"ref", "href", "idref"}) {
        if (const pugi::xml_attribute attr = find_attr_local(node, attr_name)) {
            const std::string_view value = attr.value();
            if (is_external_href(value)) {
                reader.report(validation::codes::kXmiExternalReference, Severity::Warning,
                              "SACM23-XMI-003", node, {},
                              std::format("external reference '{}' is not supported and was "
                                          "ignored",
                                          value));
                return std::nullopt;
            }
            return ElementId{strip_fragment(value)};
        }
    }
    // Bare text fallback: <source>id</source>.
    const std::string_view text = node.text().get();
    if (!text.empty()) {
        return ElementId{strip_fragment(text)};
    }
    return std::nullopt;
}

void append_idrefs(std::vector<ElementId>& out, std::string_view idrefs) {
    std::size_t start = 0;
    while (start < idrefs.size()) {
        std::size_t end = idrefs.find_first_of(" \t\r\n,", start);
        if (end == std::string_view::npos) {
            end = idrefs.size();
        }
        if (end > start) {
            out.push_back(ElementId{strip_fragment(idrefs.substr(start, end - start))});
        }
        start = end + 1;
    }
}

// --- per-family population ---------------------------------------------------

void read_common_attributes(Reader& reader, SACMElement& element, const pugi::xml_node& node) {
    if (const pugi::xml_attribute gid = node.attribute("gid")) {
        Access::gid(element) = gid.value();
    }
    if (const pugi::xml_attribute cite = node.attribute("isCitation")) {
        Access::is_citation(element) = parse_bool(cite.value());
    }
    if (const pugi::xml_attribute abstract_flag = node.attribute("isAbstract")) {
        Access::is_abstract(element) = parse_bool(abstract_flag.value());
    }
    if (const pugi::xml_attribute cited = node.attribute("citedElement")) {
        Access::cited_element(element) = ElementId{strip_fragment(cited.value())};
    }
    if (const pugi::xml_attribute abstract_form = node.attribute("abstractForm")) {
        Access::abstract_form(element) = ElementId{strip_fragment(abstract_form.value())};
    }
    (void)reader;
}

// Appends `child` (already typed) into the right containment vector of
// `parent`. Returns false when the child kind is not containable here.
bool attach_child(Reader& reader, SACMElement& parent, std::unique_ptr<SACMElement> child,
                  const pugi::xml_node& node) {
    const ElementKind kind = child->kind();
    SACMElement* raw = child.get();

    const auto downcast = [&child]<typename T>() {
        return std::unique_ptr<T>(static_cast<T*>(child.release()));
    };

    if (auto* acp = dynamic_cast<model::AssuranceCasePackage*>(&parent)) {
        if (dynamic_cast<model::AssuranceCasePackage*>(raw) != nullptr) {
            Access::assurance_case_packages(*acp).push_back(
                downcast.operator()<model::AssuranceCasePackage>());
        } else if (dynamic_cast<model::ArgumentPackage*>(raw) != nullptr) {
            Access::argument_packages(*acp).push_back(downcast.operator()<model::ArgumentPackage>());
        } else if (dynamic_cast<model::ArtifactPackage*>(raw) != nullptr) {
            Access::artifact_packages(*acp).push_back(downcast.operator()<model::ArtifactPackage>());
        } else if (dynamic_cast<model::TerminologyPackage*>(raw) != nullptr) {
            Access::terminology_packages(*acp).push_back(
                downcast.operator()<model::TerminologyPackage>());
        } else {
            return false;
        }
        Access::set_parent(*raw, acp);
        return true;
    }
    if (auto* pkg = dynamic_cast<model::ArgumentPackage*>(&parent)) {
        if (!detail::kind_is_argumentation_element(kind)) {
            return false;
        }
        Access::argument_elements(*pkg).push_back(downcast.operator()<model::ArgumentationElement>());
        Access::set_parent(*raw, pkg);
        return true;
    }
    if (auto* pkg = dynamic_cast<model::ArtifactPackage*>(&parent)) {
        if (!detail::kind_is_artifact_element_in_artifact_package(kind)) {
            return false;
        }
        Access::artifact_elements(*pkg).push_back(downcast.operator()<model::ArtifactElement>());
        Access::set_parent(*raw, pkg);
        return true;
    }
    if (auto* pkg = dynamic_cast<model::TerminologyPackage*>(&parent)) {
        if (!detail::kind_is_terminology_element(kind)) {
            return false;
        }
        Access::terminology_elements(*pkg).push_back(
            downcast.operator()<model::TerminologyElement>());
        Access::set_parent(*raw, pkg);
        return true;
    }
    if (auto* asset = dynamic_cast<model::ArtifactAsset*>(&parent)) {
        if (kind != ElementKind::Property) {
            return false;
        }
        Access::properties(*asset).push_back(downcast.operator()<model::Property>());
        Access::set_parent(*raw, asset);
        return true;
    }
    (void)reader;
    (void)node;
    return false;
}

// Handles a containment child whose concrete kind must be resolved from
// xsi:type, the role's declared type, or (tolerant) the class name.
void read_containment_child(Reader& reader, SACMElement& parent, const pugi::xml_node& node,
                            std::optional<ElementKind> declared_kind) {
    std::optional<ElementKind> kind;
    if (const std::optional<std::string> xsi_type = read_xsi_type(reader, node)) {
        kind = detail::kind_from_class_name(*xsi_type);
        if (!kind.has_value()) {
            kind = detail::kind_from_class_name_ci(*xsi_type);
        }
        if (!kind.has_value()) {
            reader.report(validation::codes::kXmiUnknownElement, reader.mode_severity(),
                          "SACM23-XMI-001", node, {parent.id()},
                          std::format("unknown xsi:type '{}'", *xsi_type));
            return;
        }
    } else if (declared_kind.has_value()) {
        kind = declared_kind;
    } else {
        // Abstract role type without xsi:type: tolerant falls back to the
        // element's local name as a class name.
        kind = detail::kind_from_class_name_ci(local_name(node.name()));
        if (reader.strict()) {
            reader.report(validation::codes::kXmiMissingType, Severity::Error, "SACM23-XMI-001",
                          node, {parent.id()},
                          std::format("containment role '{}' has an abstract type and needs "
                                      "xsi:type",
                                      local_name(node.name())));
            return;
        }
    }
    if (!kind.has_value()) {
        reader.report(validation::codes::kXmiUnknownElement, reader.mode_severity(),
                      "SACM23-XMI-003", node, {parent.id()},
                      std::format("unknown element '{}'", node.name()));
        return;
    }
    std::unique_ptr<SACMElement> child = read_element(reader, *kind, node);
    if (child == nullptr) {
        return;
    }
    if (!attach_child(reader, parent, std::move(child), node)) {
        reader.report(validation::codes::kXmiUnknownElement, reader.mode_severity(),
                      "SACM23-XMI-001", node, {parent.id()},
                      std::format("a {} cannot be contained in a {}", metadata::kind_name(*kind),
                                  metadata::kind_name(parent.kind())));
    }
}

// Reads a UtilityElement (Description/Note/ImplementationConstraint) child.
template <typename T>
std::unique_ptr<T> read_utility(Reader& reader, ElementKind kind, const pugi::xml_node& node) {
    std::optional<ElementId> id;
    if (const pugi::xml_attribute attr = find_attr_local(node, "id")) {
        reader.used_ids.insert(attr.value());
        id = ElementId{attr.value()};
    } else {
        id = reader.generate_id(kind);
    }
    auto element = std::make_unique<T>(*id);
    read_common_attributes(reader, *element, node);

    model::MultiLangString& content = Access::content(*element);
    bool saw_content_wrapper = false;
    for (const pugi::xml_node& child : node.children()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        if (local_name(child.name()) == "content") {
            saw_content_wrapper = true;
            // Strict shape: one wrapper with <value/> children. App shape:
            // repeated <content lang>text</content> children, one language
            // each — read_multi_lang_values handles both.
            read_multi_lang_values(reader, child, content);
        }
    }
    if (!saw_content_wrapper) {
        // Inner text or content attribute directly on the utility node.
        model::LangString value = read_lang_string(reader, node);
        if (!value.content.empty()) {
            content.values.push_back(std::move(value));
        }
    }
    return element;
}

void read_model_element_children(Reader& reader, model::ModelElement& element,
                                 const pugi::xml_node& node) {
    for (const pugi::xml_node& child : node.children()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        const std::string_view role = normalize_role(local_name(child.name()));
        if (role == "name") {
            // Strict shape: <name lang content/>. Legacy multi-language
            // shape: <name><content lang>text</content>...</name> — the
            // first entry becomes the name (clause 8.6: LangString[1]); the
            // rest are kept losslessly in a TaggedValue with the reserved
            // key "sacm.import.name" (clause 8.12 extension mechanism).
            model::MultiLangString values;
            for (const pugi::xml_node& entry : child.children()) {
                if (entry.type() == pugi::node_element &&
                    (local_name(entry.name()) == "content" ||
                     local_name(entry.name()) == "value")) {
                    values.values.push_back(read_lang_string(reader, entry));
                }
            }
            if (values.values.empty()) {
                Access::name(element) = read_lang_string(reader, child);
            } else {
                Access::name(element) = values.values.front();
                if (values.values.size() > 1) {
                    auto overflow = std::make_unique<model::TaggedValue>(
                        reader.generate_id(ElementKind::TaggedValue));
                    Access::key(*overflow).set("", "sacm.import.name");
                    Access::content(*overflow).values.assign(values.values.begin() + 1,
                                                             values.values.end());
                    Access::set_parent(*overflow, &element);
                    Access::tagged_values(element).push_back(std::move(overflow));
                    reader.report(validation::codes::kXmiUnknownElement, Severity::Info,
                                  "SACM23-BASE-001", child, {element.id()},
                                  "multi-language name mapped to TaggedValue 'sacm.import.name' "
                                  "(clause 8.6 allows one name LangString)");
                }
            }
        } else if (role == "description") {
            Access::descriptions(element).push_back(
                read_utility<model::Description>(reader, ElementKind::Description, child));
            Access::set_parent(*Access::descriptions(element).back(), &element);
        } else if (role == "implementationConstraint") {
            Access::implementation_constraints(element)
                .push_back(read_utility<model::ImplementationConstraint>(
                    reader, ElementKind::ImplementationConstraint, child));
            Access::set_parent(*Access::implementation_constraints(element).back(), &element);
        } else if (role == "note") {
            Access::notes(element).push_back(
                read_utility<model::Note>(reader, ElementKind::Note, child));
            Access::set_parent(*Access::notes(element).back(), &element);
        } else if (role == "taggedValue") {
            // TaggedValue: strict shape has <key> and <content> wrappers;
            // app shape is key="..." value="..." attributes.
            std::optional<ElementId> id;
            if (const pugi::xml_attribute attr = find_attr_local(child, "id")) {
                reader.used_ids.insert(attr.value());
                id = ElementId{attr.value()};
            } else {
                id = reader.generate_id(ElementKind::TaggedValue);
            }
            auto tagged = std::make_unique<model::TaggedValue>(*id);
            read_common_attributes(reader, *tagged, child);
            if (const pugi::xml_attribute key = child.attribute("key")) {
                Access::key(*tagged).set("", key.value());
            }
            if (const pugi::xml_attribute value = child.attribute("value")) {
                Access::content(*tagged).set("", value.value());
            }
            for (const pugi::xml_node& part : child.children()) {
                if (part.type() != pugi::node_element) {
                    continue;
                }
                if (local_name(part.name()) == "key") {
                    read_multi_lang_values(reader, part, Access::key(*tagged));
                } else if (local_name(part.name()) == "content") {
                    read_multi_lang_values(reader, part, Access::content(*tagged));
                }
            }
            Access::set_parent(*tagged, &element);
            Access::tagged_values(element).push_back(std::move(tagged));
        }
    }
    // Attribute shorthands (tolerant inputs; harmless in strict re-reads).
    if (const pugi::xml_attribute name = node.attribute("name")) {
        model::LangString& target = Access::name(element);
        if (target.content.empty()) {
            target.content = name.value();
        }
    }
    if (const pugi::xml_attribute description = node.attribute("description")) {
        auto holder = std::make_unique<model::Description>(
            reader.generate_id(ElementKind::Description));
        Access::content(*holder).set("", description.value());
        Access::set_parent(*holder, &element);
        Access::descriptions(element).push_back(std::move(holder));
    }
}

// Claim-style content tolerance: <content lang>text</content> children or a
// content="" attribute directly on an argumentation element map to a
// Description (clause 8.9: Descriptions provide the content of a Claim).
void read_claim_content_tolerance(Reader& reader, model::ModelElement& element,
                                  const pugi::xml_node& node) {
    model::MultiLangString collected;
    if (const pugi::xml_attribute content = node.attribute("content")) {
        collected.set("", content.value());
    }
    for (const pugi::xml_node& child : node.children()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        const std::string_view role = normalize_role(local_name(child.name()));
        if (role == "content" || role == "statement") {
            model::LangString value = read_lang_string(reader, child);
            if (!value.content.empty() || value.expression_ref.has_value()) {
                collected.values.push_back(std::move(value));
            }
        }
    }
    if (collected.values.empty()) {
        return;
    }
    auto holder =
        std::make_unique<model::Description>(reader.generate_id(ElementKind::Description));
    Access::content(*holder) = std::move(collected);
    Access::set_parent(*holder, &element);
    Access::descriptions(element).push_back(std::move(holder));
}

void read_reference_children(Reader& reader, SACMElement& element, const pugi::xml_node& node) {
    const auto add_ref = [&](std::vector<ElementId>& out, const pugi::xml_node& child) {
        if (const std::optional<ElementId> id = read_ref_node(reader, child)) {
            out.push_back(*id);
        }
    };
    const auto set_ref = [&](std::optional<ElementId>& out, const pugi::xml_node& child) {
        if (const std::optional<ElementId> id = read_ref_node(reader, child)) {
            out = *id;
        }
    };

    for (const pugi::xml_node& child : node.children()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        const std::string_view role = normalize_role(local_name(child.name()));
        if (role == "citedElement") {
            set_ref(Access::cited_element(element), child);
            continue;
        }
        if (role == "abstractForm") {
            set_ref(Access::abstract_form(element), child);
            continue;
        }
        if (auto* rel = dynamic_cast<model::AssertedRelationship*>(&element)) {
            if (role == "source") {
                add_ref(Access::sources(*rel), child);
                continue;
            }
            if (role == "target") {
                add_ref(Access::targets(*rel), child);
                continue;
            }
            if (role == "reasoning") {
                set_ref(Access::reasoning(*rel), child);
                continue;
            }
        }
        if (auto* assertion = dynamic_cast<model::Assertion*>(&element)) {
            if (role == "metaClaim") {
                add_ref(Access::meta_claims(*assertion), child);
                continue;
            }
        }
        if (auto* rel = dynamic_cast<model::ArtifactAssetRelationship*>(&element)) {
            if (role == "source") {
                add_ref(Access::sources(*rel), child);
                continue;
            }
            if (role == "target") {
                add_ref(Access::targets(*rel), child);
                continue;
            }
        }
        if (auto* reference = dynamic_cast<model::ArtifactReference*>(&element)) {
            if (role == "referencedArtifactElement" || role == "referencedArtifact") {
                add_ref(Access::referenced_artifact_elements(*reference), child);
                continue;
            }
        }
        if (auto* reasoning = dynamic_cast<model::ArgumentReasoning*>(&element)) {
            if (role == "structure") {
                set_ref(Access::structure(*reasoning), child);
                continue;
            }
        }
        if (auto* group = dynamic_cast<model::ArgumentGroup*>(&element)) {
            if (role == "argumentElement") {
                add_ref(Access::argument_elements(*group), child);
                continue;
            }
        }
        if (auto* group = dynamic_cast<model::ArtifactGroup*>(&element)) {
            if (role == "artifactElement") {
                add_ref(Access::artifact_elements(*group), child);
                continue;
            }
        }
        if (auto* group = dynamic_cast<model::TerminologyGroup*>(&element)) {
            if (role == "terminologyElement") {
                add_ref(Access::terminology_elements(*group), child);
                continue;
            }
        }
        if (auto* category = dynamic_cast<model::Category*>(&element)) {
            if (role == "category") {
                add_ref(Access::categories(*category), child);
                continue;
            }
        }
        if (auto* expr_element = dynamic_cast<model::ExpressionElement*>(&element)) {
            if (role == "category") {
                add_ref(Access::categories(*expr_element), child);
                continue;
            }
        }
        if (auto* expression = dynamic_cast<model::Expression*>(&element)) {
            if (role == "element") {
                add_ref(Access::elements(*expression), child);
                continue;
            }
        }
        if (auto* term = dynamic_cast<model::Term*>(&element)) {
            if (role == "origin") {
                set_ref(Access::origin(*term), child);
                continue;
            }
        }
        // interface/implements/participantPackage per package family.
        if (role == "interface") {
            if (auto* acp = dynamic_cast<model::AssuranceCasePackage*>(&element)) {
                add_ref(Access::interfaces(*acp), child);
                continue;
            }
            if (auto* pkg = dynamic_cast<model::ArgumentPackage*>(&element)) {
                add_ref(Access::interfaces(*pkg), child);
                continue;
            }
            if (auto* pkg = dynamic_cast<model::ArtifactPackage*>(&element)) {
                add_ref(Access::interfaces(*pkg), child);
                continue;
            }
            if (auto* pkg = dynamic_cast<model::TerminologyPackage*>(&element)) {
                add_ref(Access::interfaces(*pkg), child);
                continue;
            }
        }
        if (role == "implements") {
            if (auto* iface = dynamic_cast<model::AssuranceCasePackageInterface*>(&element)) {
                set_ref(Access::implements(*iface), child);
                continue;
            }
            if (auto* iface = dynamic_cast<model::ArgumentPackageInterface*>(&element)) {
                set_ref(Access::implements(*iface), child);
                continue;
            }
            if (auto* iface = dynamic_cast<model::ArtifactPackageInterface*>(&element)) {
                set_ref(Access::implements(*iface), child);
                continue;
            }
            if (auto* iface = dynamic_cast<model::TerminologyPackageInterface*>(&element)) {
                set_ref(Access::implements(*iface), child);
                continue;
            }
        }
        if (role == "participantPackage") {
            if (auto* binding = dynamic_cast<model::AssuranceCasePackageBinding*>(&element)) {
                add_ref(Access::participant_packages(*binding), child);
                continue;
            }
            if (auto* binding = dynamic_cast<model::ArgumentPackageBinding*>(&element)) {
                add_ref(Access::participant_packages(*binding), child);
                continue;
            }
            if (auto* binding = dynamic_cast<model::ArtifactPackageBinding*>(&element)) {
                add_ref(Access::participant_packages(*binding), child);
                continue;
            }
            if (auto* binding = dynamic_cast<model::TerminologyPackageBinding*>(&element)) {
                add_ref(Access::participant_packages(*binding), child);
                continue;
            }
        }
    }
}

// True when this node is typed by a SACM-extension relationship whose endpoint
// direction is inverted relative to the SACM class it specializes.
bool extension_reverses_endpoints(Reader& reader, const pugi::xml_node& node) {
    for (const pugi::xml_attribute& attr : node.attributes()) {
        const std::string_view name = attr.name();
        if (local_name(name) != "type" || name.starts_with("xmlns")) {
            continue;
        }
        const std::string_view value = attr.value();
        const std::string type_ns = reader.resolve_prefix(prefix_of(value));
        const std::optional<detail::ExtensionType> extension =
            detail::resolve_extension_type(type_ns, local_name(value));
        return extension.has_value() && extension->reverse_endpoints;
    }
    return false;
}

// Vendor-extension attributes are attributes whose prefix resolves to a
// namespace that is neither SACM nor the XMI/XSI infrastructure. They carry
// real information, so tolerant loads keep them and strict save refuses,
// exactly as for unknown child elements. Without this they vanished with no
// diagnostic and strict save reported success — a silent edit to the document.
void capture_vendor_attributes(Reader& reader, SACMElement& element,
                               const pugi::xml_node& node) {
    for (const pugi::xml_attribute& attr : node.attributes()) {
        const std::string_view name = attr.name();
        if (name.starts_with("xmlns")) {
            continue;
        }
        const std::string_view attr_prefix = prefix_of(name);
        if (attr_prefix.empty()) {
            // Losslessness gate: an unprefixed attribute the serialization does
            // not define is one the reader silently ignores. Preserve and
            // report it rather than dropping it without a trace.
            if (detail::is_known_sacm_attribute(name)) {
                continue;
            }
            if (reader.strict()) {
                reader.report(validation::codes::kXmiUnknownElement, Severity::Error,
                              "SACM23-XMI-003", node, {element.id()},
                              std::format("unknown attribute '{}' on {}", name,
                                          metadata::kind_name(element.kind())));
                continue;
            }
            Access::preserved_attributes(element).push_back(
                std::format(R"({}="{}")", name, attr.value()));
            reader.report(validation::codes::kXmiUnknownElement, Severity::Warning,
                          "SACM23-COMPAT-001", node, {element.id()},
                          std::format("unknown attribute '{}' on {} preserved as compatibility "
                                      "content",
                                      name, metadata::kind_name(element.kind())));
            continue;
        }
        const std::string attr_ns = reader.resolve_prefix(attr_prefix);
        if (attr_ns.empty() || metadata::namespaces::is_xmi_namespace(attr_ns) ||
            attr_ns == metadata::namespaces::kXsi ||
            metadata::namespaces::is_accepted_sacm_namespace(attr_ns) ||
            detail::is_sacm_extension_namespace(attr_ns)) {
            continue;
        }
        if (reader.strict()) {
            reader.report(validation::codes::kXmiUnknownElement, Severity::Error,
                          "SACM23-XMI-003", node, {element.id()},
                          std::format("unknown attribute '{}' from foreign namespace '{}'",
                                      name, attr_ns));
            continue;
        }
        Access::preserved_attributes(element).push_back(
            std::format(R"({}="{}")", name, attr.value()));
        reader.report(validation::codes::kXmiUnknownElement, Severity::Warning,
                      "SACM23-COMPAT-001", node, {element.id()},
                      std::format("attribute '{}' from foreign namespace '{}' preserved as "
                                  "compatibility content",
                                  name, attr_ns));
    }
}

void read_reference_attributes(Reader& reader, SACMElement& element, const pugi::xml_node& node) {
    capture_vendor_attributes(reader, element, node);
    const auto idrefs_attr = [&](std::string_view name, std::vector<ElementId>& out) {
        if (const pugi::xml_attribute attr = node.attribute(std::string(name).c_str())) {
            append_idrefs(out, attr.value());
        }
    };
    const auto ref_attr = [&](std::string_view name, std::optional<ElementId>& out) {
        if (const pugi::xml_attribute attr = node.attribute(std::string(name).c_str())) {
            out = ElementId{strip_fragment(attr.value())};
        }
    };

    if (auto* rel = dynamic_cast<model::AssertedRelationship*>(&element)) {
        // A GSN SupportedBy/InContextOf stores its endpoints in the same slots
        // as the SACM relationship it specializes but means them the other way
        // round, so importing without the swap would reverse the direction of
        // every inference in the argument.
        const bool reversed = extension_reverses_endpoints(reader, node);
        idrefs_attr(reversed ? "target" : "source", Access::sources(*rel));
        idrefs_attr(reversed ? "source" : "target", Access::targets(*rel));
        ref_attr("reasoning", Access::reasoning(*rel));
        if (reversed) {
            reader.report(validation::codes::kXmiUnknownElement, Severity::Info,
                          "SACM23-COMPAT-002", node, {element.id()},
                          "GSN relationship endpoints swapped to SACM source/target direction");
        }
    }
    if (auto* assertion = dynamic_cast<model::Assertion*>(&element)) {
        idrefs_attr("metaClaim", Access::meta_claims(*assertion));
    }
    if (auto* rel = dynamic_cast<model::ArtifactAssetRelationship*>(&element)) {
        idrefs_attr("source", Access::sources(*rel));
        idrefs_attr("target", Access::targets(*rel));
    }
    if (auto* reference = dynamic_cast<model::ArtifactReference*>(&element)) {
        idrefs_attr("referencedArtifactElement", Access::referenced_artifact_elements(*reference));
        idrefs_attr("referencedArtifact", Access::referenced_artifact_elements(*reference));
    }
    if (auto* reasoning = dynamic_cast<model::ArgumentReasoning*>(&element)) {
        ref_attr("structure", Access::structure(*reasoning));
    }
    if (auto* group = dynamic_cast<model::ArgumentGroup*>(&element)) {
        idrefs_attr("argumentElement", Access::argument_elements(*group));
    }
    if (auto* group = dynamic_cast<model::ArtifactGroup*>(&element)) {
        idrefs_attr("artifactElement", Access::artifact_elements(*group));
    }
    if (auto* group = dynamic_cast<model::TerminologyGroup*>(&element)) {
        idrefs_attr("terminologyElement", Access::terminology_elements(*group));
    }
    if (auto* category = dynamic_cast<model::Category*>(&element)) {
        idrefs_attr("category", Access::categories(*category));
    }
    if (auto* expr_element = dynamic_cast<model::ExpressionElement*>(&element)) {
        idrefs_attr("category", Access::categories(*expr_element));
    }
    if (auto* expression = dynamic_cast<model::Expression*>(&element)) {
        idrefs_attr("element", Access::elements(*expression));
    }
    if (auto* term = dynamic_cast<model::Term*>(&element)) {
        ref_attr("origin", Access::origin(*term));
    }
    if (auto* acp = dynamic_cast<model::AssuranceCasePackage*>(&element)) {
        idrefs_attr("interface", Access::interfaces(*acp));
    } else if (auto* pkg = dynamic_cast<model::ArgumentPackage*>(&element)) {
        idrefs_attr("interface", Access::interfaces(*pkg));
    } else if (auto* pkg = dynamic_cast<model::ArtifactPackage*>(&element)) {
        idrefs_attr("interface", Access::interfaces(*pkg));
    } else if (auto* pkg = dynamic_cast<model::TerminologyPackage*>(&element)) {
        idrefs_attr("interface", Access::interfaces(*pkg));
    }
    if (auto* iface = dynamic_cast<model::AssuranceCasePackageInterface*>(&element)) {
        ref_attr("implements", Access::implements(*iface));
    } else if (auto* iface = dynamic_cast<model::ArgumentPackageInterface*>(&element)) {
        ref_attr("implements", Access::implements(*iface));
    } else if (auto* iface = dynamic_cast<model::ArtifactPackageInterface*>(&element)) {
        ref_attr("implements", Access::implements(*iface));
    } else if (auto* iface = dynamic_cast<model::TerminologyPackageInterface*>(&element)) {
        ref_attr("implements", Access::implements(*iface));
    }
    if (auto* binding = dynamic_cast<model::AssuranceCasePackageBinding*>(&element)) {
        idrefs_attr("participantPackage", Access::participant_packages(*binding));
    } else if (auto* binding = dynamic_cast<model::ArgumentPackageBinding*>(&element)) {
        idrefs_attr("participantPackage", Access::participant_packages(*binding));
    } else if (auto* binding = dynamic_cast<model::ArtifactPackageBinding*>(&element)) {
        idrefs_attr("participantPackage", Access::participant_packages(*binding));
    } else if (auto* binding = dynamic_cast<model::TerminologyPackageBinding*>(&element)) {
        idrefs_attr("participantPackage", Access::participant_packages(*binding));
    }
    (void)reader;
}

void read_kind_specific_attributes(Reader& reader, SACMElement& element,
                                   const pugi::xml_node& node) {
    if (auto* assertion = dynamic_cast<model::Assertion*>(&element)) {
        if (const pugi::xml_attribute attr = node.attribute("assertionDeclaration")) {
            if (const std::optional<model::AssertionDeclaration> parsed =
                    model::parse_assertion_declaration(attr.value())) {
                Access::assertion_declaration(*assertion) = *parsed;
            } else {
                reader.report(validation::codes::kEnumInvalidLiteral, reader.mode_severity(),
                              "SACM23-ARG-001", node, {element.id()},
                              std::format("invalid assertionDeclaration literal '{}'",
                                          attr.value()));
            }
        }
        // Legacy GSN shorthand: an `undeveloped="true"` attribute is the
        // pre-SACM way Assurance Forge marked a goal as not yet argued. GSN's
        // own transformation maps undeveloped to assertionDeclaration =
        // needsSupport (docs/sacm/sacm-gsn-mapping.md), so normalize to that
        // rather than carrying a non-SACM boolean. Only when the declaration is
        // still the default `asserted` -- an explicit assumed/axiomatic/defeated
        // is more specific and must win.
        if (parse_bool(node.attribute("undeveloped").value()) &&
            assertion->assertion_declaration() == model::AssertionDeclaration::Asserted) {
            Access::assertion_declaration(*assertion) = model::AssertionDeclaration::NeedsSupport;
            reader.report(validation::codes::kXmiUnknownElement, Severity::Info, "SACM23-COMPAT-001",
                          node, {element.id()},
                          "legacy undeveloped=\"true\" normalized to assertionDeclaration="
                          "needsSupport");
        }
    }
    if (auto* rel = dynamic_cast<model::AssertedRelationship*>(&element)) {
        if (const pugi::xml_attribute attr = node.attribute("isCounter")) {
            Access::is_counter(*rel) = parse_bool(attr.value());
        }
    }
    if (auto* artifact = dynamic_cast<model::Artifact*>(&element)) {
        Access::version(*artifact) = node.attribute("version").value();
        Access::date(*artifact) = node.attribute("date").value();
    }
    if (auto* activity = dynamic_cast<model::Activity*>(&element)) {
        Access::start_time(*activity) = node.attribute("startTime").value();
        Access::end_time(*activity) = node.attribute("endTime").value();
    }
    if (auto* event = dynamic_cast<model::Event*>(&element)) {
        // `date` per the specification text; `occurece`/`occurence` are
        // ptc/22-03-13 spellings accepted on import.
        for (const char* name : {"date", "occurece", "occurence"}) {
            if (const pugi::xml_attribute attr = node.attribute(name)) {
                Access::date(*event) = attr.value();
                break;
            }
        }
    }
    if (auto* expr_element = dynamic_cast<model::ExpressionElement*>(&element)) {
        if (const pugi::xml_attribute attr = node.attribute("value")) {
            Access::value(*expr_element) = attr.value();
        }
    }
    if (auto* term = dynamic_cast<model::Term*>(&element)) {
        if (const pugi::xml_attribute attr = node.attribute("externalReference")) {
            Access::external_reference(*term) = attr.value();
        }
    }
}

// Containment roles per family; returns the declared child kind when the
// role's declared type is concrete.
std::optional<ElementKind> containment_role_kind(const SACMElement& parent,
                                                 std::string_view role) {
    if (dynamic_cast<const model::AssuranceCasePackage*>(&parent) != nullptr) {
        if (role == "assuranceCasePackage") return ElementKind::AssuranceCasePackage;
        if (role == "argumentPackage") return ElementKind::ArgumentPackage;
        if (role == "artifactPackage") return ElementKind::ArtifactPackage;
        if (role == "terminologyPackage") return ElementKind::TerminologyPackage;
    }
    if (dynamic_cast<const model::ArtifactAsset*>(&parent) != nullptr && role == "property") {
        return ElementKind::Property;
    }
    return std::nullopt;
}

bool is_abstract_containment_role(const SACMElement& parent, std::string_view role) {
    if (role == "argumentElement" &&
        dynamic_cast<const model::ArgumentPackage*>(&parent) != nullptr) {
        return true;
    }
    if (role == "artifactElement" &&
        dynamic_cast<const model::ArtifactPackage*>(&parent) != nullptr) {
        return true;
    }
    if (role == "terminologyElement" &&
        dynamic_cast<const model::TerminologyPackage*>(&parent) != nullptr) {
        return true;
    }
    return false;
}

constexpr std::string_view kCommonRoles[] = {
    "name",          "description", "implementationConstraint",
    "note",          "taggedValue", "content",
    "statement",     "citedElement", "abstractForm",
};

bool is_common_role(std::string_view role) {
    for (const std::string_view known : kCommonRoles) {
        if (role == known) {
            return true;
        }
    }
    return false;
}

bool is_reference_role(const SACMElement& element, std::string_view role) {
    if (role == "source" || role == "target" || role == "reasoning" || role == "metaClaim" ||
        role == "interface" || role == "implements" || role == "participantPackage" ||
        role == "structure" || role == "referencedArtifactElement" ||
        role == "referencedArtifact" || role == "origin" || role == "category" ||
        role == "element" || role == "expression") {
        return true;
    }
    // Group membership roles are references; package roles of the same name
    // are containment (checked before this in the dispatch order).
    if (role == "argumentElement") {
        return dynamic_cast<const model::ArgumentGroup*>(&element) != nullptr;
    }
    if (role == "artifactElement") {
        return dynamic_cast<const model::ArtifactGroup*>(&element) != nullptr;
    }
    if (role == "terminologyElement") {
        return dynamic_cast<const model::TerminologyGroup*>(&element) != nullptr;
    }
    return false;
}

void populate(Reader& reader, SACMElement& element, const pugi::xml_node& node) {
    reader.push_scope(node);

    read_common_attributes(reader, element, node);
    read_kind_specific_attributes(reader, element, node);
    read_reference_attributes(reader, element, node);
    if (auto* model_element = dynamic_cast<model::ModelElement*>(&element)) {
        read_model_element_children(reader, *model_element, node);
    }
    read_reference_children(reader, element, node);
    if (dynamic_cast<model::ArgumentationElement*>(&element) != nullptr &&
        dynamic_cast<model::ArgumentPackage*>(&element) == nullptr) {
        // Claim/reasoning tolerant content mapping (not for packages, whose
        // children are handled below).
        read_claim_content_tolerance(reader, static_cast<model::ModelElement&>(element), node);
    }

    // Containment children.
    for (const pugi::xml_node& child : node.children()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        const std::string_view role = normalize_role(local_name(child.name()));
        if (is_common_role(role) || is_reference_role(element, role)) {
            continue;
        }
        if (const std::optional<ElementKind> declared = containment_role_kind(element, role)) {
            // Concrete declared type; xsi:type may still refine to a subtype.
            reader.push_scope(child);
            const std::optional<std::string> xsi_type = read_xsi_type(reader, child);
            reader.pop_scope();
            std::optional<ElementKind> kind = declared;
            if (xsi_type.has_value()) {
                kind = detail::kind_from_class_name(*xsi_type);
                if (!kind.has_value()) {
                    kind = detail::kind_from_class_name_ci(*xsi_type);
                }
            }
            if (!kind.has_value()) {
                reader.report(validation::codes::kXmiUnknownElement, reader.mode_severity(),
                              "SACM23-XMI-001", child, {element.id()},
                              std::format("unknown xsi:type on role '{}'", role));
                continue;
            }
            std::unique_ptr<SACMElement> parsed = read_element(reader, *kind, child);
            if (parsed != nullptr && !attach_child(reader, element, std::move(parsed), child)) {
                reader.report(validation::codes::kXmiUnknownElement, reader.mode_severity(),
                              "SACM23-XMI-001", child, {element.id()},
                              std::format("a {} cannot be contained in role '{}'",
                                          metadata::kind_name(*kind), role));
            }
            continue;
        }
        if (is_abstract_containment_role(element, role)) {
            read_containment_child(reader, element, child, std::nullopt);
            continue;
        }
        // Tolerant fallback: element name is a class name (repo fixtures).
        if (const std::optional<ElementKind> kind = detail::kind_from_class_name_ci(role)) {
            if (reader.strict()) {
                reader.report(validation::codes::kXmiUnknownElement, Severity::Error,
                              "SACM23-XMI-001", child, {element.id()},
                              std::format("class-name element '{}' is not a strict containment "
                                          "role",
                                          role));
                continue;
            }
            std::unique_ptr<SACMElement> parsed = read_element(reader, *kind, child);
            if (parsed != nullptr && !attach_child(reader, element, std::move(parsed), child)) {
                reader.report(validation::codes::kXmiUnknownElement, Severity::Warning,
                              "SACM23-XMI-001", child, {element.id()},
                              std::format("a {} cannot be contained in a {}; skipped",
                                          metadata::kind_name(*kind),
                                          metadata::kind_name(element.kind())));
            }
            continue;
        }
        if (reader.strict()) {
            reader.report(validation::codes::kXmiUnknownElement, Severity::Error,
                          "SACM23-XMI-003", child, {element.id()},
                          std::format("unknown element '{}' under {}", child.name(),
                                      metadata::kind_name(element.kind())));
        } else {
            // Never silently dropped: preserved verbatim; compat save
            // re-emits it, strict save refuses (SACM-XMI-006).
            Access::preserved_content(element).push_back(node_to_string(child));
            reader.report(validation::codes::kXmiUnknownElement, Severity::Warning,
                          "SACM23-COMPAT-001", child, {element.id()},
                          std::format("unknown element '{}' under {} preserved as "
                                      "compatibility content",
                                      child.name(), metadata::kind_name(element.kind())));
        }
    }

    reader.pop_scope();
}

// Detects the SACM namespace of the root and reports unknown ones.
void check_root_namespace(Reader& reader, const pugi::xml_node& root) {
    const std::string_view root_prefix = prefix_of(root.name());
    const std::string uri = reader.resolve_prefix(root_prefix);
    reader.source_namespace = uri;
    reader.source_version = metadata::namespaces::detect_standard_version(uri);
    // A pre-2.3 document that happens to parse is not a 2.3 document. Say which
    // revision was detected, so a caller can decide rather than discovering it
    // later from a subtly wrong model.
    if (reader.source_version != metadata::namespaces::StandardVersion::V2_3 && !uri.empty()) {
        reader.report(
            validation::codes::kXmiOlderStandardVersion, Severity::Warning, "SACM23-COMPAT-001",
            root, {},
            std::format("document declares SACM {} (namespace '{}'); this library implements 2.3 "
                        "and loaded it in compatibility mode",
                        metadata::namespaces::standard_version_name(reader.source_version), uri));
    }
    if (uri.empty()) {
        if (reader.strict()) {
            reader.report(validation::codes::kXmiUnknownNamespace, Severity::Error,
                          "SACM23-XMI-001", root, {},
                          "root element has no namespace; strict SACM 2.3 requires the pinned "
                          "namespace");
        }
        return;
    }
    if (reader.strict() && !metadata::namespaces::is_strict_sacm_namespace(uri)) {
        reader.report(validation::codes::kXmiUnknownNamespace, Severity::Error, "SACM23-XMI-002",
                      root, {}, std::format("namespace '{}' is not the strict SACM 2.3 namespace",
                                            uri));
    } else if (!metadata::namespaces::is_accepted_sacm_namespace(uri)) {
        reader.report(validation::codes::kXmiUnknownNamespace, Severity::Warning, "SACM23-XMI-002",
                      root, {}, std::format("namespace '{}' is not a known SACM namespace", uri));
    }
}

// Reads one root element into the document.
void read_root(Reader& reader, model::Document& document, const pugi::xml_node& root) {
    reader.push_scope(root);
    std::optional<ElementKind> kind;
    if (const std::optional<std::string> xsi_type = read_xsi_type(reader, root)) {
        kind = detail::kind_from_class_name(*xsi_type);
    }
    if (!kind.has_value()) {
        kind = reader.strict() ? detail::kind_from_class_name(local_name(root.name()))
                               : detail::kind_from_class_name_ci(local_name(root.name()));
    }
    reader.pop_scope();

    const bool valid_root =
        kind.has_value() && metadata::is_package_kind(*kind);
    if (!valid_root) {
        reader.report(validation::codes::kXmiInvalidRoot, Severity::Error, "SACM23-XMI-001", root,
                      {},
                      std::format("'{}' is not a SACM interchange package root", root.name()));
        return;
    }

    std::unique_ptr<SACMElement> element = read_element(reader, *kind, root);
    if (element == nullptr) {
        return;
    }
    if (auto* acp = dynamic_cast<model::AssuranceCasePackage*>(element.get())) {
        (void)acp;
        Access::roots(document).push_back(std::unique_ptr<model::AssuranceCasePackage>(
            static_cast<model::AssuranceCasePackage*>(element.release())));
    } else {
        Access::other_roots(document).push_back(std::move(element));
    }
}

void build_index(Reader& reader, model::Document& document) {
    auto& index = Access::index(document);
    document.for_each_element([&](const SACMElement& element) {
        auto* mutable_element = const_cast<SACMElement*>(&element);
        const auto [it, inserted] = index.try_emplace(element.id(), mutable_element);
        if (!inserted) {
            reader.diagnostics.push_back(Diagnostic{
                .code = std::string(validation::codes::kIdDuplicate),
                .severity = Severity::Error,
                .requirement_id = "SACM23-XMI-003",
                .operation = "",
                .affected = {element.id()},
                .location = std::nullopt,
                .message = std::format("duplicate element id '{}'", element.id().value()),
            });
        }
    });
}

void check_references(Reader& reader, const model::Document& document) {
    document.for_each_element([&](const SACMElement& element) {
        model::traverse::for_each_reference(
            element, [&](const model::traverse::ReferenceUse& use) {
                if (document.find(*use.target) == nullptr) {
                    reader.diagnostics.push_back(Diagnostic{
                        .code = std::string(validation::codes::kRefDangling),
                        .severity = reader.mode_severity(),
                        .requirement_id = "SACM23-XMI-003",
                        .operation = "",
                        .affected = {element.id(), *use.target},
                        .location = std::nullopt,
                        .message = std::format("'{}' references ({}) missing element '{}'",
                                               element.id().value(), use.role,
                                               use.target->value()),
                    });
                }
            });
    });
}

LoadResult load_impl(std::string_view xml, std::string source_file, const LoadOptions& options) {
    LoadResult result;
    Reader reader;
    reader.mode = options.mode;
    reader.source_file = std::move(source_file);
    reader.buffer = xml.data();

    // SACM-SEC-001: reject DOCTYPE before parsing.
    if (xml.find("<!DOCTYPE") != std::string_view::npos ||
        xml.find("<!ENTITY") != std::string_view::npos) {
        result.diagnostics.push_back(Diagnostic{
            .code = std::string(validation::codes::kXmlDoctypeRejected),
            .severity = Severity::Error,
            .requirement_id = "SACM23-SEC-001",
            .operation = "",
            .affected = {},
            .location = std::nullopt,
            .message = "document contains a DOCTYPE/ENTITY declaration and was rejected",
        });
        return result;
    }

    pugi::xml_document parsed;
    const pugi::xml_parse_result parse_result =
        parsed.load_buffer(xml.data(), xml.size(), pugi::parse_default);
    if (!parse_result) {
        result.diagnostics.push_back(Diagnostic{
            .code = std::string(validation::codes::kXmlMalformed),
            .severity = Severity::Error,
            .requirement_id = "SACM23-VAL-001",
            .operation = "",
            .affected = {},
            .location = std::nullopt,
            .message = std::format("XML parse error: {}", parse_result.description()),
        });
        return result;
    }

    // EMF-produced files address elements by containment path instead of id;
    // rewrite those to ids so the reader sees an ordinary document. No-op for
    // files that do not use them.
    normalize_emf_references(parsed.document_element());

    const pugi::xml_node root = parsed.document_element();
    if (!root) {
        result.diagnostics.push_back(Diagnostic{
            .code = std::string(validation::codes::kXmiInvalidRoot),
            .severity = Severity::Error,
            .requirement_id = "SACM23-XMI-001",
            .operation = "",
            .affected = {},
            .location = std::nullopt,
            .message = "document has no root element",
        });
        return result;
    }

    model::Document document;
    reader.push_scope(root);
    if (local_name(root.name()) == "XMI") {
        for (const pugi::xml_node& child : root.children()) {
            if (child.type() == pugi::node_element) {
                reader.push_scope(child);
                check_root_namespace(reader, child);
                reader.pop_scope();
                read_root(reader, document, child);
            }
        }
    } else {
        check_root_namespace(reader, root);
        read_root(reader, document, root);
    }
    reader.pop_scope();

    if (document.roots().empty() && document.other_roots().empty()) {
        if (!validation::has_errors(reader.diagnostics)) {
            reader.diagnostics.push_back(Diagnostic{
                .code = std::string(validation::codes::kXmiInvalidRoot),
                .severity = Severity::Error,
                .requirement_id = "SACM23-XMI-001",
                .operation = "",
                .affected = {},
                .location = std::nullopt,
                .message = "no SACM interchange package found in the document",
            });
        }
    }

    build_index(reader, document);
    check_references(reader, document);

    result.diagnostics = std::move(reader.diagnostics);
    result.source_namespace = std::move(reader.source_namespace);
    result.source_version = reader.source_version;
    result.ok = !validation::has_errors(result.diagnostics);
    if (result.ok || !document.roots().empty() || !document.other_roots().empty()) {
        result.document = std::move(document);
    }
    return result;
}

}  // namespace

LoadResult load_xmi_string(std::string_view xml, const LoadOptions& options) {
    return load_impl(xml, "<string>", options);
}

LoadResult load_xmi_file(const std::filesystem::path& path, const LoadOptions& options) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        LoadResult result;
        result.diagnostics.push_back(Diagnostic{
            .code = std::string(validation::codes::kXmlMalformed),
            .severity = Severity::Error,
            .requirement_id = "SACM23-VAL-001",
            .operation = "",
            .affected = {},
            .location = std::nullopt,
            .message = std::format("cannot open file: {}", path.string()),
        });
        return result;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    const std::string content = buffer.str();
    return load_impl(content, path.string(), options);
}

}  // namespace sacm::io
