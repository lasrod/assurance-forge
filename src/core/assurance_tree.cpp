#include "core/assurance_tree.h"

#include "core/element_factory.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace core {

namespace {

// ===== Role classification =====

NodeRole classify_role(const parser::SacmElement& element) {
    if (element.type == "claim") {
        if (element.assertion_declaration == "assumed")
            return NodeRole::Assumption;
        if (element.assertion_declaration == "justification")
            return NodeRole::Justification;
        return NodeRole::Claim;
    }
    if (element.type == "argumentreasoning")
        return NodeRole::Strategy;
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
    return type == "assertedinference" || type == "assertedcontext" || type == "assertedevidence";
}

// ===== Relationship processing helpers =====

// Find the first node matching any of the given IDs. Returns nullptr if none found.
static TreeNode* FindFirstNode(const std::vector<std::string>& refs,
                               const std::unordered_map<std::string, TreeNode*>& node_by_id) {
    for (const auto& ref : refs) {
        auto it = node_by_id.find(ref);
        if (it != node_by_id.end())
            return it->second;
    }
    return nullptr;
}

// Wire a child node as a Group1 child of the given parent (if not already wired).
static void WireGroup1Child(TreeNode* child, TreeNode* parent, std::unordered_set<std::string>& wired_ids) {
    if (child->parent != nullptr)
        return; // already wired
    child->parent = parent;
    parent->group1_children.push_back(child);
    wired_ids.insert(child->id);
}

// Wire a child node as a Group2 attachment of the given parent with the specified role.
// Preserves an already-classified Assumption or Justification role rather than forcing it.
static void
WireGroup2Attachment(TreeNode* child, TreeNode* parent, NodeRole role, std::unordered_set<std::string>& wired_ids) {
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
    if (!target_node)
        return;

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
        if (source_it == node_by_id.end())
            continue;
        WireGroup1Child(source_it->second, attach_parent, wired_ids);
    }
    wired_ids.insert(target_node->id);
}

// Process an AssertedContext relationship.
// Source artifacts become Group2 Context attachments of the target claim.
static void ProcessContext(const parser::SacmElement& relationship,
                           const std::unordered_map<std::string, TreeNode*>& node_by_id,
                           std::unordered_set<std::string>& wired_ids) {
    if (relationship.target_refs.empty() || relationship.source_refs.empty())
        return;

    TreeNode* target_node = FindFirstNode(relationship.target_refs, node_by_id);
    if (!target_node)
        return;

    for (const auto& source_ref : relationship.source_refs) {
        auto source_it = node_by_id.find(source_ref);
        if (source_it == node_by_id.end())
            continue;
        WireGroup2Attachment(source_it->second, target_node, NodeRole::Context, wired_ids);
    }
    wired_ids.insert(target_node->id);
}

// Process an AssertedEvidence relationship.
// Source artifacts become Group1 Solution leaf children of the target claim.
static void ProcessEvidence(const parser::SacmElement& relationship,
                            const std::unordered_map<std::string, TreeNode*>& node_by_id,
                            std::unordered_set<std::string>& wired_ids) {
    if (relationship.target_refs.empty() || relationship.source_refs.empty())
        return;

    TreeNode* target_node = FindFirstNode(relationship.target_refs, node_by_id);
    if (!target_node)
        return;

    for (const auto& source_ref : relationship.source_refs) {
        auto source_it = node_by_id.find(source_ref);
        if (source_it == node_by_id.end())
            continue;

        TreeNode* source_node = source_it->second;
        source_node->role = NodeRole::Solution;
        source_node->group = ElementGroup::Group1;
        WireGroup1Child(source_node, target_node, wired_ids);
    }
    wired_ids.insert(target_node->id);
}

// Resolve the element node a challenge edge should hang off / anchor near.
// `target_id` is the challenged entity: it may be an element (returned directly)
// or a relationship, in which case we follow the relationship's own target one
// or more hops until we reach an element node. Returns nullptr if unresolved.
static TreeNode* ResolveChallengeAnchorElement(const std::string& target_id,
                                               const std::unordered_map<std::string, TreeNode*>& node_by_id,
                                               const std::unordered_map<std::string, std::string>& rel_first_target) {
    std::string current = target_id;
    for (int hop = 0; hop < 8; ++hop) {
        auto node_it = node_by_id.find(current);
        if (node_it != node_by_id.end())
            return node_it->second; // reached an element node
        auto rel_it = rel_first_target.find(current);
        if (rel_it == rel_first_target.end() || rel_it->second.empty())
            return nullptr; // dangling or unknown
        current = rel_it->second;
    }
    return nullptr;
}

// Process a counter (dialectic challenge) relationship. The counter source
// (a CG goal / CSn solution) becomes the root of its own layout cluster: it is
// NOT wired into the anchor's children, so it can carry its own sub-argument
// (strategies, solutions, context, ...). It is flagged with the data the layout
// engine needs to position the cluster relative to the target and the renderer
// needs to draw the dashed open-arrow challenge edge.
static void ProcessChallenge(const parser::SacmElement& relationship,
                             const std::unordered_map<std::string, TreeNode*>& node_by_id,
                             const std::unordered_map<std::string, std::string>& rel_first_target,
                             const std::unordered_map<std::string, std::string>& rel_source,
                             std::unordered_set<std::string>& wired_ids) {
    if (relationship.source_refs.empty() || relationship.target_refs.empty())
        return;

    TreeNode* source_node = FindFirstNode(relationship.source_refs, node_by_id);
    if (!source_node)
        return;

    const std::string& target_id = relationship.target_refs.front();
    const bool target_is_relationship = node_by_id.find(target_id) == node_by_id.end();

    // Anchor: for an element target, the target itself. For a relationship target,
    // host on the relationship's SOURCE node — the context/solution/sub-claim the
    // relationship points to (e.g. challenging "InContextOf: C1 -> G1" hosts on C1,
    // so the counter sits below the context, not below the parent claim).
    TreeNode* anchor = nullptr;
    if (target_is_relationship) {
        const auto src_it = rel_source.find(target_id);
        if (src_it != rel_source.end()) {
            const auto node_it = node_by_id.find(src_it->second);
            if (node_it != node_by_id.end())
                anchor = node_it->second;
        }
    }
    if (!anchor)
        anchor = ResolveChallengeAnchorElement(target_id, node_by_id, rel_first_target);
    if (!anchor)
        return; // cannot place — leave the counter node as an orphan

    // Counter evidence sources are artifactreferences; give them the Solution
    // role so they render as a Solution rather than a generic node. Counter
    // argument sources are claims (already classified as Claim).
    if (relationship.type == "assertedevidence")
        source_node->role = NodeRole::Solution;

    source_node->is_counter_source = true;
    source_node->challenge_target_id = target_id;
    source_node->challenge_target_is_relationship = target_is_relationship;
    source_node->challenge_relationship_id = relationship.id;
    source_node->challenge_anchor_id = anchor->id;
    // The counter is a cluster root: leave its parent null so it is laid out as
    // its own subtree and positioned by the layout's counter-placement pass.
}

} // namespace

// ===== Build the assurance tree from a parsed SACM case =====

AssuranceTree AssuranceTree::Build(const parser::AssuranceCase& ac, const std::string& secondary_language) {
    AssuranceTree tree;

    std::unordered_map<std::string, const parser::SacmElement*> element_by_id;
    std::unordered_map<std::string, TreeNode*> node_by_id;
    std::unordered_set<std::string> wired_ids;

    // Step 1: Create a TreeNode for every non-relationship element.
    for (const auto& element : ac.elements) {
        if (is_relationship(element.type))
            continue;

        auto node = std::make_unique<TreeNode>();
        node->id = element.id.empty() ? element.name : element.id;
        const std::string display_identifier = core::GsnIdentifierFor(element);
        node->undeveloped = element.undeveloped;
        node->uninstantiated = element.is_abstract;

        // Build label: "ID: Name\nDetail"
        // Per SACM spec: Claim (11.11) and ArgumentReasoning (11.12) carry their primary
        // text in the dedicated 'content' field.  All other elements (Artifact, ArtifactReference,
        // etc.) inherit descriptive text from the SACMElement 'description' field.
        bool uses_content = (element.type == "claim" || element.type == "argumentreasoning");
        std::string detail = uses_content ? element.content : element.description;
        // The separator belongs to the name, not the identifier: an element with
        // no name yet must not render as "CG1: ", which reads as a broken node.
        node->has_name = !element.name.empty();
        node->label = node->has_name ? display_identifier + ": " + element.name : display_identifier;
        if (!detail.empty()) {
            node->label += "\n" + detail;
        }

        // Build secondary language label (for language toggle)
        {
            const std::string& sec_lang = secondary_language;
            std::string sec_detail;
            if (uses_content) {
                auto cit = element.content_langs.find(sec_lang);
                if (cit != element.content_langs.end() && !cit->second.empty()) {
                    sec_detail = cit->second;
                } else {
                    auto dit = element.description_langs.find(sec_lang);
                    sec_detail =
                        (dit != element.description_langs.end() && !dit->second.empty()) ? dit->second : detail;
                }
            } else {
                auto dit = element.description_langs.find(sec_lang);
                sec_detail = (dit != element.description_langs.end() && !dit->second.empty()) ? dit->second : detail;
            }
            // Use translated name if available, otherwise primary name
            auto nit = element.name_langs.find(sec_lang);
            const std::string& sec_name =
                (nit != element.name_langs.end() && !nit->second.empty()) ? nit->second : element.name;
            node->has_name_secondary = !sec_name.empty();
            node->label_secondary =
                node->has_name_secondary ? display_identifier + ": " + sec_name : display_identifier;
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

    // Per-relationship lookups used to host a challenge that targets a
    // relationship: its first target ref (to resolve a nearby anchor element) and
    // its source element (the context/solution/sub-claim the challenge hangs off).
    std::unordered_map<std::string, std::string> rel_first_target;
    std::unordered_map<std::string, std::string> rel_source;
    for (const auto& element : ac.elements) {
        if (!is_relationship(element.type) || element.id.empty())
            continue;
        if (!element.target_refs.empty())
            rel_first_target[element.id] = element.target_refs.front();
        if (const TreeNode* source = FindFirstNode(element.source_refs, node_by_id))
            rel_source[element.id] = source->id;
    }

    // Step 2: Wire the tree using relationship elements
    for (const auto& element : ac.elements) {
        if (!is_relationship(element.type))
            continue;

        // Counter relationships are dialectic challenges, not structural support.
        if (element.is_counter) {
            ProcessChallenge(element, node_by_id, rel_first_target, rel_source, wired_ids);
            continue;
        }

        if (element.type == "assertedinference") {
            ProcessInference(element, node_by_id, wired_ids);
        } else if (element.type == "assertedcontext") {
            ProcessContext(element, node_by_id, wired_ids);
        } else if (element.type == "assertedevidence") {
            ProcessEvidence(element, node_by_id, wired_ids);
        }
    }

    // Step 2.5: Decide where each counter cluster sits relative to its host (the
    // anchor element). Done after all wiring so the host's role is known.
    //
    //   * Context host (a reference) → directly below it (it grows downward).
    //   * Everything else (Goal/Strategy/Solution, Assumption/Justification) →
    //     beside it in a side lane. The layout engine picks left vs right by
    //     balancing the two lanes' heights (and keeps an attachment's own
    //     challenge on the attachment's outward side, away from the host).
    //
    // For a relationship challenge the anchor is the relationship's source, so a
    // challenged InContextOf naturally hosts on its context and drops below it.
    for (const auto& owned_node : tree.nodes) {
        TreeNode* counter = owned_node.get();
        if (!counter->is_counter_source)
            continue;
        const auto anchor_it = node_by_id.find(counter->challenge_anchor_id);
        const TreeNode* anchor = anchor_it != node_by_id.end() ? anchor_it->second : nullptr;
        counter->challenge_side =
            (anchor && anchor->role == NodeRole::Context) ? ChallengeSide::Below : ChallengeSide::Side;
    }

    // Step 3: Find root (first parentless Claim). Counter sources are parentless
    // cluster roots, not the argument root, so they are never chosen here.
    for (const auto& node : tree.nodes) {
        if (node->parent == nullptr && node->role == NodeRole::Claim && !node->is_counter_source) {
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

const TreeNode* FindTreeNode(const AssuranceTree& tree, const std::string& id) {
    for (const std::unique_ptr<TreeNode>& node : tree.nodes) {
        if (node && node->id == id)
            return node.get();
    }
    return nullptr;
}

} // namespace core
