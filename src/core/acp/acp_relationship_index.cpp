#include "core/acp/acp_relationship_index.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace core::acp {
namespace {

constexpr const char* kStrategyChildBlockedReason =
    "ACP on a Strategy -> Goal edge is ambiguous in GSN. The confidence argument should apply to the whole inference "
    "through the strategy. Add ACP to the parent Goal -> Strategy relationship instead?";
constexpr const char* kUnsupportedRelationshipBlockedReason =
    "ACP is only supported for GSN SupportedBy and InContextOf relationships between the eligible element types.";

bool IsRelationshipType(const std::string& type) {
    return type == "assertedinference" || type == "assertedcontext" || type == "assertedevidence";
}

std::unordered_map<std::string, const parser::SacmElement*> BuildElementLookup(const parser::AssuranceCase& model) {
    std::unordered_map<std::string, const parser::SacmElement*> elements;
    for (const parser::SacmElement& element : model.elements) {
        if (!element.id.empty() && !IsRelationshipType(element.type))
            elements[element.id] = &element;
    }
    return elements;
}

std::string DisplayId(const std::string& id) {
    return id.empty() ? "?" : id;
}

std::string ElementKind(const std::unordered_map<std::string, const parser::SacmElement*>& elements,
                        const std::string& id) {
    auto found = elements.find(id);
    if (found == elements.end())
        return {};
    const parser::SacmElement& element = *found->second;
    if (element.type == "argumentreasoning")
        return "Strategy";
    if (element.type == "artifactreference")
        return "Solution";
    if (element.type == "claim") {
        if (element.assertion_declaration == "assumed")
            return "Assumption";
        if (element.assertion_declaration == "justification")
            return "Justification";
        return "Goal";
    }
    return element.type;
}

bool IsGoal(const std::unordered_map<std::string, const parser::SacmElement*>& elements, const std::string& id) {
    return ElementKind(elements, id) == "Goal";
}

bool IsStrategy(const std::unordered_map<std::string, const parser::SacmElement*>& elements, const std::string& id) {
    return ElementKind(elements, id) == "Strategy";
}

bool IsSolution(const std::unordered_map<std::string, const parser::SacmElement*>& elements, const std::string& id) {
    return ElementKind(elements, id) == "Solution";
}

bool IsContextAttachment(const std::unordered_map<std::string, const parser::SacmElement*>& elements,
                         const std::string& id) {
    const std::string kind = ElementKind(elements, id);
    return kind == "Solution" || kind == "Assumption" || kind == "Justification";
}

std::string RelationshipSummary(AcpRelationshipKind kind,
                                const std::string& parent_id,
                                const std::string& child_id,
                                const std::string& reasoning_id,
                                bool strategy_child_edge) {
    if (kind == AcpRelationshipKind::InContextOf) {
        return "InContextOf relationship " + DisplayId(parent_id) + " -> " + DisplayId(child_id);
    }
    if (kind == AcpRelationshipKind::AssertedEvidence) {
        return "Evidential support " + DisplayId(parent_id) + " -> " + DisplayId(child_id);
    }
    if (!reasoning_id.empty() && !strategy_child_edge) {
        return "SupportedBy inference " + DisplayId(parent_id) + " -> " + DisplayId(reasoning_id);
    }
    if (strategy_child_edge) {
        return "Strategy child support " + DisplayId(parent_id) + " -> " + DisplayId(child_id);
    }
    return "SupportedBy relationship " + DisplayId(parent_id) + " -> " + DisplayId(child_id);
}

void AddTarget(std::vector<AcpRelationshipTarget>& targets,
               AcpRelationshipKind kind,
               const parser::SacmElement& relationship,
               std::string parent_id,
               std::string child_id,
               std::string source_id,
               std::string target_id,
               bool strategy_child_edge,
               bool eligible_for_acp,
               std::string blocked_reason = {}) {
    AcpRelationshipTarget target;
    target.relationship_id = relationship.id;
    target.kind = kind;
    target.parent_id = std::move(parent_id);
    target.child_id = std::move(child_id);
    target.source_id = std::move(source_id);
    target.target_id = std::move(target_id);
    target.reasoning_id = relationship.reasoning_ref;
    target.strategy_child_edge = strategy_child_edge;
    target.eligible_for_acp = eligible_for_acp;
    if (strategy_child_edge)
        target.blocked_reason = kStrategyChildBlockedReason;
    else if (!eligible_for_acp)
        target.blocked_reason = blocked_reason.empty() ? kUnsupportedRelationshipBlockedReason : std::move(blocked_reason);
    target.summary = RelationshipSummary(kind, target.parent_id, target.child_id, target.reasoning_id, strategy_child_edge);
    targets.push_back(std::move(target));
}

} // namespace

const char* ToString(AcpRelationshipKind kind) {
    switch (kind) {
    case AcpRelationshipKind::SupportedBy:
        return "SupportedBy";
    case AcpRelationshipKind::InContextOf:
        return "InContextOf";
    case AcpRelationshipKind::AssertedEvidence:
        return "AssertedEvidence";
    }
    return "SupportedBy";
}

std::vector<AcpRelationshipTarget> BuildAcpRelationshipTargets(const parser::AssuranceCase& model) {
    std::vector<AcpRelationshipTarget> targets;
    const auto elements = BuildElementLookup(model);

    for (const parser::SacmElement& relationship : model.elements) {
        if (relationship.target_refs.empty())
            continue;

        if (relationship.type == "assertedinference") {
            for (const std::string& target_id : relationship.target_refs) {
                if (!relationship.reasoning_ref.empty()) {
                    const bool goal_to_strategy = IsGoal(elements, target_id) && IsStrategy(elements, relationship.reasoning_ref);
                    AddTarget(targets,
                              AcpRelationshipKind::SupportedBy,
                              relationship,
                              target_id,
                              relationship.reasoning_ref,
                              relationship.reasoning_ref,
                              target_id,
                              false,
                              goal_to_strategy);
                    for (const std::string& source_id : relationship.source_refs) {
                        const bool strategy_to_goal = IsStrategy(elements, relationship.reasoning_ref) && IsGoal(elements, source_id);
                        AddTarget(targets,
                                  AcpRelationshipKind::SupportedBy,
                                  relationship,
                                  relationship.reasoning_ref,
                                  source_id,
                                  source_id,
                                  target_id,
                                  strategy_to_goal,
                                  false,
                                  strategy_to_goal ? kStrategyChildBlockedReason : kUnsupportedRelationshipBlockedReason);
                    }
                } else {
                    for (const std::string& source_id : relationship.source_refs) {
                        const bool goal_to_goal = IsGoal(elements, target_id) && IsGoal(elements, source_id);
                        const bool goal_to_strategy = IsGoal(elements, target_id) && IsStrategy(elements, source_id);
                        const bool goal_to_solution = IsGoal(elements, target_id) && IsSolution(elements, source_id);
                        AddTarget(targets,
                                  AcpRelationshipKind::SupportedBy,
                                  relationship,
                                  target_id,
                                  source_id,
                                  source_id,
                                  target_id,
                                  false,
                                  goal_to_goal || goal_to_strategy || goal_to_solution);
                    }
                }
            }
        } else if (relationship.type == "assertedcontext") {
            for (const std::string& target_id : relationship.target_refs) {
                for (const std::string& source_id : relationship.source_refs) {
                    const bool eligible_context = (IsGoal(elements, target_id) || IsStrategy(elements, target_id)) &&
                                                  IsContextAttachment(elements, source_id);
                    AddTarget(targets,
                              AcpRelationshipKind::InContextOf,
                              relationship,
                              target_id,
                              source_id,
                              source_id,
                              target_id,
                              false,
                              eligible_context);
                }
            }
        } else if (relationship.type == "assertedevidence") {
            for (const std::string& target_id : relationship.target_refs) {
                for (const std::string& source_id : relationship.source_refs) {
                    const bool goal_to_solution = IsGoal(elements, target_id) && IsSolution(elements, source_id);
                    AddTarget(targets,
                              AcpRelationshipKind::AssertedEvidence,
                              relationship,
                              target_id,
                              source_id,
                              source_id,
                              target_id,
                              false,
                              goal_to_solution);
                }
            }
        }
    }
    return targets;
}

const AcpRelationshipTarget* FindAcpRelationshipTarget(const std::vector<AcpRelationshipTarget>& targets,
                                                       const std::string& parent_id,
                                                       const std::string& child_id) {
    auto found = std::find_if(targets.begin(), targets.end(), [&](const AcpRelationshipTarget& target) {
        return target.parent_id == parent_id && target.child_id == child_id;
    });
    return found == targets.end() ? nullptr : &*found;
}

} // namespace core::acp