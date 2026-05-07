#include "core/tree_editing.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace core {
namespace {

bool IsRelationshipType(const std::string& type) {
    return type == "assertedinference" || type == "assertedcontext" || type == "assertedevidence";
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& model, const std::string& id) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

parser::SacmElement* FindElement(parser::AssuranceCase& model, const std::string& id) {
    for (parser::SacmElement& element : model.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

const TreeNode* FindTreeNode(const TreeNode* node, const std::string& id) {
    if (!node)
        return nullptr;
    if (node->id == id)
        return node;
    for (const TreeNode* child : node->group1_children) {
        if (const TreeNode* found = FindTreeNode(child, id))
            return found;
    }
    for (const TreeNode* attachment : node->group2_attachments) {
        if (const TreeNode* found = FindTreeNode(attachment, id))
            return found;
    }
    return nullptr;
}

const TreeNode* FindTreeNode(const AssuranceTree& tree, const std::string& id) {
    if (const TreeNode* found = FindTreeNode(tree.root, id))
        return found;
    for (const TreeNode* orphan : tree.orphans) {
        if (const TreeNode* found = FindTreeNode(orphan, id))
            return found;
    }
    return nullptr;
}

bool IsDescendant(const TreeNode* root, const std::string& potential_descendant) {
    if (!root)
        return false;
    for (const TreeNode* child : root->group1_children) {
        if (child->id == potential_descendant || IsDescendant(child, potential_descendant))
            return true;
    }
    for (const TreeNode* attachment : root->group2_attachments) {
        if (attachment->id == potential_descendant || IsDescendant(attachment, potential_descendant))
            return true;
    }
    return false;
}

bool IsContextLike(NodeRole role) {
    return role == NodeRole::Context || role == NodeRole::Assumption || role == NodeRole::Justification;
}

TreeRelationshipKind DetermineRelationshipKind(NodeRole parent_role, NodeRole child_role) {
    if (parent_role == NodeRole::Claim) {
        if (child_role == NodeRole::Claim || child_role == NodeRole::Strategy)
            return TreeRelationshipKind::SupportedBy;
        if (child_role == NodeRole::Solution)
            return TreeRelationshipKind::AssertedEvidence;
        if (IsContextLike(child_role))
            return TreeRelationshipKind::InContextOf;
    }

    if (parent_role == NodeRole::Strategy) {
        if (child_role == NodeRole::Claim)
            return TreeRelationshipKind::SupportedBy;
        if (IsContextLike(child_role))
            return TreeRelationshipKind::InContextOf;
    }

    return TreeRelationshipKind::None;
}

struct TreeEditIndex {
    std::unordered_map<std::string, int> parent_counts;
    std::unordered_map<std::string, std::vector<std::string>> graph_children;
};

void AddIndexedEdge(TreeEditIndex& index, const std::string& parent, const std::string& child) {
    if (parent.empty() || child.empty() || parent == child)
        return;
    ++index.parent_counts[child];
    index.graph_children[parent].push_back(child);
}

TreeEditIndex BuildTreeEditIndex(const parser::AssuranceCase& model) {
    std::unordered_set<std::string> node_ids;
    for (const parser::SacmElement& element : model.elements) {
        if (!IsRelationshipType(element.type) && !element.id.empty())
            node_ids.insert(element.id);
    }

    TreeEditIndex index;
    for (const parser::SacmElement& relationship : model.elements) {
        if (!IsRelationshipType(relationship.type))
            continue;

        for (const std::string& target : relationship.target_refs) {
            if (node_ids.find(target) == node_ids.end())
                continue;

            if (relationship.type == "assertedinference") {
                std::string attach_parent = target;
                if (!relationship.reasoning_ref.empty() &&
                    node_ids.find(relationship.reasoning_ref) != node_ids.end()) {
                    AddIndexedEdge(index, target, relationship.reasoning_ref);
                    attach_parent = relationship.reasoning_ref;
                }
                for (const std::string& source : relationship.source_refs) {
                    if (node_ids.find(source) != node_ids.end())
                        AddIndexedEdge(index, attach_parent, source);
                }
            } else {
                for (const std::string& source : relationship.source_refs) {
                    if (node_ids.find(source) != node_ids.end())
                        AddIndexedEdge(index, target, source);
                }
            }
        }
    }
    return index;
}

bool HasGraphPath(const TreeEditIndex& index, const std::string& from, const std::string& to) {
    std::vector<std::string> stack{from};
    std::unordered_set<std::string> visited;
    while (!stack.empty()) {
        std::string current = stack.back();
        stack.pop_back();
        if (!visited.insert(current).second)
            continue;
        if (current == to)
            return true;
        auto found = index.graph_children.find(current);
        if (found == index.graph_children.end())
            continue;
        for (const std::string& child : found->second)
            stack.push_back(child);
    }
    return false;
}

bool RemoveValue(std::vector<std::string>& values, const std::string& value) {
    const size_t old_size = values.size();
    values.erase(std::remove(values.begin(), values.end(), value), values.end());
    return values.size() != old_size;
}

bool ContainsValue(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool IsParserRelationshipDangling(const parser::SacmElement& relationship) {
    if (!IsRelationshipType(relationship.type))
        return false;
    if (relationship.target_refs.empty())
        return true;
    if (relationship.type == "assertedinference")
        return relationship.source_refs.empty() && relationship.reasoning_ref.empty();
    return relationship.source_refs.empty();
}

bool IsSacmInferenceDangling(const sacm::AssertedInference& relationship) {
    if (relationship.targets.empty())
        return true;
    return relationship.sources.empty() && relationship.reasoning.empty();
}

bool IsSacmRelationshipDangling(const sacm::AssertedRelationship& relationship) {
    if (relationship.targets.empty())
        return true;
    return relationship.sources.empty();
}

std::string GenerateRelationshipId(const parser::AssuranceCase& model) {
    std::unordered_set<std::string> ids;
    for (const parser::SacmElement& element : model.elements) {
        if (!element.id.empty())
            ids.insert(element.id);
    }
    for (int i = 1; i < 100000; ++i) {
        std::string candidate = "R" + std::to_string(i);
        if (ids.find(candidate) == ids.end())
            return candidate;
    }
    return "Rx" + std::to_string(ids.size() + 1);
}

struct IncomingRelationship {
    parser::SacmElement* relationship = nullptr;
    bool child_is_reasoning = false;
};

struct ConstIncomingRelationship {
    const parser::SacmElement* relationship = nullptr;
    bool child_is_reasoning = false;
};

IncomingRelationship
FindIncomingRelationship(parser::AssuranceCase& model, const std::string& parent_id, const std::string& child_id) {
    for (parser::SacmElement& relationship : model.elements) {
        if (!IsRelationshipType(relationship.type))
            continue;

        if (relationship.type == "assertedinference") {
            if (relationship.reasoning_ref == child_id && ContainsValue(relationship.target_refs, parent_id))
                return IncomingRelationship{&relationship, true};
            if (relationship.reasoning_ref == parent_id && ContainsValue(relationship.source_refs, child_id))
                return IncomingRelationship{&relationship, false};
            if (relationship.reasoning_ref.empty() && ContainsValue(relationship.target_refs, parent_id) &&
                ContainsValue(relationship.source_refs, child_id)) {
                return IncomingRelationship{&relationship, false};
            }
        } else if (ContainsValue(relationship.target_refs, parent_id) &&
                   ContainsValue(relationship.source_refs, child_id)) {
            return IncomingRelationship{&relationship, false};
        }
    }
    return {};
}

ConstIncomingRelationship FindIncomingRelationship(const parser::AssuranceCase& model,
                                                   const std::string& parent_id,
                                                   const std::string& child_id) {
    for (const parser::SacmElement& relationship : model.elements) {
        if (!IsRelationshipType(relationship.type))
            continue;

        if (relationship.type == "assertedinference") {
            if (relationship.reasoning_ref == child_id && ContainsValue(relationship.target_refs, parent_id))
                return ConstIncomingRelationship{&relationship, true};
            if (relationship.reasoning_ref == parent_id && ContainsValue(relationship.source_refs, child_id))
                return ConstIncomingRelationship{&relationship, false};
            if (relationship.reasoning_ref.empty() && ContainsValue(relationship.target_refs, parent_id) &&
                ContainsValue(relationship.source_refs, child_id)) {
                return ConstIncomingRelationship{&relationship, false};
            }
        } else if (ContainsValue(relationship.target_refs, parent_id) &&
                   ContainsValue(relationship.source_refs, child_id)) {
            return ConstIncomingRelationship{&relationship, false};
        }
    }
    return {};
}

int RelationshipTypePriority(const std::string& type) {
    if (type == "assertedinference")
        return 0;
    if (type == "assertedcontext")
        return 1;
    if (type == "assertedevidence")
        return 2;
    return 3;
}

std::vector<std::string> ReorderIds(std::vector<std::string> order,
                                    const std::string& dragged_id,
                                    const std::string& target_id,
                                    TreeDropMode drop_mode) {
    RemoveValue(order, dragged_id);
    auto target = std::find(order.begin(), order.end(), target_id);
    if (target == order.end())
        return {};
    if (drop_mode == TreeDropMode::After)
        ++target;
    order.insert(target, dragged_id);
    return order;
}

void ApplySourceOrder(std::vector<std::string>& sources, const std::vector<std::string>& sibling_order) {
    std::unordered_map<std::string, size_t> position_by_id;
    for (size_t index = 0; index < sibling_order.size(); ++index)
        position_by_id.emplace(sibling_order[index], index);

    std::stable_sort(sources.begin(), sources.end(), [&](const std::string& lhs, const std::string& rhs) {
        const auto lhs_position = position_by_id.find(lhs);
        const auto rhs_position = position_by_id.find(rhs);
        const bool lhs_known = lhs_position != position_by_id.end();
        const bool rhs_known = rhs_position != position_by_id.end();
        if (lhs_known && rhs_known)
            return lhs_position->second < rhs_position->second;
        if (lhs_known != rhs_known)
            return lhs_known;
        return false;
    });
}

std::unordered_map<std::string, size_t> RelationshipPositionById(const parser::AssuranceCase& model,
                                                                 const std::string& parent_id,
                                                                 const std::vector<std::string>& sibling_order) {
    std::unordered_map<std::string, size_t> child_position;
    for (size_t index = 0; index < sibling_order.size(); ++index)
        child_position.emplace(sibling_order[index], index);

    std::unordered_map<std::string, size_t> relationship_position;
    for (const std::string& child_id : sibling_order) {
        ConstIncomingRelationship incoming = FindIncomingRelationship(model, parent_id, child_id);
        if (!incoming.relationship)
            continue;
        auto child_found = child_position.find(child_id);
        if (child_found == child_position.end())
            continue;
        auto inserted = relationship_position.emplace(incoming.relationship->id, child_found->second);
        if (!inserted.second && child_found->second < inserted.first->second)
            inserted.first->second = child_found->second;
    }
    return relationship_position;
}

void ApplyParserSiblingOrder(parser::AssuranceCase& model,
                             const std::string& parent_id,
                             const std::vector<std::string>& sibling_order) {
    const std::unordered_map<std::string, size_t> relationship_position =
        RelationshipPositionById(model, parent_id, sibling_order);

    for (parser::SacmElement& element : model.elements) {
        if (IsRelationshipType(element.type))
            ApplySourceOrder(element.source_refs, sibling_order);
    }

    std::stable_sort(model.elements.begin(),
                     model.elements.end(),
                     [&](const parser::SacmElement& lhs, const parser::SacmElement& rhs) {
                         const bool lhs_relationship = IsRelationshipType(lhs.type);
                         const bool rhs_relationship = IsRelationshipType(rhs.type);
                         if (lhs_relationship != rhs_relationship)
                             return false;
                         if (!lhs_relationship)
                             return false;

                         const int lhs_priority = RelationshipTypePriority(lhs.type);
                         const int rhs_priority = RelationshipTypePriority(rhs.type);
                         if (lhs_priority != rhs_priority)
                             return lhs_priority < rhs_priority;

                         const auto lhs_position = relationship_position.find(lhs.id);
                         const auto rhs_position = relationship_position.find(rhs.id);
                         const bool lhs_known = lhs_position != relationship_position.end();
                         const bool rhs_known = rhs_position != relationship_position.end();
                         if (lhs_known && rhs_known)
                             return lhs_position->second < rhs_position->second;
                         if (lhs_known != rhs_known)
                             return lhs_known;
                         return false;
                     });
}

template <typename RelationshipT>
void ApplySacmRelationshipOrder(std::vector<RelationshipT>& relationships,
                                const std::unordered_map<std::string, size_t>& relationship_position,
                                const std::vector<std::string>& sibling_order) {
    for (RelationshipT& relationship : relationships)
        ApplySourceOrder(relationship.sources, sibling_order);

    std::stable_sort(
        relationships.begin(), relationships.end(), [&](const RelationshipT& lhs, const RelationshipT& rhs) {
            const auto lhs_position = relationship_position.find(lhs.id);
            const auto rhs_position = relationship_position.find(rhs.id);
            const bool lhs_known = lhs_position != relationship_position.end();
            const bool rhs_known = rhs_position != relationship_position.end();
            if (lhs_known && rhs_known)
                return lhs_position->second < rhs_position->second;
            if (lhs_known != rhs_known)
                return lhs_known;
            return false;
        });
}

void ApplySacmSiblingOrder(sacm::AssuranceCasePackage* package,
                           const parser::AssuranceCase& model,
                           const std::string& parent_id,
                           const std::vector<std::string>& sibling_order) {
    if (!package)
        return;

    const std::unordered_map<std::string, size_t> relationship_position =
        RelationshipPositionById(model, parent_id, sibling_order);
    for (sacm::ArgumentPackage& argument_package : package->argumentPackages) {
        ApplySacmRelationshipOrder(argument_package.assertedInferences, relationship_position, sibling_order);
        ApplySacmRelationshipOrder(argument_package.assertedContexts, relationship_position, sibling_order);
        ApplySacmRelationshipOrder(argument_package.assertedEvidences, relationship_position, sibling_order);
    }
}

sacm::ArgumentPackage* FindOwningArgumentPackage(sacm::AssuranceCasePackage* package, const std::string& element_id) {
    if (!package)
        return nullptr;
    for (sacm::ArgumentPackage& argument_package : package->argumentPackages) {
        for (const sacm::Claim& claim : argument_package.claims) {
            if (claim.id == element_id)
                return &argument_package;
        }
        for (const sacm::ArgumentReasoning& reasoning : argument_package.argumentReasonings) {
            if (reasoning.id == element_id)
                return &argument_package;
        }
        for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
            if (artifact_reference.id == element_id)
                return &argument_package;
        }
    }
    if (package->argumentPackages.empty())
        package->argumentPackages.emplace_back();
    return &package->argumentPackages.front();
}

void RemoveSourceFromSacmRelationship(sacm::AssuranceCasePackage* package,
                                      const std::string& relationship_id,
                                      const std::string& source_id) {
    if (!package || relationship_id.empty())
        return;
    for (sacm::ArgumentPackage& argument_package : package->argumentPackages) {
        for (sacm::AssertedInference& relationship : argument_package.assertedInferences) {
            if (relationship.id == relationship_id)
                RemoveValue(relationship.sources, source_id);
        }
        for (sacm::AssertedContext& relationship : argument_package.assertedContexts) {
            if (relationship.id == relationship_id)
                RemoveValue(relationship.sources, source_id);
        }
        for (sacm::AssertedEvidence& relationship : argument_package.assertedEvidences) {
            if (relationship.id == relationship_id)
                RemoveValue(relationship.sources, source_id);
        }

        argument_package.assertedInferences.erase(std::remove_if(argument_package.assertedInferences.begin(),
                                                                 argument_package.assertedInferences.end(),
                                                                 [](const sacm::AssertedInference& relationship) {
                                                                     return IsSacmInferenceDangling(relationship);
                                                                 }),
                                                  argument_package.assertedInferences.end());
        argument_package.assertedContexts.erase(std::remove_if(argument_package.assertedContexts.begin(),
                                                               argument_package.assertedContexts.end(),
                                                               [](const sacm::AssertedContext& relationship) {
                                                                   return IsSacmRelationshipDangling(relationship);
                                                               }),
                                                argument_package.assertedContexts.end());
        argument_package.assertedEvidences.erase(std::remove_if(argument_package.assertedEvidences.begin(),
                                                                argument_package.assertedEvidences.end(),
                                                                [](const sacm::AssertedEvidence& relationship) {
                                                                    return IsSacmRelationshipDangling(relationship);
                                                                }),
                                                 argument_package.assertedEvidences.end());
    }
}

void RetargetSacmRelationship(sacm::AssuranceCasePackage* package,
                              const std::string& relationship_id,
                              const std::string& old_parent_id,
                              const std::string& new_parent_id) {
    if (!package || relationship_id.empty())
        return;
    auto retarget = [&](std::vector<std::string>& targets) {
        for (std::string& target : targets) {
            if (target == old_parent_id)
                target = new_parent_id;
        }
    };
    for (sacm::ArgumentPackage& argument_package : package->argumentPackages) {
        for (sacm::AssertedInference& relationship : argument_package.assertedInferences) {
            if (relationship.id == relationship_id)
                retarget(relationship.targets);
        }
        for (sacm::AssertedContext& relationship : argument_package.assertedContexts) {
            if (relationship.id == relationship_id)
                retarget(relationship.targets);
        }
        for (sacm::AssertedEvidence& relationship : argument_package.assertedEvidences) {
            if (relationship.id == relationship_id)
                retarget(relationship.targets);
        }
    }
}

void AddSacmRelationship(sacm::AssuranceCasePackage* package,
                         const std::string& relationship_id,
                         const std::string& parent_id,
                         const std::string& child_id,
                         TreeRelationshipKind kind,
                         bool child_is_strategy) {
    sacm::ArgumentPackage* argument_package = FindOwningArgumentPackage(package, parent_id);
    if (!argument_package)
        return;

    if (kind == TreeRelationshipKind::SupportedBy) {
        sacm::AssertedInference relationship;
        relationship.id = relationship_id;
        relationship.targets.push_back(parent_id);
        if (child_is_strategy)
            relationship.reasoning = child_id;
        else
            relationship.sources.push_back(child_id);
        argument_package->assertedInferences.push_back(std::move(relationship));
    } else if (kind == TreeRelationshipKind::InContextOf) {
        sacm::AssertedContext relationship;
        relationship.id = relationship_id;
        relationship.targets.push_back(parent_id);
        relationship.sources.push_back(child_id);
        argument_package->assertedContexts.push_back(std::move(relationship));
    } else if (kind == TreeRelationshipKind::AssertedEvidence) {
        sacm::AssertedEvidence relationship;
        relationship.id = relationship_id;
        relationship.targets.push_back(parent_id);
        relationship.sources.push_back(child_id);
        argument_package->assertedEvidences.push_back(std::move(relationship));
    }
}

std::vector<std::string> CurrentSiblingOrder(const TreeNode* parent, ElementGroup group) {
    std::vector<std::string> order;
    if (!parent)
        return order;
    const std::vector<TreeNode*>& children =
        group == ElementGroup::Group1 ? parent->group1_children : parent->group2_attachments;
    order.reserve(children.size());
    for (const TreeNode* child : children)
        order.push_back(child->id);
    return order;
}

void ApplyDisplayOrderToChildren(TreeNode* node, const TreeDisplayOrder& display_order) {
    if (!node)
        return;

    auto apply_to_group = [&](std::vector<TreeNode*>& children) {
        auto found = display_order.children_by_parent.find(node->id);
        if (found == display_order.children_by_parent.end())
            return;

        std::unordered_map<std::string, size_t> position_by_id;
        for (size_t index = 0; index < found->second.size(); ++index)
            position_by_id.emplace(found->second[index], index);

        std::stable_sort(children.begin(), children.end(), [&](const TreeNode* lhs, const TreeNode* rhs) {
            const auto lhs_position = position_by_id.find(lhs->id);
            const auto rhs_position = position_by_id.find(rhs->id);
            const bool lhs_known = lhs_position != position_by_id.end();
            const bool rhs_known = rhs_position != position_by_id.end();
            if (lhs_known && rhs_known)
                return lhs_position->second < rhs_position->second;
            if (lhs_known != rhs_known)
                return lhs_known;
            return false;
        });
    };

    apply_to_group(node->group1_children);
    apply_to_group(node->group2_attachments);

    for (TreeNode* child : node->group1_children)
        ApplyDisplayOrderToChildren(child, display_order);
    for (TreeNode* attachment : node->group2_attachments)
        ApplyDisplayOrderToChildren(attachment, display_order);
}

} // namespace

TreeDropValidationResult ValidateTreeDrop(const parser::AssuranceCase& model,
                                          const AssuranceTree& tree,
                                          const std::string& dragged_element_id,
                                          const std::string& target_element_id,
                                          TreeDropMode drop_mode) {
    TreeDropValidationResult result;

    if (dragged_element_id.empty() || target_element_id.empty()) {
        result.reason = "Drop is missing an element id.";
        return result;
    }
    if (dragged_element_id == target_element_id) {
        result.reason = "Cannot move an element under itself.";
        return result;
    }
    if (!FindElement(model, dragged_element_id) || !FindElement(model, target_element_id)) {
        result.reason = "Drop references an element that is not in the model.";
        return result;
    }

    const TreeNode* dragged_node = FindTreeNode(tree, dragged_element_id);
    const TreeNode* target_node = FindTreeNode(tree, target_element_id);
    if (!dragged_node || !target_node) {
        result.reason = "Drop references an element that is not visible in the tree.";
        return result;
    }

    const TreeEditIndex index = BuildTreeEditIndex(model);
    if (index.parent_counts.count(dragged_element_id) == 0) {
        result.reason = "Root or orphan elements cannot be moved in the tree yet.";
        return result;
    }
    if (index.parent_counts.at(dragged_element_id) != 1) {
        result.reason = "This element has multiple parents and cannot be moved in the tree yet.";
        return result;
    }
    auto target_parent_count = index.parent_counts.find(target_element_id);
    if (target_parent_count != index.parent_counts.end() && target_parent_count->second > 1) {
        result.reason = "The target element has multiple parents and cannot be used as a drop target yet.";
        return result;
    }

    if (drop_mode == TreeDropMode::Before || drop_mode == TreeDropMode::After) {
        if (dragged_node->parent != target_node->parent || !dragged_node->parent) {
            result.reason = "Only same-parent reorder is currently supported for above/below drops.";
            return result;
        }
        if (dragged_node->group != target_node->group) {
            result.reason = "Only siblings in the same tree group can be reordered.";
            return result;
        }
        result.allowed = true;
        result.relationship_kind = TreeRelationshipKind::None;
        result.changes_semantic_relationship = false;
        return result;
    }

    if (IsDescendant(dragged_node, target_element_id)) {
        result.reason = "Cannot move an element into its own subtree.";
        return result;
    }
    if (HasGraphPath(index, dragged_element_id, target_element_id)) {
        result.reason = "Cannot create a cycle in the argument graph.";
        return result;
    }

    const TreeRelationshipKind kind = DetermineRelationshipKind(target_node->role, dragged_node->role);
    if (kind == TreeRelationshipKind::None) {
        result.reason = "Cannot move this element under the selected target type.";
        return result;
    }

    result.allowed = true;
    result.relationship_kind = kind;
    result.changes_semantic_relationship = true;
    return result;
}

bool ReorderSiblings(parser::AssuranceCase& model,
                     sacm::AssuranceCasePackage* package,
                     const AssuranceTree& tree,
                     TreeDisplayOrder& display_order,
                     const ReorderSiblingsCommand& command,
                     std::string& out_error) {
    out_error.clear();
    TreeDropValidationResult validation =
        ValidateTreeDrop(model, tree, command.dragged_element_id, command.target_element_id, command.drop_mode);
    if (!validation.allowed) {
        out_error = validation.reason;
        return false;
    }

    const TreeNode* dragged_node = FindTreeNode(tree, command.dragged_element_id);
    const TreeNode* parent = dragged_node ? dragged_node->parent : nullptr;
    if (!parent) {
        out_error = "Dragged element has no parent.";
        return false;
    }

    std::vector<std::string> order = ReorderIds(CurrentSiblingOrder(parent, dragged_node->group),
                                                command.dragged_element_id,
                                                command.target_element_id,
                                                command.drop_mode);
    if (order.empty()) {
        out_error = "Target sibling was not found in the current order.";
        return false;
    }
    ApplyParserSiblingOrder(model, parent->id, order);
    ApplySacmSiblingOrder(package, model, parent->id, order);
    display_order.children_by_parent[parent->id] = order;
    return true;
}

bool MoveSubtree(parser::AssuranceCase& model,
                 sacm::AssuranceCasePackage* package,
                 const AssuranceTree& tree,
                 const MoveSubtreeCommand& command,
                 std::string& out_error) {
    out_error.clear();
    TreeDropValidationResult validation =
        ValidateTreeDrop(model, tree, command.dragged_element_id, command.new_parent_element_id, TreeDropMode::AsChild);
    if (!validation.allowed) {
        out_error = validation.reason;
        return false;
    }

    const TreeNode* dragged_node = FindTreeNode(tree, command.dragged_element_id);
    if (!dragged_node || !dragged_node->parent) {
        out_error = "Dragged element has no old parent.";
        return false;
    }
    const std::string old_parent_id = dragged_node->parent->id;
    IncomingRelationship incoming = FindIncomingRelationship(model, old_parent_id, command.dragged_element_id);
    if (!incoming.relationship) {
        out_error = "Could not find the relationship that connects the moved element to its old parent.";
        return false;
    }

    const std::string old_relationship_id = incoming.relationship->id;
    if (incoming.child_is_reasoning && validation.relationship_kind == TreeRelationshipKind::SupportedBy) {
        for (std::string& target : incoming.relationship->target_refs) {
            if (target == old_parent_id)
                target = command.new_parent_element_id;
        }
        RetargetSacmRelationship(package, old_relationship_id, old_parent_id, command.new_parent_element_id);
        return true;
    }

    const std::string new_relationship_id = GenerateRelationshipId(model);

    RemoveValue(incoming.relationship->source_refs, command.dragged_element_id);
    RemoveSourceFromSacmRelationship(package, old_relationship_id, command.dragged_element_id);
    model.elements.erase(
        std::remove_if(model.elements.begin(),
                       model.elements.end(),
                       [](const parser::SacmElement& element) { return IsParserRelationshipDangling(element); }),
        model.elements.end());

    parser::SacmElement new_relationship;
    new_relationship.id = new_relationship_id;
    new_relationship.target_refs.push_back(command.new_parent_element_id);
    if (validation.relationship_kind == TreeRelationshipKind::SupportedBy) {
        new_relationship.type = "assertedinference";
        if (dragged_node->role == NodeRole::Strategy)
            new_relationship.reasoning_ref = command.dragged_element_id;
        else
            new_relationship.source_refs.push_back(command.dragged_element_id);
    } else if (validation.relationship_kind == TreeRelationshipKind::InContextOf) {
        new_relationship.type = "assertedcontext";
        new_relationship.source_refs.push_back(command.dragged_element_id);
    } else if (validation.relationship_kind == TreeRelationshipKind::AssertedEvidence) {
        new_relationship.type = "assertedevidence";
        new_relationship.source_refs.push_back(command.dragged_element_id);
    } else {
        out_error = "Unsupported tree relationship kind.";
        return false;
    }

    AddSacmRelationship(package,
                        new_relationship.id,
                        command.new_parent_element_id,
                        command.dragged_element_id,
                        validation.relationship_kind,
                        dragged_node->role == NodeRole::Strategy);
    model.elements.push_back(std::move(new_relationship));
    return true;
}

void ApplyTreeDisplayOrder(AssuranceTree& tree, const TreeDisplayOrder& display_order) {
    ApplyDisplayOrderToChildren(tree.root, display_order);
    for (TreeNode* orphan : tree.orphans)
        ApplyDisplayOrderToChildren(orphan, display_order);
}

} // namespace core
