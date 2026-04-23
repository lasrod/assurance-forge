#include "core/assurance_tree.h"
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

namespace core {

namespace {

// ===== Role classification =====

NodeRole classify_role(const parser::SacmElement& element) {
    if (element.type == "claim") {
        if (element.assertion_declaration == "assumed") return NodeRole::Assumption;
        if (element.assertion_declaration == "justification") return NodeRole::Justification;
        return NodeRole::Claim;
    }
    if (element.type == "argumentreasoning") return NodeRole::Strategy;
    return NodeRole::Other;
}

ElementGroup group_for_role(NodeRole role) {
    switch (role) {
        case NodeRole::Claim:
        case NodeRole::Strategy:
        case NodeRole::Solution:
            return ElementGroup::Group1;
        case NodeRole::Context:
        case NodeRole::Assumption:
        case NodeRole::Justification:
            return ElementGroup::Group2;
        default:
            return ElementGroup::Group1;
    }
}

bool is_relationship(const std::string& type) {
    return type == "assertedinference" ||
           type == "assertedcontext" ||
           type == "assertedevidence";
}

// ===== Relationship processing helpers =====

// Find the first node matching any of the given IDs. Returns nullptr if none found.
static TreeNode* FindFirstNode(const std::vector<std::string>& refs,
                               const std::unordered_map<std::string, TreeNode*>& node_by_id) {
    for (const auto& ref : refs) {
        auto it = node_by_id.find(ref);
        if (it != node_by_id.end()) return it->second;
    }
    return nullptr;
}

// Wire a child node as a Group1 child of the given parent (if not already wired).
static void WireGroup1Child(TreeNode* child, TreeNode* parent, std::unordered_set<std::string>& wired_ids) {
    if (child->parent != nullptr) return; // already wired
    child->parent = parent;
    parent->group1_children.push_back(child);
    wired_ids.insert(child->id);
}

// Wire a child node as a Group2 attachment of the given parent with the specified role.
// Preserves an already-classified Assumption or Justification role rather than forcing it.
static void WireGroup2Attachment(TreeNode* child, TreeNode* parent, NodeRole role,
                                 std::unordered_set<std::string>& wired_ids) {
    if (child->role != NodeRole::Assumption && child->role != NodeRole::Justification) {
        child->role = role;
    }
    child->group = ElementGroup::Group2;
    child->parent = parent;
    parent->group2_attachments.push_back(child);
    wired_ids.insert(child->id);
}

// Process an AssertedInference relationship.
// Wires source claims as Group1 children of the target claim.
// If a reasoning node (Strategy) is specified, it is inserted between target and sources.
static void ProcessInference(const parser::SacmElement& relationship,
                             const std::unordered_map<std::string, TreeNode*>& node_by_id,
                             std::unordered_set<std::string>& wired_ids) {
    TreeNode* target_node = FindFirstNode(relationship.target_refs, node_by_id);
    if (!target_node) return;

    // Determine the parent that sources attach to
    TreeNode* attach_parent = target_node;

    // If reasoning is specified, insert the strategy node between target and sources
    if (!relationship.reasoning_ref.empty()) {
        auto reasoning_it = node_by_id.find(relationship.reasoning_ref);
        if (reasoning_it != node_by_id.end()) {
            TreeNode* reasoning_node = reasoning_it->second;
            reasoning_node->role = NodeRole::Strategy;
            reasoning_node->group = ElementGroup::Group1;
            if (reasoning_node->parent == nullptr) {
                reasoning_node->parent = target_node;
                target_node->group1_children.push_back(reasoning_node);
                wired_ids.insert(reasoning_node->id);
            }
            attach_parent = reasoning_node;
        }
    }

    // Wire source nodes as Group1 children of the attach parent
    for (const auto& source_ref : relationship.source_refs) {
        auto source_it = node_by_id.find(source_ref);
        if (source_it == node_by_id.end()) continue;
        WireGroup1Child(source_it->second, attach_parent, wired_ids);
    }
    wired_ids.insert(target_node->id);
}

// Process an AssertedContext relationship.
// Source artifacts become Group2 Context attachments of the target claim.
static void ProcessContext(const parser::SacmElement& relationship,
                           const std::unordered_map<std::string, TreeNode*>& node_by_id,
                           std::unordered_set<std::string>& wired_ids) {
    if (relationship.target_refs.empty() || relationship.source_refs.empty()) return;

    TreeNode* target_node = FindFirstNode(relationship.target_refs, node_by_id);
    if (!target_node) return;

    for (const auto& source_ref : relationship.source_refs) {
        auto source_it = node_by_id.find(source_ref);
        if (source_it == node_by_id.end()) continue;
        WireGroup2Attachment(source_it->second, target_node, NodeRole::Context, wired_ids);
    }
    wired_ids.insert(target_node->id);
}

// Process an AssertedEvidence relationship.
// Source artifacts become Group1 Solution leaf children of the target claim.
static void ProcessEvidence(const parser::SacmElement& relationship,
                            const std::unordered_map<std::string, TreeNode*>& node_by_id,
                            std::unordered_set<std::string>& wired_ids) {
    if (relationship.target_refs.empty() || relationship.source_refs.empty()) return;

    TreeNode* target_node = FindFirstNode(relationship.target_refs, node_by_id);
    if (!target_node) return;

    for (const auto& source_ref : relationship.source_refs) {
        auto source_it = node_by_id.find(source_ref);
        if (source_it == node_by_id.end()) continue;

        TreeNode* source_node = source_it->second;
        source_node->role = NodeRole::Solution;
        source_node->group = ElementGroup::Group1;
        WireGroup1Child(source_node, target_node, wired_ids);
    }
    wired_ids.insert(target_node->id);
}

}  // namespace

// ===== Build the assurance tree from a parsed SACM case =====

AssuranceTree AssuranceTree::Build(const parser::AssuranceCase& ac) {
    AssuranceTree tree;

    std::unordered_map<std::string, const parser::SacmElement*> element_by_id;
    std::unordered_map<std::string, TreeNode*> node_by_id;
    std::unordered_set<std::string> wired_ids;

    // Step 1: Create a TreeNode for every non-relationship element
    for (const auto& element : ac.elements) {
        if (is_relationship(element.type)) continue;

        auto node = std::make_unique<TreeNode>();
        node->id = element.id.empty() ? element.name : element.id;

        // Build label: "ID: Name\nDetail"
        // Per SACM spec: Claim (11.11) and ArgumentReasoning (11.12) carry their primary
        // text in the dedicated 'content' field.  All other elements (Artifact, ArtifactReference,
        // etc.) inherit descriptive text from the SACMElement 'description' field.
        bool uses_content = (element.type == "claim" || element.type == "argumentreasoning");
        std::string detail = uses_content ? element.content : element.description;
        node->label = node->id + ": " + element.name;
        if (!detail.empty()) {
            node->label += "\n" + detail;
        }

        // Build secondary language label (for language toggle)
        {
            std::string sec_lang = "ja";
            std::string sec_detail;
            if (uses_content) {
                auto cit = element.content_langs.find(sec_lang);
                if (cit != element.content_langs.end() && !cit->second.empty()) {
                    sec_detail = cit->second;
                } else {
                    auto dit = element.description_langs.find(sec_lang);
                    sec_detail = (dit != element.description_langs.end() && !dit->second.empty()) ? dit->second : detail;
                }
            } else {
                auto dit = element.description_langs.find(sec_lang);
                sec_detail = (dit != element.description_langs.end() && !dit->second.empty()) ? dit->second : detail;
            }
            // Use translated name if available, otherwise primary name
            auto nit = element.name_langs.find(sec_lang);
            const std::string& sec_name = (nit != element.name_langs.end() && !nit->second.empty())
                                          ? nit->second : element.name;
            node->label_secondary = node->id + ": " + sec_name;
            if (!sec_detail.empty()) {
                node->label_secondary += "\n" + sec_detail;
            }
        }

        node->role = classify_role(element);
        node->group = group_for_role(node->role);

        element_by_id[node->id] = &element;
        node_by_id[node->id] = node.get();
        tree.nodes.push_back(std::move(node));
    }

    // Step 2: Wire the tree using relationship elements
    for (const auto& element : ac.elements) {
        if (!is_relationship(element.type)) continue;

        if (element.type == "assertedinference") {
            ProcessInference(element, node_by_id, wired_ids);
        } else if (element.type == "assertedcontext") {
            ProcessContext(element, node_by_id, wired_ids);
        } else if (element.type == "assertedevidence") {
            ProcessEvidence(element, node_by_id, wired_ids);
        }
    }

    // Step 3: Find root (first parentless Claim)
    for (const auto& node : tree.nodes) {
        if (node->parent == nullptr && node->role == NodeRole::Claim) {
            if (tree.root == nullptr) {
                tree.root = node.get();
            }
        }
    }

    // Step 4: Collect orphans (non-relationship nodes not wired into the tree)
    for (const auto& node : tree.nodes) {
        if (node->parent == nullptr && node.get() != tree.root) {
            tree.orphans.push_back(node.get());
        }
    }

    return tree;
}

}  // namespace core
