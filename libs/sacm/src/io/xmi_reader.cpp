#include "sacm/io/xmi.h"

#include "io/name_tables.h"
#include "model/access.h"
#include "model/traverse.h"

#include "sacm/metadata/namespaces.h"
#include "sacm/validation/codes.h"

#include <pugixml.hpp>

#include <format>
#include <fstream>
#include <map>
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

bool is_xml_name_char(char c) {
    const unsigned char value = static_cast<unsigned char>(c);
    return std::isalnum(value) != 0 || c == '_' || c == '-' || c == '.' || c == ':' || value >= 0x80;
}

std::string_view read_xml_name_at(std::string_view xml, std::size_t position) {
    const std::size_t begin = position;
    while (position < xml.size() && is_xml_name_char(xml[position])) {
        ++position;
    }
    return xml.substr(begin, position - begin);
}

// One past the '>' that closes the tag starting at `tag_start`, skipping any
// '>' inside a quoted attribute value.
std::size_t find_tag_end(std::string_view xml, std::size_t tag_start) {
    char quote = '\0';
    for (std::size_t i = tag_start; i < xml.size(); ++i) {
        const char c = xml[i];
        if (quote != '\0') {
            if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '>') {
            return i + 1;
        }
    }
    return xml.size();
}

void collect_declared_prefixes(std::string_view tag, std::unordered_set<std::string>& out) {
    std::size_t i = 0;
    while ((i = tag.find("xmlns:", i)) != std::string_view::npos) {
        const std::string_view name = read_xml_name_at(tag, i + 6);
        if (!name.empty()) {
            out.emplace(name);
        }
        i += 6 + std::max<std::size_t>(name.size(), 1);
    }
}

// What pugixml's own wording leaves out about a document it could not parse.
struct MalformedXmlDetail {
    // The innermost element still open where a closing tag disagreed with it,
    // spelled as the source spells it (prefix included).
    std::string open_tag;
    // The name on the closing tag that disagreed.
    std::string close_tag;
    // Prefixes used by those two tags that no xmlns declaration in the document
    // defines.
    std::vector<std::string> undeclared_prefixes;

    bool has_tag_mismatch() const {
        return !open_tag.empty() || !close_tag.empty();
    }
};

// Text-level diagnosis of a document that failed to parse.
//
// There is no DOM to inspect at this point, so this re-scans the source. That
// is worth doing because the population of files that reach here is mostly
// files the *user did not write* -- another tool's export -- where "Start-end
// tags mismatch" with no tag names and no position is not something anyone can
// act on (#285, carried out of #16).
//
// The scan walks tags until a closing tag disagrees with the innermost open
// one. It is deliberately independent of `parse_result.offset`, which is used
// only for the reported line/column: the offset's meaning varies by error
// status, while "the first tag that does not match" is the thing being
// described.
//
// Prefix scoping is not modelled: a prefix declared anywhere in the document
// counts as declared. That can only ever suppress a complaint, never invent
// one, which is the right direction for a diagnostic.
MalformedXmlDetail diagnose_malformed_xml(std::string_view xml) {
    MalformedXmlDetail detail;
    std::vector<std::string> open_tags;
    std::unordered_set<std::string> declared_prefixes{"xml", "xmlns"};

    std::size_t i = 0;
    while (i < xml.size()) {
        const std::size_t lt = xml.find('<', i);
        if (lt == std::string_view::npos) {
            break;
        }
        if (xml.compare(lt, 4, "<!--") == 0) {
            const std::size_t end = xml.find("-->", lt + 4);
            i = end == std::string_view::npos ? xml.size() : end + 3;
            continue;
        }
        if (xml.compare(lt, 9, "<![CDATA[") == 0) {
            const std::size_t end = xml.find("]]>", lt + 9);
            i = end == std::string_view::npos ? xml.size() : end + 3;
            continue;
        }
        if (lt + 1 < xml.size() && (xml[lt + 1] == '?' || xml[lt + 1] == '!')) {
            i = find_tag_end(xml, lt);
            continue;
        }
        if (lt + 1 < xml.size() && xml[lt + 1] == '/') {
            const std::string_view name = read_xml_name_at(xml, lt + 2);
            if (open_tags.empty()) {
                detail.close_tag = std::string(name);
                break;
            }
            if (open_tags.back() != name) {
                detail.open_tag = open_tags.back();
                detail.close_tag = std::string(name);
                break;
            }
            open_tags.pop_back();
            i = find_tag_end(xml, lt);
            continue;
        }
        const std::string_view name = read_xml_name_at(xml, lt + 1);
        const std::size_t end = find_tag_end(xml, lt);
        const std::string_view tag = xml.substr(lt, end - lt);
        collect_declared_prefixes(tag, declared_prefixes);
        // A self-closing tag never needs a partner.
        if (tag.size() >= 2 && tag[tag.size() - 2] == '/') {
            i = end;
            continue;
        }
        if (!name.empty()) {
            open_tags.emplace_back(name);
        }
        i = end;
    }

    for (const std::string& tag : {detail.open_tag, detail.close_tag}) {
        const std::string_view prefix = prefix_of(tag);
        if (prefix.empty() || declared_prefixes.contains(std::string(prefix))) {
            continue;
        }
        const std::string value(prefix);
        if (std::ranges::find(detail.undeclared_prefixes, value) == detail.undeclared_prefixes.end()) {
            detail.undeclared_prefixes.push_back(value);
        }
    }
    return detail;
}

// True when the writer declares `uri` itself, so recording it on the document
// would be redundant -- and, for a SACM namespace, actively wrong: re-declaring
// the source dialect's URI would fight the export-namespace override.
//
// A SACM *extension* namespace (GSN and friends) is deliberately not in this
// set, even though the reader understands it. An extension type whose SACM
// supertype is abstract has no concrete class to become, so its subtree is kept
// verbatim in preserved content and re-emitted with its original prefix -- a
// prefix the writer declares for no other reason. Treating the namespace as
// self-declared would emit that fragment under an undeclared prefix: output
// that is not namespace-well-formed, and whose attributes the next load drops
// because an undeclared prefix resolves to no namespace. That is exactly the
// loss fixed for vendor prefixes under SACM23-COMPAT-001, and it became
// reachable for extension prefixes once extension fragments started reaching
// preserved content at all (SACM23-COMPAT-002). The writer emits recorded
// declarations only when something is preserved, so recording a prefix the
// document never re-emits costs nothing.
bool is_self_declared_namespace(std::string_view uri) {
    return metadata::namespaces::is_xmi_namespace(uri) || uri == metadata::namespaces::kXsi ||
           metadata::namespaces::is_accepted_sacm_namespace(uri);
}

// Collects the document's foreign `xmlns:<prefix>` declarations as a union over
// the whole tree, not just the root: a fragment preserved verbatim may use a
// prefix declared on any ancestor, and the fragment itself is opaque text once
// captured. First declaration in document order wins, so a prefix rebound at
// different depths still yields a deterministic result.
void collect_foreign_namespaces(const pugi::xml_node& node, std::map<std::string, std::string>& out) {
    for (const pugi::xml_attribute& attr : node.attributes()) {
        const std::string_view name = attr.name();
        if (!name.starts_with("xmlns:")) {
            continue;
        }
        const std::string_view uri = attr.value();
        if (uri.empty() || is_self_declared_namespace(uri)) {
            continue;
        }
        out.emplace(std::string(name.substr(6)), std::string(uri));
    }
    for (const pugi::xml_node& child : node.children()) {
        if (child.type() == pugi::node_element) {
            collect_foreign_namespaces(child, out);
        }
    }
}

struct Reader {
    Mode mode = Mode::Tolerant;
    std::string source_file;
    const char* buffer = nullptr;
    std::vector<Diagnostic> diagnostics;
    // Innermost-first stack of xmlns scopes (prefix -> URI; "" = default ns).
    std::vector<std::unordered_map<std::string, std::string>> ns_scopes;
    std::string source_namespace;
    metadata::namespaces::StandardVersion source_version = metadata::namespaces::StandardVersion::Unknown;
    std::uint64_t generated_counter = 0;
    std::unordered_set<std::string> used_ids;
    // Ids found inside subtrees kept as preserved compatibility content. They
    // never enter the element index, so without this a reference to one is
    // indistinguishable from a reference to nothing (SACM23-COMPAT-002).
    std::unordered_set<ElementId> preserved_element_ids;

    bool strict() const {
        return mode == Mode::Strict;
    }

    std::optional<validation::SourceLocation> locate_offset(std::ptrdiff_t offset) const {
        if (buffer == nullptr || offset < 0) {
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

    std::optional<validation::SourceLocation> locate(const pugi::xml_node& node) const {
        return locate_offset(node.offset_debug());
    }

    void report(std::string_view code,
                Severity severity,
                std::string_view requirement,
                const pugi::xml_node& node,
                std::vector<ElementId> affected,
                std::string message) {
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
    Severity mode_severity() const {
        return strict() ? Severity::Error : Severity::Warning;
    }

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

    void pop_scope() {
        ns_scopes.pop_back();
    }

    std::string resolve_prefix(std::string_view prefix) const {
        for (auto it = ns_scopes.rbegin(); it != ns_scopes.rend(); ++it) {
            const auto found = it->find(std::string(prefix));
            if (found != it->end()) {
                return found->second;
            }
        }
        return {};
    }

    // `kind` is unused: generated ids are deliberately kind-independent, and
    // uniqueness comes from checking `used_ids` rather than from the name
    // carrying a type (SACM23-XMI-003). Kept in the signature so callers stay
    // explicit about what they are generating an id for.
    ElementId generate_id([[maybe_unused]] ElementKind kind) {
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
    return !value.empty() && !value.starts_with('#') && value.find('#') != std::string_view::npos;
}

bool parse_bool(std::string_view value) {
    return value == "true" || value == "1";
}

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

bool looks_like_emf_path(std::string_view value) {
    return value.starts_with("//@");
}

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
void assign_emf_path_ids(pugi::xml_node node,
                         const std::string& path,
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
void rewrite_emf_paths(pugi::xml_node node, const std::unordered_map<std::string, std::string>& path_to_id) {
    for (pugi::xml_attribute attr : node.attributes()) {
        const std::string value = attr.value();
        if (value.find("//@") == std::string::npos) {
            continue;
        }
        std::string rewritten;
        std::size_t start = 0;
        while (start <= value.size()) {
            const std::size_t end = value.find(' ', start);
            const std::string token = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
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

// What reading `xsi:type` established about the node. The three cases were once
// two -- a class name or `std::nullopt` -- which conflated "there is no xsi:type
// here, infer the kind from the element name" with "there is one, and this
// subtree has to survive verbatim". Every caller took the second for the first,
// so an extension type whose SACM supertype is abstract was reported as
// preserved and then dropped without a trace.
enum class XsiTypeOutcome {
    // No xsi:type attribute on the node; the caller falls back to the role's
    // declared type or (tolerant) the element name.
    Absent,
    // Resolved to a SACM class name, directly or through an extension type's
    // SACM supertype.
    Resolved,
    // A type this reader recognizes but cannot represent as any concrete SACM
    // class. The caller must keep the subtree verbatim (tolerant) or reject it
    // (strict); inferring a kind here would silently retype the element.
    PreserveAsCompatibility,
};

struct XsiTypeResult {
    XsiTypeOutcome outcome = XsiTypeOutcome::Absent;
    // The SACM class name when `Resolved`; the raw qualified xsi:type value
    // when `PreserveAsCompatibility`, so a caller can name it in a diagnostic.
    std::string type_name;

    bool resolved() const {
        return outcome == XsiTypeOutcome::Resolved;
    }
    bool preserve() const {
        return outcome == XsiTypeOutcome::PreserveAsCompatibility;
    }
};

// xsi:type="sacm:Claim" (or xmi:type) -> class name, validating the prefix
// namespace.
XsiTypeResult read_xsi_type(Reader& reader, const pugi::xml_node& node) {
    for (const pugi::xml_attribute& attr : node.attributes()) {
        const std::string_view name = attr.name();
        if (local_name(name) != "type" || name.starts_with("xmlns")) {
            continue;
        }
        const std::string_view attr_prefix = prefix_of(name);
        const std::string attr_ns = reader.resolve_prefix(attr_prefix);
        const bool is_xsi = attr_ns == metadata::namespaces::kXsi ||
                            (attr_ns.empty() && attr_prefix == metadata::namespaces::kXsiPrefix);
        // XMI 2.5.1 spells the type discriminator `xmi:type`, and OMG-toolchain
        // output uses it -- including the pinned normative metamodel file in
        // third_party/sacm-2.3 and MagicDraw-family exports. Accepting only the
        // XSI spelling left such an element to fall through to role/class-name
        // inference and be preserved-or-rejected rather than typed, which is the
        // opposite of what clause 2 asks of an importer (#336).
        const bool is_xmi = (!attr_ns.empty() && metadata::namespaces::is_xmi_namespace(attr_ns)) ||
                            (attr_ns.empty() && attr_prefix == metadata::namespaces::kXmiPrefix);
        if (!is_xsi && !is_xmi) {
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
                    // there is nothing concrete to become. The caller keeps the
                    // subtree rather than silently coercing it into an unrelated
                    // class; strict rejects the document, which is why this is
                    // an error there and a warning on the tolerant path.
                    reader.report(validation::codes::kXmiUnknownElement,
                                  reader.mode_severity(),
                                  "SACM23-COMPAT-002",
                                  node,
                                  {},
                                  std::format("xsi:type '{}' extends an abstract SACM class and has no "
                                              "concrete equivalent",
                                              value));
                    return XsiTypeResult{XsiTypeOutcome::PreserveAsCompatibility, std::string(value)};
                }
                reader.report(validation::codes::kXmiUnknownElement,
                              Severity::Info,
                              "SACM23-COMPAT-001",
                              node,
                              {},
                              std::format("xsi:type '{}' resolved to its SACM supertype '{}'",
                                          value,
                                          metadata::kind_name(*extension->sacm_kind)));
                return XsiTypeResult{XsiTypeOutcome::Resolved, std::string(metadata::kind_name(*extension->sacm_kind))};
            }
            // Distinguish "we know this metamodel but not this type" from
            // "we do not know this metamodel at all". Both end up preserved,
            // but only the first means our extension table is out of date, and
            // a reader cannot act on the difference if both say the same thing.
            if (detail::is_sacm_extension_namespace(type_ns)) {
                reader.report(validation::codes::kXmiUnknownElement,
                              reader.mode_severity(),
                              "SACM23-COMPAT-002",
                              node,
                              {},
                              std::format("xsi:type '{}' is an unrecognized type in the known SACM-extension "
                                          "namespace '{}'",
                                          value,
                                          type_ns));
                return XsiTypeResult{XsiTypeOutcome::PreserveAsCompatibility, std::string(value)};
            }
            reader.report(validation::codes::kXmiUnknownNamespace,
                          reader.mode_severity(),
                          "SACM23-XMI-002",
                          node,
                          {},
                          std::format("xsi:type '{}' resolves to non-SACM namespace '{}'", value, type_ns));
            if (reader.strict()) {
                return XsiTypeResult{};
            }
        }
        return XsiTypeResult{XsiTypeOutcome::Resolved, std::string(local_name(value))};
    }
    return XsiTypeResult{};
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
std::optional<ElementId> read_element_id(Reader& reader, const pugi::xml_node& node, ElementKind kind) {
    const pugi::xml_attribute attr = find_attr_local(node, "id");
    if (attr) {
        reader.used_ids.insert(attr.value());
        return ElementId{attr.value()};
    }
    if (reader.strict()) {
        reader.report(validation::codes::kXmiMissingId,
                      Severity::Error,
                      "SACM23-XMI-001",
                      node,
                      {},
                      std::format("{} element has no xmi:id", metadata::kind_name(kind)));
        return std::nullopt;
    }
    reader.report(validation::codes::kXmiMissingId,
                  Severity::Warning,
                  "SACM23-XMI-001",
                  node,
                  {},
                  std::format("{} element has no xmi:id; generated one", metadata::kind_name(kind)));
    return reader.generate_id(kind);
}

// Creates and populates a child element of `kind` from `node`.
std::unique_ptr<SACMElement> read_element(Reader& reader, ElementKind kind, const pugi::xml_node& node) {
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
        reader.report(validation::codes::kXmiUnknownElement,
                      Severity::Info,
                      "SACM23-XMI-001",
                      node,
                      {},
                      std::format("LangString xmi:id '{}' is not preserved: LangString is a "
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
    } else if (const pugi::xml_node child =
                   node.find_child([](const pugi::xml_node& n) { return local_name(n.name()) == "expression"; })) {
        const pugi::xml_attribute ref =
            find_attr_local(child, "href") ? find_attr_local(child, "href") : find_attr_local(child, "ref");
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
void read_multi_lang_values(Reader& reader, const pugi::xml_node& wrapper, model::MultiLangString& out) {
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
                reader.report(validation::codes::kXmiExternalReference,
                              Severity::Warning,
                              "SACM23-XMI-003",
                              node,
                              {},
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
bool attach_child(Reader& reader, SACMElement& parent, std::unique_ptr<SACMElement> child, const pugi::xml_node& node) {
    const ElementKind kind = child->kind();
    SACMElement* raw = child.get();

    const auto downcast = [&child]<typename T>() { return std::unique_ptr<T>(static_cast<T*>(child.release())); };

    if (auto* acp = dynamic_cast<model::AssuranceCasePackage*>(&parent)) {
        if (dynamic_cast<model::AssuranceCasePackage*>(raw) != nullptr) {
            Access::assurance_case_packages(*acp).push_back(downcast.operator()<model::AssuranceCasePackage>());
        } else if (dynamic_cast<model::ArgumentPackage*>(raw) != nullptr) {
            Access::argument_packages(*acp).push_back(downcast.operator()<model::ArgumentPackage>());
        } else if (dynamic_cast<model::ArtifactPackage*>(raw) != nullptr) {
            Access::artifact_packages(*acp).push_back(downcast.operator()<model::ArtifactPackage>());
        } else if (dynamic_cast<model::TerminologyPackage*>(raw) != nullptr) {
            Access::terminology_packages(*acp).push_back(downcast.operator()<model::TerminologyPackage>());
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
        Access::terminology_elements(*pkg).push_back(downcast.operator()<model::TerminologyElement>());
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

// Keeps a subtree the reader recognizes but cannot type verbatim on its parent,
// so a compatibility save re-emits it unchanged. Strict mode preserves nothing:
// read_xsi_type has already reported the type as an error there, and strict
// output carries no compatibility content (SACM23-XMI-004), so the load fails
// rather than the element being written back without it.
// Records every id inside a preserved subtree. Those elements never reach the
// index, so a relationship endpoint naming one would otherwise look dangling --
// reporting an intact argument as broken purely because we could not type one
// of its elements.
void collect_preserved_ids(Reader& reader, const pugi::xml_node& node) {
    for (const pugi::xml_attribute& attr : node.attributes()) {
        const std::string_view name = attr.name();
        if (local_name(name) != "id" || name.starts_with("xmlns")) {
            continue;
        }
        // Only the XMI serialization identity counts. Matching any attribute
        // whose local name happens to be "id" would let a vendor's own
        // `acme:id` downgrade a genuinely dangling SACM reference from
        // SACM-REF-001 to SACM-REF-003 -- masking a broken document on the
        // strength of an unrelated attribute that shares a spelling.
        //
        // `is_xmi_namespace` rather than a comparison against the pinned URI:
        // it also accepts the older `http://www.omg.org/XMI`, which is what the
        // EMF dialects in the interop corpus actually declare. Comparing
        // exactly made preserved-element identity depend on the prefix
        // happening to be spelled `xmi`, reinstating the false
        // "structurally broken" failure for any EMF file that spells it
        // otherwise.
        const std::string_view prefix = prefix_of(name);
        if (!prefix.empty()) {
            const std::string uri = reader.resolve_prefix(prefix);
            // An UNDECLARED `xmi:` prefix is accepted on its spelling -- that is
            // the conventional form and plenty of real files never declare it.
            // A DECLARED one must resolve to an XMI namespace, so a document
            // that rebinds `xmlns:xmi` to something else cannot confer
            // serialization identity on its own terms. Accepting the spelling
            // unconditionally would let such a document downgrade a genuinely
            // dangling reference to SACM-REF-003, which is the same masking the
            // prefix check exists to prevent.
            const bool is_xmi_identity = uri.empty() ? prefix == "xmi" : metadata::namespaces::is_xmi_namespace(uri);
            if (!is_xmi_identity) {
                continue;
            }
        }
        if (attr.value() != nullptr && *attr.value() != '\0') {
            reader.preserved_element_ids.insert(ElementId{attr.value()});
        }
        break;
    }
    for (const pugi::xml_node& child : node.children()) {
        if (child.type() == pugi::node_element) {
            collect_preserved_ids(reader, child);
        }
    }
}

void preserve_extension_subtree(Reader& reader,
                                SACMElement& parent,
                                const pugi::xml_node& node,
                                std::string_view type_name,
                                std::string_view role,
                                std::size_t role_index) {
    if (reader.strict()) {
        return;
    }
    Access::preserved_content(parent).push_back(
        model::PreservedFragment{.xml = node_to_string(node), .role = std::string(role), .index = role_index});
    collect_preserved_ids(reader, node);
    reader.report(validation::codes::kXmiUnknownElement,
                  Severity::Warning,
                  "SACM23-COMPAT-002",
                  node,
                  {parent.id()},
                  std::format("element '{}' with xsi:type '{}' preserved as compatibility content "
                              "under {}",
                              node.name(),
                              type_name,
                              metadata::kind_name(parent.kind())));
}

// Handles a containment child whose concrete kind must be resolved from
// xsi:type, the role's declared type, or (tolerant) the class name.
void read_containment_child(Reader& reader,
                            SACMElement& parent,
                            const pugi::xml_node& node,
                            std::optional<ElementKind> declared_kind,
                            std::string_view role,
                            std::size_t role_index) {
    std::optional<ElementKind> kind;
    // Resolve `xsi:type` under the CHILD's own namespace scope: a document may
    // declare the xsi or the extension prefix on this element rather than on an
    // ancestor, and without the scope `resolve_prefix` would return empty --
    // the type would look unqualified, miss the extension branch entirely, and
    // the subtree would be silently dropped or coerced (the very failure this
    // preservation path exists to prevent). The declared-role branch in
    // `populate` and `read_root` already scope their own reads the same way.
    reader.push_scope(node);
    const XsiTypeResult xsi_type = read_xsi_type(reader, node);
    reader.pop_scope();
    if (xsi_type.preserve()) {
        // Not a fall-through to name-based inference: the element name here is
        // the abstract role ("argumentElement"), which is no class name, so
        // inference would fail and the subtree would be dropped.
        preserve_extension_subtree(reader, parent, node, xsi_type.type_name, role, role_index);
        return;
    }
    if (xsi_type.resolved()) {
        kind = detail::kind_from_class_name(xsi_type.type_name);
        if (!kind.has_value()) {
            kind = detail::kind_from_class_name_ci(xsi_type.type_name);
        }
        if (!kind.has_value()) {
            // "Unknown" and "abstract" are different mistakes with different
            // fixes: an unknown type means the file names a class this reader
            // does not have, an abstract one means the file instantiated a
            // class the metamodel forbids instantiating. Saying "unknown" for
            // both sends the reader looking for a typo that is not there.
            const bool abstract_class = detail::is_abstract_sacm_class_name(xsi_type.type_name);
            reader.report(validation::codes::kXmiUnknownElement,
                          reader.mode_severity(),
                          "SACM23-XMI-001",
                          node,
                          {parent.id()},
                          abstract_class
                              ? std::format("type '{}' names an abstract SACM class, which cannot be instantiated",
                                            xsi_type.type_name)
                              : std::format("unknown xsi:type '{}'", xsi_type.type_name));
            return;
        }
    } else if (declared_kind.has_value()) {
        kind = declared_kind;
    } else {
        // Abstract role type without xsi:type: tolerant falls back to the
        // element's local name as a class name.
        kind = detail::kind_from_class_name_ci(local_name(node.name()));
        if (reader.strict()) {
            reader.report(validation::codes::kXmiMissingType,
                          Severity::Error,
                          "SACM23-XMI-001",
                          node,
                          {parent.id()},
                          std::format("containment role '{}' has an abstract type and needs "
                                      "xsi:type",
                                      local_name(node.name())));
            return;
        }
    }
    if (!kind.has_value()) {
        reader.report(validation::codes::kXmiUnknownElement,
                      reader.mode_severity(),
                      "SACM23-XMI-003",
                      node,
                      {parent.id()},
                      std::format("unknown element '{}'", node.name()));
        return;
    }
    std::unique_ptr<SACMElement> child = read_element(reader, *kind, node);
    if (child == nullptr) {
        return;
    }
    if (!attach_child(reader, parent, std::move(child), node)) {
        reader.report(validation::codes::kXmiUnknownElement,
                      reader.mode_severity(),
                      "SACM23-XMI-001",
                      node,
                      {parent.id()},
                      std::format("a {} cannot be contained in a {}",
                                  metadata::kind_name(*kind),
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

void read_model_element_children(Reader& reader, model::ModelElement& element, const pugi::xml_node& node) {
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
                    (local_name(entry.name()) == "content" || local_name(entry.name()) == "value")) {
                    values.values.push_back(read_lang_string(reader, entry));
                }
            }
            if (values.values.empty()) {
                Access::name(element) = read_lang_string(reader, child);
            } else {
                Access::name(element) = values.values.front();
                if (values.values.size() > 1) {
                    auto overflow = std::make_unique<model::TaggedValue>(reader.generate_id(ElementKind::TaggedValue));
                    Access::key(*overflow).set("", "sacm.import.name");
                    Access::content(*overflow).values.assign(values.values.begin() + 1, values.values.end());
                    Access::set_parent(*overflow, &element);
                    Access::tagged_values(element).push_back(std::move(overflow));
                    reader.report(validation::codes::kXmiUnknownElement,
                                  Severity::Info,
                                  "SACM23-BASE-001",
                                  child,
                                  {element.id()},
                                  "multi-language name mapped to TaggedValue 'sacm.import.name' "
                                  "(clause 8.6 allows one name LangString)");
                }
            }
        } else if (role == "location" && dynamic_cast<model::Resource*>(&element) != nullptr) {
            // Resource.location (clause 12.10): a MultiLangString composition,
            // and the only payload a Resource carries. Declared by the normative
            // text and absent from ptc/22-03-13; without this branch a
            // text-conformant file's <location> fell into preserved content,
            // which strict save then refused.
            read_multi_lang_values(reader, child, Access::location(static_cast<model::Resource&>(element)));
        } else if (role == "description") {
            Access::descriptions(element).push_back(
                read_utility<model::Description>(reader, ElementKind::Description, child));
            Access::set_parent(*Access::descriptions(element).back(), &element);
        } else if (role == "implementationConstraint") {
            Access::implementation_constraints(element).push_back(
                read_utility<model::ImplementationConstraint>(reader, ElementKind::ImplementationConstraint, child));
            Access::set_parent(*Access::implementation_constraints(element).back(), &element);
        } else if (role == "note") {
            Access::notes(element).push_back(read_utility<model::Note>(reader, ElementKind::Note, child));
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
        auto holder = std::make_unique<model::Description>(reader.generate_id(ElementKind::Description));
        Access::content(*holder).set("", description.value());
        Access::set_parent(*holder, &element);
        Access::descriptions(element).push_back(std::move(holder));
    }
}

// Claim-style content tolerance: <content lang>text</content> children or a
// content="" attribute directly on an argumentation element map to a
// Description (clause 8.9: Descriptions provide the content of a Claim).
void read_claim_content_tolerance(Reader& reader, model::ModelElement& element, const pugi::xml_node& node) {
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
    auto holder = std::make_unique<model::Description>(reader.generate_id(ElementKind::Description));
    Access::content(*holder) = std::move(collected);
    Access::set_parent(*holder, &element);
    // A legacy `content=`/`<content>` statement is the element's primary text
    // (clause 8.9: the Description provides the content of a Claim), so it goes
    // to the *front*. A `<description>` child already read is a secondary note
    // and stays after it, so description() returns the statement.
    std::vector<std::unique_ptr<model::Description>>& descriptions = Access::descriptions(element);
    descriptions.insert(descriptions.begin(), std::move(holder));
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
void capture_vendor_attributes(Reader& reader, SACMElement& element, const pugi::xml_node& node) {
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
                reader.report(validation::codes::kXmiUnknownElement,
                              Severity::Error,
                              "SACM23-XMI-003",
                              node,
                              {element.id()},
                              std::format("unknown attribute '{}' on {}", name, metadata::kind_name(element.kind())));
                continue;
            }
            Access::preserved_attributes(element).push_back(std::format(R"({}="{}")", name, attr.value()));
            reader.report(validation::codes::kXmiUnknownElement,
                          Severity::Warning,
                          "SACM23-COMPAT-001",
                          node,
                          {element.id()},
                          std::format("unknown attribute '{}' on {} preserved as compatibility "
                                      "content",
                                      name,
                                      metadata::kind_name(element.kind())));
            continue;
        }
        const std::string attr_ns = reader.resolve_prefix(attr_prefix);
        if (attr_ns.empty() || metadata::namespaces::is_xmi_namespace(attr_ns) ||
            attr_ns == metadata::namespaces::kXsi || metadata::namespaces::is_accepted_sacm_namespace(attr_ns) ||
            detail::is_sacm_extension_namespace(attr_ns)) {
            continue;
        }
        if (reader.strict()) {
            reader.report(validation::codes::kXmiUnknownElement,
                          Severity::Error,
                          "SACM23-XMI-003",
                          node,
                          {element.id()},
                          std::format("unknown attribute '{}' from foreign namespace '{}'", name, attr_ns));
            continue;
        }
        Access::preserved_attributes(element).push_back(std::format(R"({}="{}")", name, attr.value()));
        reader.report(validation::codes::kXmiUnknownElement,
                      Severity::Warning,
                      "SACM23-COMPAT-001",
                      node,
                      {element.id()},
                      std::format("attribute '{}' from foreign namespace '{}' preserved as "
                                  "compatibility content",
                                  name,
                                  attr_ns));
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
            reader.report(validation::codes::kXmiUnknownElement,
                          Severity::Info,
                          "SACM23-COMPAT-002",
                          node,
                          {element.id()},
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

// An extension type with its namespace owned. `ExtensionType::namespace_uri`
// aliases the argument given to `resolve_extension_type`, which here is a
// temporary from prefix resolution, so it must be copied out before use.
// `type_name` and `assertion_declaration` point into the static table and are
// safe to keep as views.
struct ExtensionOrigin {
    std::string namespace_uri;
    std::string_view type_name;
    std::optional<ElementKind> sacm_kind;
    std::string_view assertion_declaration;
};

// Resolves the node's xsi:type back to the extension type it named, if any.
// `populate` has already pushed this node's scope, so prefixes resolve here.
std::optional<ExtensionOrigin> extension_type_of(Reader& reader, const pugi::xml_node& node) {
    for (const pugi::xml_attribute& attr : node.attributes()) {
        if (local_name(attr.name()) != "type") {
            continue;
        }
        const std::string_view value = attr.value();
        const std::string namespace_uri = reader.resolve_prefix(prefix_of(value));
        const std::optional<detail::ExtensionType> extension =
            detail::resolve_extension_type(namespace_uri, local_name(value));
        if (!extension.has_value()) {
            return std::nullopt;
        }
        return ExtensionOrigin{
            namespace_uri, extension->type_name, extension->sacm_kind, extension->assertion_declaration};
    }
    return std::nullopt;
}

// GSN types resolve to their SACM supertypes, and that resolution is lossy:
// Goal, Assumption and Justification all land on `Claim`. Two things stop the
// distinction from disappearing:
//
//   1. The original GSN type is recorded in a reserved TaggedValue (clause 8.12
//      extension mechanism), qualified with its namespace so the value is
//      unambiguous when a document mixes GSN revisions.
//   2. The AssertionDeclaration the GSN transformation defines is applied --
//      but only when the source did not state one. An explicit declaration in
//      the file is more specific than one inferred from the type and must win,
//      the same rule the `undeveloped` shorthand already follows.
//
// Before this, a GSN file round-tripped into indistinguishable Claims and the
// argument's assumptions and justifications became plain goals (SACM23-COMPAT-002).
void record_extension_origin(Reader& reader, SACMElement& element, const pugi::xml_node& node) {
    const std::optional<ExtensionOrigin> extension = extension_type_of(reader, node);
    if (!extension.has_value() || !extension->sacm_kind.has_value()) {
        return;
    }

    auto* model_element = dynamic_cast<model::ModelElement*>(&element);
    if (model_element == nullptr) {
        return;
    }

    auto origin = std::make_unique<model::TaggedValue>(reader.generate_id(ElementKind::TaggedValue));
    Access::key(*origin).set("", std::string(detail::kImportExtensionTypeKey));
    Access::content(*origin).set("", std::format("{{{}}}{}", extension->namespace_uri, extension->type_name));
    Access::set_parent(*origin, &element);
    Access::tagged_values(*model_element).push_back(std::move(origin));

    if (extension->assertion_declaration.empty()) {
        return;
    }
    auto* assertion = dynamic_cast<model::Assertion*>(&element);
    if (assertion == nullptr) {
        return;
    }
    // An assertionDeclaration written in the file has already been read; only
    // fill in the default.
    if (node.attribute("assertionDeclaration")) {
        return;
    }
    const std::optional<model::AssertionDeclaration> declaration =
        model::parse_assertion_declaration(std::string(extension->assertion_declaration));
    if (!declaration.has_value()) {
        return;
    }
    Access::assertion_declaration(*assertion) = *declaration;
    reader.report(validation::codes::kXmiUnknownElement,
                  Severity::Info,
                  "SACM23-COMPAT-002",
                  node,
                  {element.id()},
                  std::format("GSN '{}' resolved to Claim with assertionDeclaration='{}' (original "
                              "type preserved in TaggedValue '{}')",
                              extension->type_name,
                              extension->assertion_declaration,
                              detail::kImportExtensionTypeKey));
}

void read_kind_specific_attributes(Reader& reader, SACMElement& element, const pugi::xml_node& node) {
    if (auto* assertion = dynamic_cast<model::Assertion*>(&element)) {
        if (const pugi::xml_attribute attr = node.attribute("assertionDeclaration")) {
            if (const std::optional<model::AssertionDeclaration> parsed =
                    model::parse_assertion_declaration(attr.value())) {
                Access::assertion_declaration(*assertion) = *parsed;
            } else if (std::string_view(attr.value()) == "justification") {
                // Legacy Assurance Forge / GSN shorthand: `justification` is not a
                // SACM AssertionDeclaration literal. GSN's own mapping makes a
                // Justification an `axiomatic` assertion (docs/sacm/sacm-gsn-mapping.md);
                // normalize to that and preserve the original GSN role in a
                // reserved TaggedValue (clause 8.12) so a client can tell a
                // Justification from a plain axiomatic Goal. The key is a library
                // import convention (mirrors "sacm.import.name"); the Assurance
                // Forge adapter reads it (sacm_adapter::kImportAssertionDeclarationKey).
                Access::assertion_declaration(*assertion) = model::AssertionDeclaration::Axiomatic;
                auto role = std::make_unique<model::TaggedValue>(reader.generate_id(ElementKind::TaggedValue));
                Access::key(*role).set("", "sacm.import.assertionDeclaration");
                Access::content(*role).set("", "justification");
                Access::set_parent(*role, &element);
                Access::tagged_values(*assertion).push_back(std::move(role));
                reader.report(validation::codes::kXmiUnknownElement,
                              Severity::Info,
                              "SACM23-COMPAT-001",
                              node,
                              {element.id()},
                              "legacy assertionDeclaration=\"justification\" normalized to "
                              "axiomatic (GSN role preserved in TaggedValue "
                              "'sacm.import.assertionDeclaration')");
            } else {
                reader.report(validation::codes::kEnumInvalidLiteral,
                              reader.mode_severity(),
                              "SACM23-ARG-001",
                              node,
                              {element.id()},
                              std::format("invalid assertionDeclaration literal '{}'", attr.value()));
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
            reader.report(validation::codes::kXmiUnknownElement,
                          Severity::Info,
                          "SACM23-COMPAT-001",
                          node,
                          {element.id()},
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
std::optional<ElementKind> containment_role_kind(const SACMElement& parent, std::string_view role) {
    if (dynamic_cast<const model::AssuranceCasePackage*>(&parent) != nullptr) {
        if (role == "assuranceCasePackage")
            return ElementKind::AssuranceCasePackage;
        if (role == "argumentPackage")
            return ElementKind::ArgumentPackage;
        if (role == "artifactPackage")
            return ElementKind::ArtifactPackage;
        if (role == "terminologyPackage")
            return ElementKind::TerminologyPackage;
    }
    if (dynamic_cast<const model::ArtifactAsset*>(&parent) != nullptr && role == "property") {
        return ElementKind::Property;
    }
    return std::nullopt;
}

bool is_abstract_containment_role(const SACMElement& parent, std::string_view role) {
    if (role == "argumentElement" && dynamic_cast<const model::ArgumentPackage*>(&parent) != nullptr) {
        return true;
    }
    if (role == "artifactElement" && dynamic_cast<const model::ArtifactPackage*>(&parent) != nullptr) {
        return true;
    }
    if (role == "terminologyElement" && dynamic_cast<const model::TerminologyPackage*>(&parent) != nullptr) {
        return true;
    }
    return false;
}

constexpr std::string_view kCommonRoles[] = {
    "name",
    "description",
    "implementationConstraint",
    "note",
    "taggedValue",
    "content",
    "statement",
    "citedElement",
    "abstractForm",
};

bool is_common_role(std::string_view role) {
    for (const std::string_view known : kCommonRoles) {
        if (role == known) {
            return true;
        }
    }
    return false;
}

// Roles `read_model_element_children` already consumed for THIS element's class,
// so the containment pass below must not see them as unknown children. Kept
// separate from `kCommonRoles`, which is class-independent: `location` is a
// child of Resource (clause 12.10) and nothing else, and a `<location>` under
// any other class is genuinely unknown and should still be reported.
bool is_class_specific_model_child(const SACMElement& element, std::string_view role) {
    return role == "location" && dynamic_cast<const model::Resource*>(&element) != nullptr;
}

// Legacy Assurance Forge terminology shorthand: a TerminologyPackage/Group's
// contained elements are written with their concrete class name as the element
// name -- <expression id=.. value=..>, <term>, <category> -- instead of the
// canonical <terminologyElement xsi:type="sacm:Expression">. The same names are
// also genuine *reference attributes* elsewhere (a Term's origin, a Category's
// category), but those are read as attributes; as child elements of a
// terminology container they are contained elements. Recognizing them is what
// keeps a whole TerminologyPackage from being silently dropped.
bool is_terminology_shorthand_child(const SACMElement& element, std::string_view role) {
    if (dynamic_cast<const model::TerminologyPackage*>(&element) == nullptr &&
        dynamic_cast<const model::TerminologyGroup*>(&element) == nullptr) {
        return false;
    }
    return role == "expression" || role == "term" || role == "category";
}

bool is_reference_role(const SACMElement& element, std::string_view role) {
    // A terminology container's concrete-named children are contained elements,
    // not references, even though some of the same names are reference ends on
    // other element kinds.
    if (is_terminology_shorthand_child(element, role)) {
        return false;
    }
    if (role == "source" || role == "target" || role == "reasoning" || role == "metaClaim" || role == "interface" ||
        role == "implements" || role == "participantPackage" || role == "structure" ||
        role == "referencedArtifactElement" || role == "referencedArtifact" || role == "origin" || role == "category" ||
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
    // Must follow read_kind_specific_attributes: it decides whether the source
    // set an assertionDeclaration of its own, which this must not override.
    record_extension_origin(reader, element, node);
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
    //
    // Counted per role as the source spells them, because a preserved fragment
    // has to be re-emitted in the slot it occupied: the EMF dialect addresses
    // siblings by position, so appending it instead would renumber everything
    // after it. Typed and preserved children share one counter -- the writer
    // re-interleaves them into a single sequence.
    std::unordered_map<std::string, std::size_t> next_role_index;
    for (const pugi::xml_node& child : node.children()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        const std::string_view role = normalize_role(local_name(child.name()));
        if (is_common_role(role) || is_class_specific_model_child(element, role) || is_reference_role(element, role)) {
            continue;
        }
        const std::size_t role_index = next_role_index[std::string(role)]++;
        if (const std::optional<ElementKind> declared = containment_role_kind(element, role)) {
            // Concrete declared type; xsi:type may still refine to a subtype.
            reader.push_scope(child);
            const XsiTypeResult xsi_type = read_xsi_type(reader, child);
            reader.pop_scope();
            if (xsi_type.preserve()) {
                // The role's declared kind is not a safe default here: the
                // xsi:type says the element is something the declared class
                // cannot represent, so falling back to it would silently coerce
                // the element into an unrelated class.
                preserve_extension_subtree(reader, element, child, xsi_type.type_name, role, role_index);
                continue;
            }
            std::optional<ElementKind> kind = declared;
            if (xsi_type.resolved()) {
                kind = detail::kind_from_class_name(xsi_type.type_name);
                if (!kind.has_value()) {
                    kind = detail::kind_from_class_name_ci(xsi_type.type_name);
                }
            }
            if (!kind.has_value()) {
                reader.report(validation::codes::kXmiUnknownElement,
                              reader.mode_severity(),
                              "SACM23-XMI-001",
                              child,
                              {element.id()},
                              std::format("unknown xsi:type on role '{}'", role));
                continue;
            }
            std::unique_ptr<SACMElement> parsed = read_element(reader, *kind, child);
            if (parsed != nullptr && !attach_child(reader, element, std::move(parsed), child)) {
                reader.report(validation::codes::kXmiUnknownElement,
                              reader.mode_severity(),
                              "SACM23-XMI-001",
                              child,
                              {element.id()},
                              std::format("a {} cannot be contained in role '{}'", metadata::kind_name(*kind), role));
            }
            continue;
        }
        if (is_abstract_containment_role(element, role)) {
            read_containment_child(reader, element, child, std::nullopt, role, role_index);
            continue;
        }
        // Tolerant fallback: element name is a class name (repo fixtures).
        if (const std::optional<ElementKind> kind = detail::kind_from_class_name_ci(role)) {
            if (reader.strict()) {
                reader.report(validation::codes::kXmiUnknownElement,
                              Severity::Error,
                              "SACM23-XMI-001",
                              child,
                              {element.id()},
                              std::format("class-name element '{}' is not a strict containment "
                                          "role",
                                          role));
                continue;
            }
            std::unique_ptr<SACMElement> parsed = read_element(reader, *kind, child);
            if (parsed != nullptr && !attach_child(reader, element, std::move(parsed), child)) {
                reader.report(validation::codes::kXmiUnknownElement,
                              Severity::Warning,
                              "SACM23-XMI-001",
                              child,
                              {element.id()},
                              std::format("a {} cannot be contained in a {}; skipped",
                                          metadata::kind_name(*kind),
                                          metadata::kind_name(element.kind())));
            }
            continue;
        }
        if (reader.strict()) {
            reader.report(
                validation::codes::kXmiUnknownElement,
                Severity::Error,
                "SACM23-XMI-003",
                child,
                {element.id()},
                std::format("unknown element '{}' under {}", child.name(), metadata::kind_name(element.kind())));
        } else {
            // Never silently dropped: preserved verbatim; compat save
            // re-emits it, strict save refuses (SACM-XMI-006).
            //
            // No role is recorded: the element's name is not a containment role
            // the writer emits, so there is no sibling sequence to hold a slot
            // in and the fragment can only be appended.
            Access::preserved_content(element).push_back(
                model::PreservedFragment{.xml = node_to_string(child), .role = {}, .index = 0});
            collect_preserved_ids(reader, child);
            reader.report(validation::codes::kXmiUnknownElement,
                          Severity::Warning,
                          "SACM23-COMPAT-001",
                          child,
                          {element.id()},
                          std::format("unknown element '{}' under {} preserved as "
                                      "compatibility content",
                                      child.name(),
                                      metadata::kind_name(element.kind())));
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
        reader.report(validation::codes::kXmiOlderStandardVersion,
                      Severity::Warning,
                      "SACM23-COMPAT-001",
                      root,
                      {},
                      std::format("document declares SACM {} (namespace '{}'); this library implements 2.3 "
                                  "and loaded it in compatibility mode",
                                  metadata::namespaces::standard_version_name(reader.source_version),
                                  uri));
    }
    if (uri.empty()) {
        if (reader.strict()) {
            reader.report(validation::codes::kXmiUnknownNamespace,
                          Severity::Error,
                          "SACM23-XMI-001",
                          root,
                          {},
                          "root element has no namespace; strict SACM 2.3 requires the pinned "
                          "namespace");
        }
        return;
    }
    if (reader.strict() && !metadata::namespaces::is_strict_sacm_namespace(uri)) {
        reader.report(validation::codes::kXmiUnknownNamespace,
                      Severity::Error,
                      "SACM23-XMI-002",
                      root,
                      {},
                      std::format("namespace '{}' is not the strict SACM 2.3 namespace", uri));
    } else if (!metadata::namespaces::is_accepted_sacm_namespace(uri)) {
        reader.report(validation::codes::kXmiUnknownNamespace,
                      Severity::Warning,
                      "SACM23-XMI-002",
                      root,
                      {},
                      std::format("namespace '{}' is not a known SACM namespace", uri));
    }
}

// The SACM interchange-package kind a node denotes, if any. Tries xsi:type, the
// element's own name, and the singular of a plural containment role -- real
// containers spell the role `assuranceCasePackages` for a list of them.
std::optional<ElementKind> interchange_package_kind(Reader& reader, const pugi::xml_node& node) {
    const auto package_kind = [](std::optional<ElementKind> kind) -> std::optional<ElementKind> {
        if (kind.has_value() && metadata::is_package_kind(*kind)) {
            return kind;
        }
        return std::nullopt;
    };

    reader.push_scope(node);
    const XsiTypeResult xsi_type = read_xsi_type(reader, node);
    reader.pop_scope();
    if (xsi_type.resolved()) {
        if (const std::optional<ElementKind> kind = package_kind(detail::kind_from_class_name_ci(xsi_type.type_name))) {
            return kind;
        }
    }

    const std::string_view name = local_name(node.name());
    if (const std::optional<ElementKind> kind = package_kind(detail::kind_from_class_name_ci(name))) {
        return kind;
    }
    if (name.size() > 1 && name.back() == 's') {
        return package_kind(detail::kind_from_class_name_ci(name.substr(0, name.size() - 1)));
    }
    return std::nullopt;
}

// Collects the OUTERMOST SACM interchange packages inside a foreign container,
// without descending into one once found (its contents are the reader's job).
void collect_embedded_packages(Reader& reader, const pugi::xml_node& node, std::vector<pugi::xml_node>& out) {
    for (const pugi::xml_node& child : node.children()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        if (interchange_package_kind(reader, child).has_value()) {
            out.push_back(child);
            continue;
        }
        collect_embedded_packages(reader, child, out);
    }
}

// Reads one root element into the document.
void read_root(Reader& reader, model::Document& document, const pugi::xml_node& root) {
    reader.push_scope(root);
    std::optional<ElementKind> kind;
    const XsiTypeResult xsi_type = read_xsi_type(reader, root);
    if (xsi_type.resolved()) {
        kind = detail::kind_from_class_name(xsi_type.type_name);
    } else if (xsi_type.preserve()) {
        // A document root has no parent element to carry preserved content, so
        // the subtree cannot be kept the way a child's can. Say that plainly
        // instead of leaving the type diagnostic to imply a preservation that
        // did not happen: the root is read from its element name as before, and
        // if that is not a package kind the invalid-root error below stands.
        reader.report(validation::codes::kXmiUnknownElement,
                      reader.mode_severity(),
                      "SACM23-COMPAT-002",
                      root,
                      {},
                      std::format("root element's xsi:type '{}' has no concrete SACM equivalent "
                                  "and a document root cannot be preserved as compatibility "
                                  "content; reading the root from its element name '{}'",
                                  xsi_type.type_name,
                                  local_name(root.name())));
    }
    if (!kind.has_value()) {
        const std::string_view name = local_name(root.name());
        kind = reader.strict() ? detail::kind_from_class_name(name) : detail::kind_from_class_name_ci(name);
        // A multi-valued containment role is spelled plural in real containers:
        // an ODE DDIPackage holds its assurance case under `assuranceCasePackages`.
        // Without this the reader descends past that element into the argument
        // package inside it, silently dropping the AssuranceCasePackage level.
        if (!kind.has_value() && !reader.strict() && name.size() > 1 && name.back() == 's') {
            kind = detail::kind_from_class_name_ci(name.substr(0, name.size() - 1));
        }
    }
    reader.pop_scope();

    const bool valid_root = kind.has_value() && metadata::is_package_kind(*kind);
    if (!valid_root) {
        // Real toolchains ship SACM embedded in a larger container -- an ODE
        // DDIPackage carrying architecture and failure-logic models alongside
        // the assurance case. Strict refuses such a file, but refusing it on the
        // tolerant path too would mean a user with a real vendor file simply
        // cannot open their own assurance case.
        //
        // So read the SACM out of it and be explicit about what that costs: the
        // file is not a conformant SACM interchange document, the container's
        // non-SACM content is not represented in the model, and a save produces
        // conformant SACM rather than the original container. Silence here would
        // be the worst option -- the user would see their argument load and have
        // no reason to expect the rest of the file to disappear on save.
        if (!reader.strict()) {
            std::vector<pugi::xml_node> embedded;
            collect_embedded_packages(reader, root, embedded);
            if (!embedded.empty()) {
                reader.report(validation::codes::kXmiForeignContainerRoot,
                              Severity::Warning,
                              "SACM23-COMPAT-002",
                              root,
                              {},
                              std::format("'{}' is not a SACM interchange package root; {} SACM package(s) were read "
                                          "from inside it. This file does not conform to SACM 2.3 as an interchange "
                                          "document: content outside the SACM packages is NOT represented in the "
                                          "model, and saving will write a conformant SACM document rather than the "
                                          "original container",
                                          root.name(),
                                          embedded.size()));
                for (const pugi::xml_node& package : embedded) {
                    reader.push_scope(package);
                    read_root(reader, document, package);
                    reader.pop_scope();
                }
                return;
            }
        }
        reader.report(validation::codes::kXmiInvalidRoot,
                      Severity::Error,
                      "SACM23-XMI-001",
                      root,
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
        Access::roots(document).push_back(
            std::unique_ptr<model::AssuranceCasePackage>(static_cast<model::AssuranceCasePackage*>(element.release())));
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
        model::traverse::for_each_reference(element, [&](const model::traverse::ReferenceUse& use) {
            if (document.find(*use.target) != nullptr) {
                return;
            }
            // An endpoint naming an element we preserved verbatim is not a
            // broken reference: the target is in the file, we just could not
            // type it. Saying "missing" there would report an intact
            // argument as malformed, and at Error severity it would fail
            // every GSN document containing so much as a Context.
            if (document.has_preserved_element(*use.target)) {
                reader.diagnostics.push_back(Diagnostic{
                    .code = std::string(validation::codes::kRefPreservedTarget),
                    .severity = Severity::Warning,
                    .requirement_id = "SACM23-COMPAT-002",
                    .operation = "",
                    .affected = {element.id(), *use.target},
                    .location = std::nullopt,
                    .message = std::format("'{}' references ({}) '{}', which was preserved as compatibility "
                                           "content and therefore cannot be type-checked",
                                           element.id().value(),
                                           use.role,
                                           use.target->value()),
                });
                return;
            }
            reader.diagnostics.push_back(Diagnostic{
                .code = std::string(validation::codes::kRefDangling),
                .severity = reader.mode_severity(),
                .requirement_id = "SACM23-XMI-003",
                .operation = "",
                .affected = {element.id(), *use.target},
                .location = std::nullopt,
                .message = std::format(
                    "'{}' references ({}) missing element '{}'", element.id().value(), use.role, use.target->value()),
            });
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
    if (xml.find("<!DOCTYPE") != std::string_view::npos || xml.find("<!ENTITY") != std::string_view::npos) {
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
    const pugi::xml_parse_result parse_result = parsed.load_buffer(xml.data(), xml.size(), pugi::parse_default);
    if (!parse_result) {
        // pugixml names the failure ("Start-end tags mismatch") but not where it
        // is, which tag pair caused it, or that an undeclared prefix is what
        // made the two spellings differ. All three are recoverable from the
        // source, and all three are what a user needs to fix a file another
        // tool produced (#285).
        std::string message = std::format("XML parse error: {}", parse_result.description());
        const MalformedXmlDetail detail = diagnose_malformed_xml(xml);
        if (detail.has_tag_mismatch()) {
            if (detail.open_tag.empty()) {
                message += std::format(" -- '</{}>' closes an element that was never opened", detail.close_tag);
            } else {
                message += std::format(" -- '<{}>' is closed by '</{}>'", detail.open_tag, detail.close_tag);
            }
        }
        for (const std::string& prefix : detail.undeclared_prefixes) {
            message += std::format("; namespace prefix '{}' is not declared", prefix);
        }
        result.diagnostics.push_back(Diagnostic{
            .code = std::string(validation::codes::kXmlMalformed),
            .severity = Severity::Error,
            .requirement_id = "SACM23-VAL-001",
            .operation = "",
            .affected = {},
            .location = reader.locate_offset(static_cast<std::ptrdiff_t>(parse_result.offset)),
            .message = std::move(message),
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

    // Pre-reserve every explicit xmi:id in the document before any parsing, so
    // generate_id() never mints an id that already appears elsewhere -- even
    // later in document order. Generated ids can be persisted (e.g. on a
    // TaggedValue that had no id when first read) and reappear on a re-load;
    // without this pre-pass, minting "generated_49" for an id-less element
    // encountered earlier would collide with a later element already carrying
    // "generated_49", producing a spurious SACM-ID-001 duplicate on round-trip.
    collect_existing_ids(root, reader.used_ids);

    model::Document document;
    // Recorded before parsing: preserved fragments keep their prefixes, so the
    // declarations that give those prefixes meaning have to outlive the DOM.
    collect_foreign_namespaces(root, Access::foreign_namespaces(document));
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
    // Must precede check_references: it is what lets a reference into preserved
    // content be told apart from a reference to nothing.
    Access::preserved_element_ids(document) = reader.preserved_element_ids;
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

} // namespace

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

} // namespace sacm::io
