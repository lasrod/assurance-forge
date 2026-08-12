#include "export/gsn_projection.h"

#include "core/string_utils.h"
#include "core/element_factory.h"
#include "core/terminology_package_service.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace export_gsn {
namespace {

bool IsRelationshipType(const std::string& type) {
    return type == "assertedinference" || type == "assertedcontext" || type == "assertedevidence";
}

bool IsSupportedElementType(const std::string& type) {
    return type == "claim" || type == "argumentreasoning" || type == "artifact" || type == "artifactreference" ||
           type == "expression";
}

bool IsArtifactElementType(const std::string& type) {
    return type == "artifact" || type == "artifactreference" || type == "expression";
}

bool IsVisibleTerminologyContextElement(const parser::SacmElement& element) {
    return element.type == "assertedcontext" &&
           core::TrimWhitespace(element.description) == core::kVisibleTerminologyContextMarker;
}

bool ReferencesElement(const std::unordered_set<std::string>& refs, const parser::SacmElement& element) {
    const std::string id_ref = core::NormalizeRef(element.id);
    const std::string gid_ref = core::NormalizeRef(element.gid);
    return (!id_ref.empty() && refs.count(id_ref) > 0) || (!gid_ref.empty() && refs.count(gid_ref) > 0);
}

void AddElementReference(std::unordered_map<std::string, const parser::SacmElement*>& elements_by_ref,
                         const std::string& ref,
                         const parser::SacmElement& element) {
    const std::string key = core::NormalizeRef(ref);
    if (!key.empty() && elements_by_ref.find(key) == elements_by_ref.end())
        elements_by_ref[key] = &element;
}

bool IsContextRelationshipTarget(const parser::SacmElement& element,
                                 const std::unordered_set<std::string>& context_attachment_refs) {
    if (ReferencesElement(context_attachment_refs, element))
        return false;
    if (element.type == "argumentreasoning")
        return true;
    return element.type == "claim" && element.assertion_declaration != "assumed" &&
           element.assertion_declaration != "justification";
}

bool HasContextRelationshipTarget(const parser::SacmElement& relationship,
                                  const std::unordered_map<std::string, const parser::SacmElement*>& elements_by_ref,
                                  const std::unordered_set<std::string>& context_attachment_refs) {
    for (const std::string& target_ref : relationship.target_refs) {
        auto target_it = elements_by_ref.find(core::NormalizeRef(target_ref));
        if (target_it != elements_by_ref.end() && target_it->second &&
            IsContextRelationshipTarget(*target_it->second, context_attachment_refs))
            return true;
    }
    return false;
}

std::string DisplaySourceId(const parser::SacmElement& element) {
    if (!element.id.empty())
        return element.id;
    if (!element.gid.empty())
        return element.gid;
    if (!element.name.empty())
        return element.name;
    return {};
}

std::string MakeFallbackId(size_t index) {
    std::ostringstream out;
    out << "node_" << std::setw(3) << std::setfill('0') << index;
    return out.str();
}

std::string MakeSafeSvgId(const std::string& value) {
    std::string safe;
    safe.reserve(value.size() + 4);
    for (char ch : value) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-') {
            safe.push_back(ch);
        } else {
            safe.push_back('_');
        }
    }
    if (safe.empty())
        safe = "node";
    unsigned char first = static_cast<unsigned char>(safe.front());
    if (!std::isalpha(first) && safe.front() != '_')
        safe = "gsn_" + safe;
    return safe;
}

std::string MakeUniqueSvgId(const std::string& base,
                            std::unordered_map<std::string, int>& id_counts,
                            std::vector<std::string>& warnings) {
    const std::string safe_base = MakeSafeSvgId(base);
    int& count = id_counts[safe_base];
    ++count;
    if (count == 1)
        return safe_base;

    warnings.push_back("Duplicate SVG id base '" + safe_base + "' was made unique.");
    return safe_base + "_" + std::to_string(count);
}

std::string NextContextDisplayId(const std::set<std::string>& reserved_ids,
                                 const std::unordered_map<std::string, int>& id_counts) {
    for (int index = 1; index < 100000; ++index) {
        const std::string candidate = "C" + std::to_string(index);
        const std::string safe_candidate = MakeSafeSvgId(candidate);
        if (reserved_ids.find(safe_candidate) == reserved_ids.end() &&
            id_counts.find(safe_candidate) == id_counts.end())
            return candidate;
    }
    return "C";
}

GsnNodeKind InitialKindFor(const parser::SacmElement& element) {
    if (element.type == "argumentreasoning")
        return GsnNodeKind::Strategy;
    if (element.type == "artifact" || element.type == "artifactreference" || element.type == "expression")
        return GsnNodeKind::Context;
    if (element.assertion_declaration == "assumed")
        return GsnNodeKind::Assumption;
    if (element.assertion_declaration == "justification")
        return GsnNodeKind::Justification;
    return GsnNodeKind::Goal;
}

bool UsesContentText(const parser::SacmElement& element) {
    return element.type == "claim" || element.type == "argumentreasoning";
}

// The requested language's text for one field, or the primary when that field
// has no such translation. Per field, matching the canvas: a half-translated
// element exports its translated fields in the requested language and the rest
// in the primary, rather than dropping text that exists.
std::string LanguageOrPrimary(const std::map<std::string, std::string>& translations,
                              const std::string& language,
                              const std::string& primary) {
    if (language.empty())
        return primary;
    const std::map<std::string, std::string>::const_iterator found = translations.find(language);
    if (found != translations.end() && !found->second.empty())
        return found->second;
    return primary;
}

std::string TextFor(const parser::SacmElement& element, const std::string& language) {
    // Which field carries the body text is decided on the primary content, so a
    // translation can never move a node's text between fields.
    if (UsesContentText(element)) {
        return !element.content.empty() ? LanguageOrPrimary(element.content_langs, language, element.content)
                                        : LanguageOrPrimary(element.description_langs, language, element.description);
    }
    return !element.description.empty() ? LanguageOrPrimary(element.description_langs, language, element.description)
                                        : LanguageOrPrimary(element.content_langs, language, element.content);
}

void AddReference(std::unordered_map<std::string, size_t>& node_by_ref,
                  const std::string& ref,
                  size_t node_index,
                  std::vector<std::string>& warnings) {
    std::string key = core::NormalizeRef(ref);
    if (key.empty())
        return;
    if (node_by_ref.find(key) != node_by_ref.end()) {
        warnings.push_back("Duplicate source reference '" + key + "' maps to the first exported node.");
        return;
    }
    node_by_ref[key] = node_index;
}

const size_t* FindNodeIndex(const std::unordered_map<std::string, size_t>& node_by_ref, const std::string& ref) {
    std::string key = core::NormalizeRef(ref);
    auto it = node_by_ref.find(key);
    if (it == node_by_ref.end())
        return nullptr;
    return &it->second;
}

const size_t* FindFirstNodeIndex(const std::unordered_map<std::string, size_t>& node_by_ref,
                                 const std::vector<std::string>& refs) {
    for (const std::string& ref : refs) {
        const size_t* node_index = FindNodeIndex(node_by_ref, ref);
        if (node_index)
            return node_index;
    }
    return nullptr;
}

const size_t* FindContextTargetIndex(const parser::SacmElement& relationship,
                                     const std::unordered_map<std::string, const parser::SacmElement*>& elements_by_ref,
                                     const std::unordered_set<std::string>& context_attachment_refs,
                                     const std::unordered_map<std::string, size_t>& node_by_ref) {
    for (const std::string& target_ref : relationship.target_refs) {
        const std::string normalized_ref = core::NormalizeRef(target_ref);
        auto target_it = elements_by_ref.find(normalized_ref);
        if (target_it == elements_by_ref.end() || !target_it->second ||
            !IsContextRelationshipTarget(*target_it->second, context_attachment_refs)) {
            continue;
        }

        const size_t* node_index = FindNodeIndex(node_by_ref, normalized_ref);
        if (node_index)
            return node_index;
    }
    return nullptr;
}

std::string EdgeId(const parser::SacmElement& relationship, const std::string& from_id, const std::string& to_id) {
    std::string raw = !relationship.id.empty() ? relationship.id : relationship.name;
    if (raw.empty())
        raw = from_id + "_to_" + to_id;
    return raw;
}

// Collects everything an edge needs to be created, named uniquely, and indexed
// by the relationship it came from. The index exists so a GSN v3 challenge
// aimed at a *relationship* can be pointed at the edge representing it.
struct EdgeSink {
    GsnDiagram& diagram;
    std::unordered_map<std::string, int>& id_counts;
    std::vector<std::string>& warnings;
    std::unordered_map<std::string, std::vector<std::string>>& edge_ids_by_relationship;
};

void IndexEdgeByRelationship(EdgeSink& sink, const parser::SacmElement& relationship, const std::string& edge_id) {
    for (const std::string& ref : {relationship.id, relationship.gid}) {
        const std::string key = core::NormalizeRef(ref);
        if (!key.empty())
            sink.edge_ids_by_relationship[key].push_back(edge_id);
    }
}

// Returns the id assigned to the new edge, or an empty string when the edge was
// skipped.
std::string AddEdge(EdgeSink& sink,
                    const parser::SacmElement& relationship,
                    const std::vector<std::string>& acp_labels,
                    const std::string& from_id,
                    const std::string& to_id,
                    GsnEdgeKind kind) {
    if (from_id.empty() || to_id.empty()) {
        sink.warnings.push_back("Skipped relationship '" + DisplaySourceId(relationship) +
                                "' with a missing endpoint.");
        return {};
    }
    GsnEdge edge;
    edge.id = MakeUniqueSvgId(EdgeId(relationship, from_id, to_id), sink.id_counts, sink.warnings);
    const std::string assigned_id = edge.id;
    edge.from_id = from_id;
    edge.to_id = to_id;
    edge.kind = kind;
    edge.acp_labels = acp_labels;
    sink.diagram.edges.push_back(std::move(edge));
    IndexEdgeByRelationship(sink, relationship, assigned_id);
    return assigned_id;
}

// A challenge whose target is another relationship. Drawn to that edge's
// midpoint rather than to a node.
void AddChallengeToEdge(EdgeSink& sink,
                        const parser::SacmElement& relationship,
                        const std::vector<std::string>& acp_labels,
                        const std::string& from_id,
                        const std::string& target_edge_id) {
    GsnEdge edge;
    edge.id = MakeUniqueSvgId(EdgeId(relationship, from_id, target_edge_id), sink.id_counts, sink.warnings);
    const std::string assigned_id = edge.id;
    edge.from_id = from_id;
    edge.to_edge_id = target_edge_id;
    edge.kind = GsnEdgeKind::Challenges;
    edge.acp_labels = acp_labels;
    sink.diagram.edges.push_back(std::move(edge));
    IndexEdgeByRelationship(sink, relationship, assigned_id);
}

// Assurance Claim Points are held on the case rather than on the element, so
// they are indexed once and attached to whatever they point at. `target_kind`
// is "relationship" or "element"; the id may be either an id or a gid.
std::unordered_map<std::string, std::vector<std::string>> BuildAcpLabelsByTarget(const parser::AssuranceCase& model,
                                                                                 const std::string& target_kind) {
    std::unordered_map<std::string, std::vector<std::string>> labels;
    for (const parser::AcpRecord& acp : model.acps) {
        if (acp.target_kind != target_kind)
            continue;
        const std::string key = core::NormalizeRef(acp.target_id);
        if (key.empty() || acp.id.empty())
            continue;
        labels[key].push_back(acp.id);
    }
    for (auto& entry : labels)
        std::sort(entry.second.begin(), entry.second.end());
    return labels;
}

void AttachAcpLabels(std::vector<std::string>& target,
                     const std::unordered_map<std::string, std::vector<std::string>>& labels_by_target,
                     const std::string& primary_ref,
                     const std::string& secondary_ref) {
    for (const std::string& ref : {primary_ref, secondary_ref}) {
        const std::string key = core::NormalizeRef(ref);
        if (key.empty())
            continue;
        auto it = labels_by_target.find(key);
        if (it == labels_by_target.end())
            continue;
        for (const std::string& label : it->second) {
            if (std::find(target.begin(), target.end(), label) == target.end())
                target.push_back(label);
        }
    }
}

// GSN v3 dialectics. A counter relationship runs *from* the challenging element
// *to* what it challenges, and unlike SupportedBy/InContextOf its endpoints are
// not swapped on import -- see docs/sacm/sacm-gsn-mapping.md.
//
// The target may be an element or another relationship (a challenge may itself
// be challenged), so this runs after every structural edge exists.
void ProjectChallenges(const parser::AssuranceCase& model,
                       const std::unordered_map<std::string, std::vector<std::string>>& relationship_acp_labels,
                       const std::unordered_map<std::string, size_t>& node_by_ref,
                       EdgeSink& sink) {
    for (const parser::SacmElement& relationship : model.elements) {
        if (!IsRelationshipType(relationship.type) || !relationship.is_counter)
            continue;

        const size_t* source_index = FindFirstNodeIndex(node_by_ref, relationship.source_refs);
        if (!source_index) {
            sink.warnings.push_back("Skipped Challenges relationship '" + DisplaySourceId(relationship) +
                                    "' because its counter source was missing.");
            continue;
        }
        if (relationship.target_refs.empty()) {
            sink.warnings.push_back("Skipped Challenges relationship '" + DisplaySourceId(relationship) +
                                    "' because it had no target.");
            continue;
        }

        std::vector<std::string> acp_labels;
        AttachAcpLabels(acp_labels, relationship_acp_labels, relationship.id, relationship.gid);
        const std::string& from_id = sink.diagram.nodes[*source_index].id;
        const std::string& target_ref = relationship.target_refs.front();

        if (const size_t* target_index = FindNodeIndex(node_by_ref, target_ref)) {
            AddEdge(
                sink, relationship, acp_labels, from_id, sink.diagram.nodes[*target_index].id, GsnEdgeKind::Challenges);
            continue;
        }

        const auto target_edges = sink.edge_ids_by_relationship.find(core::NormalizeRef(target_ref));
        if (target_edges != sink.edge_ids_by_relationship.end() && !target_edges->second.empty()) {
            AddChallengeToEdge(sink, relationship, acp_labels, from_id, target_edges->second.front());
            continue;
        }

        sink.warnings.push_back("Skipped Challenges relationship '" + DisplaySourceId(relationship) +
                                "' because its target '" + core::NormalizeRef(target_ref) + "' was not exported.");
    }
}

void ApplyContextKind(GsnNode& node) {
    if (node.kind != GsnNodeKind::Assumption && node.kind != GsnNodeKind::Justification)
        node.kind = GsnNodeKind::Context;
}

} // namespace

GsnProjectionResult BuildGsnProjection(const parser::AssuranceCase& model, const std::string& secondary_language) {
    GsnProjectionResult result;
    std::unordered_map<std::string, size_t> node_by_ref;
    std::unordered_map<std::string, int> node_id_counts;
    std::unordered_map<std::string, int> edge_id_counts;
    std::unordered_set<std::string> exported_artifact_refs;
    std::unordered_set<std::string> visible_terminology_context_refs;
    std::unordered_set<std::string> context_attachment_refs;
    std::unordered_map<std::string, const parser::SacmElement*> elements_by_ref;
    std::set<std::string> reserved_source_ids;
    const std::unordered_map<std::string, std::vector<std::string>> element_acp_labels =
        BuildAcpLabelsByTarget(model, "element");
    const std::unordered_map<std::string, std::vector<std::string>> relationship_acp_labels =
        BuildAcpLabelsByTarget(model, "relationship");
    // Relationship source id -> ids of the edges it produced, so a challenge
    // aimed at a relationship can be pointed at the edge that represents it.
    std::unordered_map<std::string, std::vector<std::string>> edge_ids_by_relationship;

    for (const parser::SacmElement& element : model.elements) {
        if (IsRelationshipType(element.type))
            continue;
        AddElementReference(elements_by_ref, element.id, element);
        AddElementReference(elements_by_ref, element.gid, element);
    }

    for (const parser::SacmElement& relationship : model.elements) {
        if (relationship.type != "assertedcontext")
            continue;
        for (const std::string& source_ref : relationship.source_refs)
            context_attachment_refs.insert(core::NormalizeRef(source_ref));
    }

    for (const parser::SacmElement& relationship : model.elements) {
        if (relationship.type == "assertedevidence") {
            for (const std::string& source_ref : relationship.source_refs)
                exported_artifact_refs.insert(core::NormalizeRef(source_ref));
        } else if (relationship.type == "assertedcontext") {
            if (!HasContextRelationshipTarget(relationship, elements_by_ref, context_attachment_refs))
                continue;
            for (const std::string& source_ref : relationship.source_refs) {
                const std::string normalized_ref = core::NormalizeRef(source_ref);
                exported_artifact_refs.insert(normalized_ref);
                if (IsVisibleTerminologyContextElement(relationship))
                    visible_terminology_context_refs.insert(normalized_ref);
            }
        }
    }

    for (const parser::SacmElement& element : model.elements) {
        if (IsRelationshipType(element.type))
            continue;
        if (ReferencesElement(visible_terminology_context_refs, element))
            continue;
        const std::string source_id = DisplaySourceId(element);
        if (!source_id.empty()) {
            reserved_source_ids.insert(MakeSafeSvgId(source_id));
        }
    }

    for (const parser::SacmElement& element : model.elements) {
        if (IsRelationshipType(element.type))
            continue;
        if (!IsSupportedElementType(element.type)) {
            result.warnings.push_back("Skipped unsupported element type '" + element.type + "'.");
            continue;
        }
        const bool is_visible_terminology_context = ReferencesElement(visible_terminology_context_refs, element);
        if (IsArtifactElementType(element.type)) {
            if (!ReferencesElement(exported_artifact_refs, element))
                continue;
        }

        std::string source_id = is_visible_terminology_context
                                    ? NextContextDisplayId(reserved_source_ids, node_id_counts)
                                    : DisplaySourceId(element);
        if (source_id.empty()) {
            source_id = MakeFallbackId(result.diagram.nodes.size() + 1);
            result.warnings.push_back("Generated fallback id '" + source_id + "' for an exported node.");
        }

        GsnNode node;
        node.id = MakeUniqueSvgId(source_id, node_id_counts, result.warnings);
        node.display_id = is_visible_terminology_context ? source_id : core::GsnIdentifierFor(element);
        node.source_gid = element.gid;
        node.kind = is_visible_terminology_context ? GsnNodeKind::Context : InitialKindFor(element);
        node.title = LanguageOrPrimary(element.name_langs, secondary_language, element.name);
        node.text = TextFor(element, secondary_language);
        node.undeveloped = element.undeveloped;
        node.uninstantiated = element.is_abstract;
        AttachAcpLabels(node.acp_labels, element_acp_labels, element.id, element.gid);
        if (node.title.empty() && node.text.empty()) {
            node.title = "(no title)";
            result.warnings.push_back("Node '" + node.id + "' had no title or text; placeholder title was used.");
        }

        const size_t node_index = result.diagram.nodes.size();
        result.diagram.nodes.push_back(std::move(node));
        AddReference(node_by_ref, element.id, node_index, result.warnings);
        AddReference(node_by_ref, element.gid, node_index, result.warnings);
    }

    EdgeSink edge_sink{result.diagram, edge_id_counts, result.warnings, edge_ids_by_relationship};

    // Pass 1: structural relationships. Counter relationships are deliberately
    // excluded -- a GSN v3 challenge is not support, and projecting one as a
    // SupportedBy edge would show counter-evidence as evidence for the very
    // claim it attacks. They are handled in pass 2, once every structural edge
    // exists and can therefore be named as a challenge target.
    for (const parser::SacmElement& relationship : model.elements) {
        if (!IsRelationshipType(relationship.type) || relationship.is_counter)
            continue;

        std::vector<std::string> edge_acp_labels;
        AttachAcpLabels(edge_acp_labels, relationship_acp_labels, relationship.id, relationship.gid);

        if (relationship.type == "assertedinference") {
            const size_t* target_index = FindFirstNodeIndex(node_by_ref, relationship.target_refs);
            if (!target_index) {
                result.warnings.push_back("Skipped SupportedBy relationship '" + DisplaySourceId(relationship) +
                                          "' because its target was missing.");
                continue;
            }

            std::string attach_parent_id = result.diagram.nodes[*target_index].id;
            if (!relationship.reasoning_ref.empty()) {
                if (const size_t* reasoning_index = FindNodeIndex(node_by_ref, relationship.reasoning_ref)) {
                    result.diagram.nodes[*reasoning_index].kind = GsnNodeKind::Strategy;
                    AddEdge(edge_sink,
                            relationship,
                            edge_acp_labels,
                            attach_parent_id,
                            result.diagram.nodes[*reasoning_index].id,
                            GsnEdgeKind::SupportedBy);
                    attach_parent_id = result.diagram.nodes[*reasoning_index].id;
                } else {
                    result.warnings.push_back("SupportedBy relationship '" + DisplaySourceId(relationship) +
                                              "' referenced missing reasoning '" + relationship.reasoning_ref + "'.");
                }
            }

            if (relationship.source_refs.empty()) {
                result.warnings.push_back("Skipped SupportedBy relationship '" + DisplaySourceId(relationship) +
                                          "' because it had no sources.");
            }
            for (const std::string& source_ref : relationship.source_refs) {
                const size_t* source_index = FindNodeIndex(node_by_ref, source_ref);
                if (!source_index) {
                    result.warnings.push_back("Skipped SupportedBy endpoint '" + core::NormalizeRef(source_ref) +
                                              "' because the node was missing.");
                    continue;
                }
                AddEdge(edge_sink,
                        relationship,
                        edge_acp_labels,
                        attach_parent_id,
                        result.diagram.nodes[*source_index].id,
                        GsnEdgeKind::SupportedBy);
            }
        } else if (relationship.type == "assertedevidence") {
            const size_t* target_index = FindFirstNodeIndex(node_by_ref, relationship.target_refs);
            if (!target_index) {
                result.warnings.push_back("Skipped evidence relationship '" + DisplaySourceId(relationship) +
                                          "' because its target was missing.");
                continue;
            }
            if (relationship.source_refs.empty()) {
                result.warnings.push_back("Skipped evidence relationship '" + DisplaySourceId(relationship) +
                                          "' because it had no sources.");
            }
            for (const std::string& source_ref : relationship.source_refs) {
                const size_t* source_index = FindNodeIndex(node_by_ref, source_ref);
                if (!source_index) {
                    result.warnings.push_back("Skipped evidence endpoint '" + core::NormalizeRef(source_ref) +
                                              "' because the node was missing.");
                    continue;
                }
                result.diagram.nodes[*source_index].kind = GsnNodeKind::Solution;
                AddEdge(edge_sink,
                        relationship,
                        edge_acp_labels,
                        result.diagram.nodes[*target_index].id,
                        result.diagram.nodes[*source_index].id,
                        GsnEdgeKind::SupportedBy);
            }
        } else if (relationship.type == "assertedcontext") {
            if (!HasContextRelationshipTarget(relationship, elements_by_ref, context_attachment_refs))
                continue;

            const size_t* target_index =
                FindContextTargetIndex(relationship, elements_by_ref, context_attachment_refs, node_by_ref);
            if (!target_index) {
                result.warnings.push_back("Skipped InContextOf relationship '" + DisplaySourceId(relationship) +
                                          "' because its target was missing.");
                continue;
            }
            if (relationship.source_refs.empty()) {
                result.warnings.push_back("Skipped InContextOf relationship '" + DisplaySourceId(relationship) +
                                          "' because it had no sources.");
            }
            for (const std::string& source_ref : relationship.source_refs) {
                const size_t* source_index = FindNodeIndex(node_by_ref, source_ref);
                if (!source_index) {
                    result.warnings.push_back("Skipped InContextOf endpoint '" + core::NormalizeRef(source_ref) +
                                              "' because the node was missing.");
                    continue;
                }
                ApplyContextKind(result.diagram.nodes[*source_index]);
                AddEdge(edge_sink,
                        relationship,
                        edge_acp_labels,
                        result.diagram.nodes[*target_index].id,
                        result.diagram.nodes[*source_index].id,
                        GsnEdgeKind::InContextOf);
            }
        }
    }

    ProjectChallenges(model, relationship_acp_labels, node_by_ref, edge_sink);

    return result;
}

} // namespace export_gsn
