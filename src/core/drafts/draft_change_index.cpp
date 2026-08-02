#include "core/drafts/draft_change_index.h"

#include "core/reviews/review_proposal.h"

#include <algorithm>
#include <unordered_map>

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

} // namespace core::drafts
