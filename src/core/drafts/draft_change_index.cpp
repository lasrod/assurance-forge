#include "core/drafts/draft_change_index.h"

#include "core/reviews/review_proposal.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace core::drafts {
namespace {

// id -> semantic hash, so "modified" means the element actually says something
// different rather than merely having been touched.
std::unordered_map<std::string, std::string> HashById(const core::AssuranceCase& model) {
    std::unordered_map<std::string, std::string> hashes;
    hashes.reserve(model.elements.size() * 2);
    for (const core::SacmElement& element : model.elements) {
        if (element.id.empty())
            continue;
        hashes.emplace(element.id, reviews::ComputeElementSemanticHash(element));
    }
    return hashes;
}

const core::SacmElement* FindElement(const core::AssuranceCase& model, const std::string& id) {
    for (const core::SacmElement& element : model.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

void AddContribution(DraftChangeIndex& index,
                     const std::string& element_id,
                     const std::string& group_id,
                     DraftElementChange change) {
    DraftElementEntry& entry = index.elements[element_id];
    entry.contributions.push_back(DraftElementContribution{group_id, change});
}

} // namespace

const char* DraftElementChangeToString(DraftElementChange change) {
    switch (change) {
    case DraftElementChange::Added:
        return "added";
    case DraftElementChange::Modified:
        return "modified";
    case DraftElementChange::Removed:
        return "removed";
    case DraftElementChange::Unchanged:
        break;
    }
    return "unchanged";
}

const DraftElementEntry* DraftChangeIndex::Find(const std::string& element_id) const {
    auto found = elements.find(element_id);
    return found == elements.end() ? nullptr : &found->second;
}

std::vector<std::string> DraftChangeIndex::ChangedElementIds() const {
    std::vector<std::string> ids;
    ids.reserve(elements.size());
    for (const auto& [element_id, entry] : elements) {
        if (entry.change != DraftElementChange::Unchanged)
            ids.push_back(element_id);
    }
    // `elements` is a std::map, so this is already sorted; sorting again costs
    // nothing and stops the guarantee depending on the container choice.
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::string> DraftChangeIndex::ContributingGroupIds(const std::string& element_id) const {
    std::vector<std::string> group_ids;
    const DraftElementEntry* entry = Find(element_id);
    if (entry == nullptr)
        return group_ids;
    for (const DraftElementContribution& contribution : entry->contributions) {
        if (std::find(group_ids.begin(), group_ids.end(), contribution.group_id) == group_ids.end())
            group_ids.push_back(contribution.group_id);
    }
    return group_ids;
}

void RecordGroupContributions(DraftChangeIndex& index,
                              const std::string& group_id,
                              const core::AssuranceCase& before,
                              const core::AssuranceCase& after) {
    const std::unordered_map<std::string, std::string> before_hashes = HashById(before);
    const std::unordered_map<std::string, std::string> after_hashes = HashById(after);

    for (const auto& [element_id, after_hash] : after_hashes) {
        auto previous = before_hashes.find(element_id);
        if (previous == before_hashes.end()) {
            AddContribution(index, element_id, group_id, DraftElementChange::Added);
        } else if (previous->second != after_hash) {
            AddContribution(index, element_id, group_id, DraftElementChange::Modified);
        }
    }

    for (const auto& [element_id, before_hash] : before_hashes) {
        if (after_hashes.find(element_id) == after_hashes.end())
            AddContribution(index, element_id, group_id, DraftElementChange::Removed);
    }
}

void FinalizeChangeIndex(DraftChangeIndex& index,
                         const core::AssuranceCase& accepted,
                         const core::AssuranceCase& working) {
    const std::unordered_map<std::string, std::string> accepted_hashes = HashById(accepted);
    const std::unordered_map<std::string, std::string> working_hashes = HashById(working);

    index.removed.clear();
    index.added_count = 0;
    index.modified_count = 0;
    index.removed_count = 0;

    for (const auto& [element_id, working_hash] : working_hashes) {
        auto baseline = accepted_hashes.find(element_id);
        DraftElementChange change = DraftElementChange::Unchanged;
        if (baseline == accepted_hashes.end()) {
            change = DraftElementChange::Added;
        } else if (baseline->second != working_hash) {
            change = DraftElementChange::Modified;
        }
        if (change == DraftElementChange::Unchanged) {
            // A group may have touched it and a later group put it back. The
            // contributions stay, because the history is real; the net effect is
            // that the accepted argument is unaffected.
            auto entry = index.elements.find(element_id);
            if (entry != index.elements.end())
                entry->second.change = DraftElementChange::Unchanged;
            continue;
        }
        index.elements[element_id].change = change;
        if (change == DraftElementChange::Added) {
            ++index.added_count;
        } else {
            ++index.modified_count;
        }
    }

    for (const auto& [element_id, accepted_hash] : accepted_hashes) {
        if (working_hashes.find(element_id) != working_hashes.end())
            continue;
        index.elements[element_id].change = DraftElementChange::Removed;
        ++index.removed_count;
        if (const core::SacmElement* element = FindElement(accepted, element_id))
            index.removed.push_back(*element);
    }

    std::sort(index.removed.begin(),
              index.removed.end(),
              [](const core::SacmElement& left, const core::SacmElement& right) { return left.id < right.id; });
}

namespace {

bool IsRelationship(const core::SacmElement& element) {
    return element.type == "assertedinference" || element.type == "assertedcontext" ||
           element.type == "assertedevidence";
}

// Every endpoint a relationship touches: SACM puts the premise in `source_refs`,
// the conclusion in `target_refs`, and a strategy in `reasoning_ref`.
std::vector<std::string> Endpoints(const core::SacmElement& relationship) {
    std::vector<std::string> ids = relationship.source_refs;
    ids.insert(ids.end(), relationship.target_refs.begin(), relationship.target_refs.end());
    if (!relationship.reasoning_ref.empty())
        ids.push_back(relationship.reasoning_ref);
    return ids;
}

} // namespace

core::AssuranceCase BuildChangesOnlyView(const core::AssuranceCase& working, const DraftChangeIndex& index) {
    std::unordered_set<std::string> included;
    for (const std::string& id : index.ChangedElementIds())
        included.insert(id);
    if (included.empty())
        return working;

    // Walk upward to the root: anything an included element supports or gives
    // context to is included too. Repeated to a fixpoint because the path may be
    // several steps long, and bounded by the element count because each pass
    // either adds something or stops.
    bool grew = true;
    while (grew) {
        grew = false;
        for (const core::SacmElement& element : working.elements) {
            if (!IsRelationship(element))
                continue;
            const bool from_included = std::any_of(element.source_refs.begin(),
                                                   element.source_refs.end(),
                                                   [&](const std::string& id) { return included.count(id) > 0; }) ||
                                       (!element.reasoning_ref.empty() && included.count(element.reasoning_ref) > 0);
            if (!from_included)
                continue;
            for (const std::string& target : element.target_refs) {
                if (included.insert(target).second)
                    grew = true;
            }
        }
    }

    core::AssuranceCase view;
    view.id = working.id;
    view.name = working.name;
    view.description = working.description;
    view.acps = working.acps;
    for (const core::SacmElement& element : working.elements) {
        if (IsRelationship(element)) {
            const std::vector<std::string> endpoints = Endpoints(element);
            const bool all_present =
                !endpoints.empty() && std::all_of(endpoints.begin(), endpoints.end(), [&](const std::string& id) {
                    return included.count(id) > 0;
                });
            // A relationship with an endpoint outside the view would render as a
            // dangling edge, which reads as a structural defect the argument does
            // not have.
            if (all_present)
                view.elements.push_back(element);
            continue;
        }
        if (included.count(element.id) > 0)
            view.elements.push_back(element);
    }
    return view;
}

} // namespace core::drafts
