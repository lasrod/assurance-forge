#include "core/drafts/draft_dependency_graph.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace core::drafts {
namespace {

void CollectExistingRef(const std::optional<reviews::ElementRef>& ref, std::unordered_set<std::string>& into) {
    if (ref.has_value() && ref->existing_id.has_value() && !ref->existing_id->empty())
        into.insert(ref->existing_id.value());
}

// Every element id a group refers to by *existing* id.
//
// A `create_ref` is patch-local and resolved within the group, so it can never
// create a cross-group dependency; only a reference to an already-existing id
// can, and that id may have been brought into existence by an earlier group.
std::unordered_set<std::string> ReferencedExistingIds(const DraftChangeGroup& group) {
    std::unordered_set<std::string> ids;
    for (const reviews::PatchOperation& operation : group.operations) {
        CollectExistingRef(operation.element, ids);
        CollectExistingRef(operation.source, ids);
        CollectExistingRef(operation.target, ids);
    }
    return ids;
}

void AddUnique(std::vector<std::string>& values, const std::string& value) {
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(value);
}

std::vector<std::string> InSequenceOrder(const DraftWorkspace& workspace, const std::unordered_set<std::string>& ids) {
    std::vector<std::string> ordered;
    for (const DraftChangeGroup* group : workspace.ActiveGroups()) {
        if (ids.count(group->id) > 0)
            ordered.push_back(group->id);
    }
    return ordered;
}

} // namespace

std::map<std::string, std::vector<std::string>> InferDraftDependencies(const DraftWorkspace& workspace) {
    // element id -> the group that created it.
    std::unordered_map<std::string, std::string> creator_of;
    for (const DraftChangeGroup& group : workspace.groups) {
        if (!group.active())
            continue;
        for (const auto& [create_ref, element_id] : group.generated_ids)
            creator_of.emplace(element_id, group.id);
    }

    std::map<std::string, std::vector<std::string>> dependencies;
    for (const DraftChangeGroup& group : workspace.groups) {
        if (!group.active())
            continue;
        std::vector<std::string>& depends_on = dependencies[group.id];
        // What the source told us, kept: it may know a reason identity cannot
        // show, such as one change only making sense after another.
        for (const std::string& declared : group.depends_on_group_ids)
            AddUnique(depends_on, declared);

        for (const std::string& referenced : ReferencedExistingIds(group)) {
            auto creator = creator_of.find(referenced);
            if (creator == creator_of.end() || creator->second == group.id)
                continue;
            AddUnique(depends_on, creator->second);
        }

        const std::unordered_set<std::string> as_set(depends_on.begin(), depends_on.end());
        depends_on = InSequenceOrder(workspace, as_set);
    }
    return dependencies;
}

void ApplyInferredDependencies(DraftWorkspace& workspace) {
    const std::map<std::string, std::vector<std::string>> inferred = InferDraftDependencies(workspace);
    for (DraftChangeGroup& group : workspace.groups) {
        auto found = inferred.find(group.id);
        if (found != inferred.end())
            group.depends_on_group_ids = found->second;
    }
}

std::vector<std::string> DependencyClosure(const DraftWorkspace& workspace, const std::vector<std::string>& selection) {
    const std::map<std::string, std::vector<std::string>> dependencies = InferDraftDependencies(workspace);

    std::unordered_set<std::string> closed;
    std::vector<std::string> pending = selection;
    while (!pending.empty()) {
        const std::string current = pending.back();
        pending.pop_back();
        if (!closed.insert(current).second)
            continue;
        auto found = dependencies.find(current);
        if (found == dependencies.end())
            continue;
        for (const std::string& dependency : found->second) {
            if (closed.count(dependency) == 0)
                pending.push_back(dependency);
        }
    }
    return InSequenceOrder(workspace, closed);
}

std::vector<std::string> DependentsOf(const DraftWorkspace& workspace, const std::vector<std::string>& selection) {
    const std::map<std::string, std::vector<std::string>> dependencies = InferDraftDependencies(workspace);

    std::unordered_set<std::string> rejected(selection.begin(), selection.end());
    // Repeated to a fixpoint: a group that depends on a dependent is stranded
    // too, and one pass would miss it.
    bool grew = true;
    while (grew) {
        grew = false;
        for (const auto& [group_id, depends_on] : dependencies) {
            if (rejected.count(group_id) > 0)
                continue;
            for (const std::string& dependency : depends_on) {
                if (rejected.count(dependency) > 0) {
                    rejected.insert(group_id);
                    grew = true;
                    break;
                }
            }
        }
    }
    for (const std::string& selected : selection)
        rejected.erase(selected);
    return InSequenceOrder(workspace, rejected);
}

} // namespace core::drafts
