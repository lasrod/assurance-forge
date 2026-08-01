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

// Where a dialectic challenge cluster is placed relative to its host node.
enum class ChallengeSide {
    Side,  // beside the host in a side lane; the layout picks left vs right by
           // balancing the lane heights, and the cluster grows downward
    Below, // directly below the host (used for challenges to references)
};

struct TreeNode {
    std::string id;
    std::string label;
    std::string label_secondary; // secondary language label for toggle

    // Whether the element carried a name, per language. Renderers need this:
    // the canvas must not draw an identifier trailing a separator into nothing
    // ("CG1: "), while the navigator substitutes the element's text for the
    // missing name. It was previously inferred from a trailing ": " in `label`,
    // which meant the two renderers disagreed about what the label may contain.
    bool has_name = false;
    bool has_name_secondary = false;
    bool undeveloped = false;
    bool uninstantiated = false;
    NodeRole role = NodeRole::Other;
    ElementGroup group = ElementGroup::Group1;

    std::vector<TreeNode*> group1_children;    // structural children (row below)
    std::vector<TreeNode*> group2_attachments; // contextual side attachments
    TreeNode* parent = nullptr;

    // GSN v3 dialectic: when true, this node is the source of a Challenges
    // relationship (a counter argument / counter evidence) and the root of its
    // own layout cluster (it is not wired into the anchor's children, so it can
    // carry its own sub-argument). Its connecting edge renders as a dashed
    // open-arrow challenge edge rather than ordinary support.
    bool is_counter_source = false;
    // Id of the challenged target. When `challenge_target_is_relationship`, the
    // target is a relationship element (no tree node); otherwise it is an element.
    std::string challenge_target_id;
    bool challenge_target_is_relationship = false;
    // Id of this counter's own Challenges relationship (the dashed arrow). Lets
    // the arrow itself be challenged (challenge-of-a-challenge).
    std::string challenge_relationship_id;
    // How/where this counter's cluster is placed relative to its host (the
    // challenged element, or the anchor element for a relationship target).
    ChallengeSide challenge_side = ChallengeSide::Side;
    // Host element the cluster is placed beside/below: the challenged element, or
    // (for a relationship target) the relationship's source element.
    std::string challenge_anchor_id;

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
