#pragma once

#include "parser/xml_parser.h"

#include <memory>
#include <string>
#include <vector>

namespace core {

enum class ElementGroup {
    Group1, // Structural: Claim, Strategy, Solution â€” placed below parent
    Group2  // Contextual: Context, Assumption, Justification â€” side-attached
};

enum class NodeRole { Claim, Strategy, Solution, Context, Assumption, Justification, Other };

struct TreeNode {
    std::string id;
    std::string label;
    std::string label_secondary; // secondary language label for toggle
    bool undeveloped = false;
    NodeRole role = NodeRole::Other;
    ElementGroup group = ElementGroup::Group1;

    std::vector<TreeNode*> group1_children;    // structural children (row below)
    std::vector<TreeNode*> group2_attachments; // contextual side attachments
    TreeNode* parent = nullptr;

    // GSN v3 dialectic: when true, this node is the source of a Challenges
    // relationship (a counter argument / counter evidence). Its connecting edge
    // renders as a dashed open-arrow challenge edge rather than ordinary support.
    bool is_counter_source = false;
    // Id of the challenged target. When `challenge_target_is_relationship`, the
    // target is a relationship element (no tree node) and the edge anchors to
    // that relationship's midpoint; otherwise it is the parent element body.
    std::string challenge_target_id;
    bool challenge_target_is_relationship = false;

    // Layout scratch fields (filled by layout engine)
    int subtree_width = 1;
    int left_overhang = 0;  // extra columns Group2 extends beyond left edge
    int right_overhang = 0; // extra columns Group2 extends beyond right edge
};

class AssuranceTree {
public:
    // Build a tree from a parsed assurance case. `secondary_language` selects which
    // translation entry (key into name_langs/description_langs/content_langs) is
    // used to populate TreeNode::label_secondary.
    static AssuranceTree Build(const parser::AssuranceCase& ac, const std::string& secondary_language = "ja");

    TreeNode* root = nullptr;
    std::vector<TreeNode*> orphans;               // nodes not connected by any relationship
    std::vector<std::unique_ptr<TreeNode>> nodes; // owns all nodes
};

// Returns the node with the given element id, or nullptr if none. `nodes` owns
// every node in the tree (root, orphans, children, attachments), so a flat scan
// covers the whole tree.
const TreeNode* FindTreeNode(const AssuranceTree& tree, const std::string& id);

} // namespace core
