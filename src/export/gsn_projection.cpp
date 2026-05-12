#include "export/gsn_projection.h"

#include "core/string_utils.h"
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

std::string TextFor(const parser::SacmElement& element) {
    const std::string detail = UsesContentText(element)
                                   ? (!element.content.empty() ? element.content : element.description)
                                   : (!element.description.empty() ? element.description : element.content);
    if (!detail.empty())
        return detail;
    return element.name;
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

size_t* FindNodeIndex(std::unordered_map<std::string, size_t>& node_by_ref, const std::string& ref) {
    std::string key = core::NormalizeRef(ref);
    auto it = node_by_ref.find(key);
    if (it == node_by_ref.end())
        return nullptr;
    return &it->second;
}

const size_t* FindNodeIndex(const std::unordered_map<std::string, size_t>& node_by_ref, const std::string& ref) {
    std::string key = core::NormalizeRef(ref);
    auto it = node_by_ref.find(key);
    if (it == node_by_ref.end())
        return nullptr;
    return &it->second;
}

std::string EdgeId(const parser::SacmElement& relationship, const std::string& from_id, const std::string& to_id) {
    std::string raw = !relationship.id.empty() ? relationship.id : relationship.name;
    if (raw.empty())
        raw = from_id + "_to_" + to_id;
    return raw;
}

void AddEdge(GsnDiagram& diagram,
             std::unordered_map<std::string, int>& id_counts,
             std::vector<std::string>& warnings,
             const parser::SacmElement& relationship,
             const std::string& from_id,
             const std::string& to_id,
             GsnEdgeKind kind) {
    if (from_id.empty() || to_id.empty()) {
        warnings.push_back("Skipped relationship '" + DisplaySourceId(relationship) + "' with a missing endpoint.");
        return;
    }
    GsnEdge edge;
    edge.id = MakeUniqueSvgId(EdgeId(relationship, from_id, to_id), id_counts, warnings);
    edge.from_id = from_id;
    edge.to_id = to_id;
    edge.kind = kind;
    diagram.edges.push_back(std::move(edge));
}

void ApplyContextKind(GsnNode& node) {
    if (node.kind != GsnNodeKind::Assumption && node.kind != GsnNodeKind::Justification)
        node.kind = GsnNodeKind::Context;
}

} // namespace

GsnProjectionResult BuildGsnProjection(const parser::AssuranceCase& model) {
    GsnProjectionResult result;
    std::unordered_map<std::string, size_t> node_by_ref;
    std::unordered_map<std::string, int> node_id_counts;
    std::unordered_map<std::string, int> edge_id_counts;
    std::unordered_set<std::string> exported_artifact_refs;
    std::unordered_set<std::string> visible_terminology_context_refs;
    std::unordered_set<std::string> context_attachment_refs;
    std::unordered_map<std::string, const parser::SacmElement*> elements_by_ref;
    std::set<std::string> reserved_source_ids;

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
        node.source_gid = element.gid;
        node.kind = is_visible_terminology_context ? GsnNodeKind::Context : InitialKindFor(element);
        node.text = TextFor(element);
        if (node.text.empty()) {
            node.text = "(no text)";
            result.warnings.push_back("Node '" + node.id + "' had no text; placeholder text was used.");
        }

        const size_t node_index = result.diagram.nodes.size();
        result.diagram.nodes.push_back(std::move(node));
        AddReference(node_by_ref, element.id, node_index, result.warnings);
        AddReference(node_by_ref, element.gid, node_index, result.warnings);
    }

    for (const parser::SacmElement& relationship : model.elements) {
        if (!IsRelationshipType(relationship.type))
            continue;

        if (relationship.type == "assertedinference") {
            const size_t* target_index = nullptr;
            for (const std::string& target_ref : relationship.target_refs) {
                target_index = FindNodeIndex(node_by_ref, target_ref);
                if (target_index)
                    break;
            }
            if (!target_index) {
                result.warnings.push_back("Skipped SupportedBy relationship '" + DisplaySourceId(relationship) +
                                          "' because its target was missing.");
                continue;
            }

            std::string attach_parent_id = result.diagram.nodes[*target_index].id;
            if (!relationship.reasoning_ref.empty()) {
                if (const size_t* reasoning_index = FindNodeIndex(node_by_ref, relationship.reasoning_ref)) {
                    result.diagram.nodes[*reasoning_index].kind = GsnNodeKind::Strategy;
                    AddEdge(result.diagram,
                            edge_id_counts,
                            result.warnings,
                            relationship,
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
                AddEdge(result.diagram,
                        edge_id_counts,
                        result.warnings,
                        relationship,
                        attach_parent_id,
                        result.diagram.nodes[*source_index].id,
                        GsnEdgeKind::SupportedBy);
            }
        } else if (relationship.type == "assertedevidence") {
            const size_t* target_index = nullptr;
            for (const std::string& target_ref : relationship.target_refs) {
                target_index = FindNodeIndex(node_by_ref, target_ref);
                if (target_index)
                    break;
            }
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
                AddEdge(result.diagram,
                        edge_id_counts,
                        result.warnings,
                        relationship,
                        result.diagram.nodes[*target_index].id,
                        result.diagram.nodes[*source_index].id,
                        GsnEdgeKind::SupportedBy);
            }
        } else if (relationship.type == "assertedcontext") {
            if (!HasContextRelationshipTarget(relationship, elements_by_ref, context_attachment_refs))
                continue;

            const size_t* target_index = nullptr;
            for (const std::string& target_ref : relationship.target_refs) {
                target_index = FindNodeIndex(node_by_ref, target_ref);
                if (target_index)
                    break;
            }
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
                AddEdge(result.diagram,
                        edge_id_counts,
                        result.warnings,
                        relationship,
                        result.diagram.nodes[*target_index].id,
                        result.diagram.nodes[*source_index].id,
                        GsnEdgeKind::InContextOf);
            }
        }
    }

    return result;
}

} // namespace export_gsn
