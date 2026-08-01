#pragma once

#include "core/assurance_tree.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
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

// Which relationship a drop would create. **The names mix two vocabularies**:
// `SupportedBy` and `InContextOf` are GSN's, `AssertedEvidence` is SACM's. Each
// value names the SACM relationship actually written:
//
//   SupportedBy      -> `assertedinference`
//   InContextOf      -> `assertedcontext`
//   AssertedEvidence -> `assertedevidence`
//
// So a GSN `SupportedBy` whose child is a Solution maps to `AssertedEvidence`,
// not to the value spelled `SupportedBy` — GSN has one relationship there where
// SACM has two, split by what sits at the far end. Reading the enum as pure GSN
// makes that look like a bug and invites "fixing" the return value, which would
// write an inference where the evidence relationship belongs.
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

struct TreeEditIndex {
    std::unordered_set<std::string> element_ids;
    std::unordered_map<std::string, int> parent_counts;
    std::unordered_map<std::string, std::vector<std::string>> graph_children;
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

TreeEditIndex BuildTreeEditIndex(const parser::AssuranceCase& model);

TreeDropValidationResult ValidateTreeDrop(const TreeEditIndex& index,
                                          const AssuranceTree& tree,
                                          const std::string& dragged_element_id,
                                          const std::string& target_element_id,
                                          TreeDropMode drop_mode);

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
