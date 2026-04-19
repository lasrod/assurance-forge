#include "core/assurance_tree.h"
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

namespace core {

namespace {

NodeRole classify_role(const parser::SacmElement& e) {
    if (e.type == "claim") {
        if (e.assertion_declaration == "assumed") return NodeRole::Assumption;
        return NodeRole::Claim;
    }
    if (e.type == "argumentreasoning") return NodeRole::Strategy;
    if (e.type == "artifact" || e.type == "artifactreference") return NodeRole::Other; // role assigned by relationship
    if (e.type == "expression") return NodeRole::Other;
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

}  // namespace

AssuranceTree AssuranceTree::Build(const parser::AssuranceCase& ac) {
    AssuranceTree tree;

    // Index: id → SacmElement (non-relationship elements only)
    std::unordered_map<std::string, const parser::SacmElement*> elem_by_id;
    // Index: id → TreeNode*
    std::unordered_map<std::string, TreeNode*> node_by_id;
    // Track which ids are wired into the tree
    std::unordered_set<std::string> wired_ids;

    // Step 1: Create TreeNode for every non-relationship element
    for (const auto& e : ac.elements) {
        if (is_relationship(e.type)) continue;

        auto node = std::make_unique<TreeNode>();
        node->id = e.id.empty() ? e.name : e.id;
        node->label = !e.name.empty() ? e.name : e.content;
        node->role = classify_role(e);
        node->group = group_for_role(node->role);

        elem_by_id[node->id] = &e;
        node_by_id[node->id] = node.get();
        tree.nodes.push_back(std::move(node));
    }

    // Step 2: Wire the tree using relationship elements
    for (const auto& e : ac.elements) {
        if (!is_relationship(e.type)) continue;

        if (e.type == "assertedinference") {
            // AssertedInference: source nodes become Group 1 children of target node
            // If reasoning is specified, the reasoning node (Strategy) is inserted between
            // sources and target as: target -> reasoning -> sources
            if (e.target_refs.empty()) continue;

            TreeNode* target_node = nullptr;
            for (const auto& tref : e.target_refs) {
                auto it = node_by_id.find(tref);
                if (it != node_by_id.end()) {
                    target_node = it->second;
                    break;
                }
            }
            if (!target_node) continue;

            // Determine the parent that sources attach to
            TreeNode* attach_parent = target_node;

            // If reasoning is specified, insert reasoning node between target and sources
            if (!e.reasoning_ref.empty()) {
                auto rit = node_by_id.find(e.reasoning_ref);
                if (rit != node_by_id.end()) {
                    TreeNode* reasoning_node = rit->second;
                    reasoning_node->role = NodeRole::Strategy;
                    reasoning_node->group = ElementGroup::Group1;

                    // Add reasoning as child of target (if not already)
                    if (reasoning_node->parent == nullptr) {
                        reasoning_node->parent = target_node;
                        target_node->group1_children.push_back(reasoning_node);
                        wired_ids.insert(reasoning_node->id);
                    }

                    attach_parent = reasoning_node;
                }
            }

            // Add source nodes as Group 1 children of attach_parent
            for (const auto& sref : e.source_refs) {
                auto sit = node_by_id.find(sref);
                if (sit == node_by_id.end()) continue;

                TreeNode* source_node = sit->second;
                if (source_node->parent != nullptr) continue; // already wired

                source_node->parent = attach_parent;
                attach_parent->group1_children.push_back(source_node);
                wired_ids.insert(source_node->id);
            }

            wired_ids.insert(target_node->id);

        } else if (e.type == "assertedcontext") {
            // AssertedContext: source artifact becomes Group 2 attachment (Context) of target claim
            if (e.target_refs.empty() || e.source_refs.empty()) continue;

            TreeNode* target_node = nullptr;
            for (const auto& tref : e.target_refs) {
                auto it = node_by_id.find(tref);
                if (it != node_by_id.end()) { target_node = it->second; break; }
            }
            if (!target_node) continue;

            for (const auto& sref : e.source_refs) {
                auto sit = node_by_id.find(sref);
                if (sit == node_by_id.end()) continue;

                TreeNode* source_node = sit->second;
                source_node->role = NodeRole::Context;
                source_node->group = ElementGroup::Group2;
                source_node->parent = target_node;
                target_node->group2_attachments.push_back(source_node);
                wired_ids.insert(source_node->id);
            }

            wired_ids.insert(target_node->id);

        } else if (e.type == "assertedevidence") {
            // AssertedEvidence: source artifact becomes Group 1 child (Solution leaf) of target claim
            if (e.target_refs.empty() || e.source_refs.empty()) continue;

            TreeNode* target_node = nullptr;
            for (const auto& tref : e.target_refs) {
                auto it = node_by_id.find(tref);
                if (it != node_by_id.end()) { target_node = it->second; break; }
            }
            if (!target_node) continue;

            for (const auto& sref : e.source_refs) {
                auto sit = node_by_id.find(sref);
                if (sit == node_by_id.end()) continue;

                TreeNode* source_node = sit->second;
                source_node->role = NodeRole::Solution;
                source_node->group = ElementGroup::Group1;
                source_node->parent = target_node;
                target_node->group1_children.push_back(source_node);
                wired_ids.insert(source_node->id);
            }

            wired_ids.insert(target_node->id);
        }
    }

    // Step 3: Find root (claim with no parent)
    for (const auto& n : tree.nodes) {
        if (n->parent == nullptr && n->role == NodeRole::Claim) {
            if (tree.root == nullptr) {
                tree.root = n.get();
            }
        }
    }

    // Step 4: Collect orphans (non-relationship nodes not wired into the tree)
    for (const auto& n : tree.nodes) {
        if (n->parent == nullptr && n.get() != tree.root) {
            tree.orphans.push_back(n.get());
        }
    }

    return tree;
}

}  // namespace core
