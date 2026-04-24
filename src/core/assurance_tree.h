#pragma once

#include <string>
#include <vector>
#include <memory>
#include "parser/xml_parser.h"

namespace core {

enum class ElementGroup {
    Group1,  // Structural: Claim, Strategy, Solution â€” placed below parent
    Group2   // Contextual: Context, Assumption, Justification â€” side-attached
};

enum class NodeRole {
    Claim,
    Strategy,
    Solution,
    Context,
    Assumption,
    Justification,
    Other
};

struct TreeNode {
    std::string id;
    std::string label;
    std::string label_secondary;  // secondary language label for toggle
    bool undeveloped = false;
    NodeRole role = NodeRole::Other;
    ElementGroup group = ElementGroup::Group1;

    std::vector<TreeNode*> group1_children;   // structural children (row below)
    std::vector<TreeNode*> group2_attachments; // contextual side attachments
    TreeNode* parent = nullptr;

    // Layout scratch fields (filled by layout engine)
    int subtree_width = 1;
    int left_overhang = 0;   // extra columns Group2 extends beyond left edge
    int right_overhang = 0;  // extra columns Group2 extends beyond right edge
};

class AssuranceTree {
public:
    // Build a tree from a parsed assurance case
    static AssuranceTree Build(const parser::AssuranceCase& ac);

    TreeNode* root = nullptr;
    std::vector<TreeNode*> orphans; // nodes not connected by any relationship
    std::vector<std::unique_ptr<TreeNode>> nodes; // owns all nodes
};

}  // namespace core
