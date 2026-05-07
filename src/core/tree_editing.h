#pragma once

#include "core/assurance_tree.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace core {

constexpr const char* AF_TREE_NODE_PAYLOAD = "AF_TREE_NODE";

struct TreeDragPayload {
    std::string dragged_element_id;
};

enum class TreeDropMode {
    Before,
    After,
    AsChild,
};

enum class TreeRelationshipKind {
    None,
    SupportedBy,
    InContextOf,
    AssertedEvidence,
};

struct TreeDropValidationResult {
    bool allowed = false;
    std::string reason;
    TreeRelationshipKind relationship_kind = TreeRelationshipKind::None;
    bool changes_semantic_relationship = false;
};

struct TreeDisplayOrder {
    std::unordered_map<std::string, std::vector<std::string>> children_by_parent;
};

struct ReorderSiblingsCommand {
    std::string dragged_element_id;
    std::string target_element_id;
    TreeDropMode drop_mode = TreeDropMode::Before;
};

struct MoveSubtreeCommand {
    std::string dragged_element_id;
    std::string new_parent_element_id;
};

TreeDropValidationResult ValidateTreeDrop(const parser::AssuranceCase& model,
                                          const AssuranceTree& tree,
                                          const std::string& dragged_element_id,
                                          const std::string& target_element_id,
                                          TreeDropMode drop_mode);

bool ReorderSiblings(parser::AssuranceCase& model,
                     sacm::AssuranceCasePackage* package,
                     const AssuranceTree& tree,
                     TreeDisplayOrder& display_order,
                     const ReorderSiblingsCommand& command,
                     std::string& out_error);

bool MoveSubtree(parser::AssuranceCase& model,
                 sacm::AssuranceCasePackage* package,
                 const AssuranceTree& tree,
                 const MoveSubtreeCommand& command,
                 std::string& out_error);

void ApplyTreeDisplayOrder(AssuranceTree& tree, const TreeDisplayOrder& display_order);

} // namespace core
