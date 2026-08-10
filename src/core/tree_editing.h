#pragma once

#include "core/assurance_tree.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_model.h"

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

// The structural change a `MoveSubtree` makes, found by running the mutator on a
// scratch model and diffing it -- one relationship created, one relationship's
// endpoints rewritten, and any relationship the move left structurally empty.
//
// It is a PLAN rather than an application: nothing is written until the caller has
// seen the whole of it. That ordering is the point. The library can refuse the
// creation (an AssertedInference whose only end is `reasoning` violates source
// [1..*] -- the shape a bare-placed Strategy produces), and a caller that had
// already applied the deletion would be left with a half-moved argument and no
// way back. `unrepresentable_reason` reports that up front so the caller can
// decline before touching the document.
struct MoveSubtreePlan {
    struct Relationship {
        std::string id;
        std::string type; // parser token: assertedinference / assertedcontext / assertedevidence
        std::vector<std::string> sources;
        std::vector<std::string> targets;
        std::string reasoning;
    };

    std::vector<Relationship> created;
    std::vector<Relationship> retargeted;
    std::vector<std::string> deleted_ids;
    std::string unrepresentable_reason; // non-empty when a seam cannot express this move
    bool touches_non_relationships = false;
};

// Diffs `before` against `after` (the same model with `core::MoveSubtree` applied)
// and reports the relationship-level change. `after` must come from the mutator,
// not be hand-built: the point is to mirror what the mutator decided, never to
// re-decide it.
MoveSubtreePlan PlanMoveSubtreeFromDiff(const parser::AssuranceCase& before, const parser::AssuranceCase& after);

bool MoveSubtree(parser::AssuranceCase& model,
                 sacm::AssuranceCasePackage* package,
                 const AssuranceTree& tree,
                 const MoveSubtreeCommand& command,
                 std::string& out_error);

void ApplyTreeDisplayOrder(AssuranceTree& tree, const TreeDisplayOrder& display_order);

} // namespace core
