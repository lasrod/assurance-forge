#include "core/problems/argument_cycles.h"

#include "core/string_utils.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace core {
namespace {

bool IsSupportRelationship(const parser::SacmElement& element) {
    // A challenge is not support. Following one would make every dialectic
    // argument look circular: a counter-claim points back at what it attacks.
    if (element.is_counter)
        return false;
    return element.type == "assertedinference" || element.type == "assertedevidence";
}

// parent -> the elements that support it, sorted and deduplicated so traversal
// order does not depend on document order.
using SupportGraph = std::map<std::string, std::vector<std::string>>;

void AddSupportEdge(SupportGraph& graph, const std::string& parent, const std::string& child) {
    // A self-edge (parent == child) is deliberately kept: an element that
    // supports itself is a cycle of one, and exactly the defect this looks for.
    if (parent.empty() || child.empty())
        return;
    graph[parent].push_back(child);
}

SupportGraph BuildSupportGraph(const parser::AssuranceCase& model) {
    std::unordered_set<std::string> known_elements;
    for (const parser::SacmElement& element : model.elements) {
        if (!element.id.empty())
            known_elements.insert(element.id);
    }

    SupportGraph graph;
    for (const parser::SacmElement& relationship : model.elements) {
        if (!IsSupportRelationship(relationship))
            continue;
        for (const std::string& raw_target : relationship.target_refs) {
            const std::string target = NormalizeRef(raw_target);
            if (known_elements.find(target) == known_elements.end())
                continue;

            // A strategy sits between the conclusion and its premises in GSN, so
            // route through it: the reported cycle then names the strategy the
            // author would have to break, not just the two goals.
            std::string from = target;
            const std::string reasoning = NormalizeRef(relationship.reasoning_ref);
            if (!reasoning.empty() && known_elements.find(reasoning) != known_elements.end()) {
                AddSupportEdge(graph, target, reasoning);
                from = reasoning;
            }

            for (const std::string& raw_source : relationship.source_refs) {
                const std::string source = NormalizeRef(raw_source);
                if (known_elements.find(source) == known_elements.end())
                    continue;
                AddSupportEdge(graph, from, source);
            }
        }
    }

    for (auto& entry : graph) {
        std::sort(entry.second.begin(), entry.second.end());
        entry.second.erase(std::unique(entry.second.begin(), entry.second.end()), entry.second.end());
    }
    return graph;
}

// Rotates the cycle so its smallest id comes first. The same loop entered from
// a different node then produces the same key, which is what makes
// deduplication possible at all.
std::string CanonicalKey(const std::vector<std::string>& cycle) {
    const size_t smallest = static_cast<size_t>(
        std::min_element(cycle.begin(), cycle.end()) - cycle.begin());
    std::string key;
    for (size_t offset = 0; offset < cycle.size(); ++offset) {
        key += cycle[(smallest + offset) % cycle.size()];
        key.push_back('\x1f');
    }
    return key;
}

std::vector<std::string> RotateToSmallest(const std::vector<std::string>& cycle) {
    const size_t smallest = static_cast<size_t>(
        std::min_element(cycle.begin(), cycle.end()) - cycle.begin());
    std::vector<std::string> rotated;
    rotated.reserve(cycle.size());
    for (size_t offset = 0; offset < cycle.size(); ++offset)
        rotated.push_back(cycle[(smallest + offset) % cycle.size()]);
    return rotated;
}

enum class VisitState { Unvisited, OnStack, Done };

// One frame of the explicit DFS stack. Recursion would be simpler to read but
// its depth is bounded only by the element count, and this runs on files the
// tool did not author.
struct Frame {
    std::string node;
    size_t next_child = 0;
};

void CollectCyclesFrom(const std::string& root,
                       const SupportGraph& graph,
                       std::unordered_map<std::string, VisitState>& state,
                       std::set<std::string>& seen_keys,
                       std::vector<ArgumentCycle>& cycles) {
    if (state[root] != VisitState::Unvisited)
        return;

    std::vector<Frame> stack;
    std::vector<std::string> path;
    std::unordered_map<std::string, size_t> path_index;

    stack.push_back(Frame{root, 0});
    state[root] = VisitState::OnStack;
    path.push_back(root);
    path_index[root] = 0;

    while (!stack.empty()) {
        Frame& frame = stack.back();
        const auto children = graph.find(frame.node);
        const bool has_children = children != graph.end();

        if (has_children && frame.next_child < children->second.size()) {
            const std::string& child = children->second[frame.next_child];
            ++frame.next_child;

            const VisitState child_state = state[child];
            if (child_state == VisitState::OnStack) {
                // Back edge: everything from the child's position to the top of
                // the path is a cycle.
                const std::vector<std::string> cycle(path.begin() + static_cast<long>(path_index[child]),
                                                     path.end());
                const std::string key = CanonicalKey(cycle);
                if (seen_keys.insert(key).second)
                    cycles.push_back(ArgumentCycle{RotateToSmallest(cycle)});
                continue;
            }
            if (child_state == VisitState::Done)
                continue;

            state[child] = VisitState::OnStack;
            path_index[child] = path.size();
            path.push_back(child);
            stack.push_back(Frame{child, 0});
            continue;
        }

        state[frame.node] = VisitState::Done;
        path_index.erase(frame.node);
        path.pop_back();
        stack.pop_back();
    }
}

} // namespace

std::vector<ArgumentCycle> FindSupportCycles(const parser::AssuranceCase& model) {
    const SupportGraph graph = BuildSupportGraph(model);

    std::unordered_map<std::string, VisitState> state;
    std::set<std::string> seen_keys;
    std::vector<ArgumentCycle> cycles;

    // Iterate the graph's own (sorted) key order so the result is deterministic
    // regardless of document order.
    for (const auto& entry : graph)
        CollectCyclesFrom(entry.first, graph, state, seen_keys, cycles);

    std::sort(cycles.begin(), cycles.end(), [](const ArgumentCycle& a, const ArgumentCycle& b) {
        return a.element_ids < b.element_ids;
    });
    return cycles;
}

} // namespace core
