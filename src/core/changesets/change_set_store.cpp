#include "core/changesets/change_set_store.h"

#include "core/reviews/review_proposal_factory.h"
#include "core/time_utils.h"
#include "parser/model_utils.h"

#include <algorithm>
#include <set>
#include <utility>

namespace core::changesets {
namespace {

// Every existing element the operations reach is declared affected and given a
// base hash. Collected here rather than trusted from the agent: one that
// under-declared would be handed weaker staleness checking than it deserves,
// which is exactly the failure that lets a patch apply cleanly to an argument
// that has moved underneath it.
void RefreshAffectedElements(reviews::ReviewProposal& proposal, const parser::AssuranceCase& committed) {
    std::set<std::string> affected;
    if (!proposal.anchor_element_id.empty()) {
        affected.insert(proposal.anchor_element_id);
    }
    for (const reviews::PatchOperation& operation : proposal.operations) {
        for (const std::optional<reviews::ElementRef>* ref :
             {&operation.element, &operation.source, &operation.target}) {
            if (ref->has_value() && (*ref)->existing_id.has_value() && !(*ref)->existing_id->empty()) {
                affected.insert(*(*ref)->existing_id);
            }
        }
    }

    proposal.affected_existing_element_ids.clear();
    proposal.base_element_hashes.clear();
    proposal.base_model_hash = reviews::ComputeModelSemanticHash(committed);
    for (const std::string& id : affected) {
        const parser::SacmElement* element = parser::FindElementById(committed, id);
        if (element == nullptr) {
            continue;
        }
        proposal.affected_existing_element_ids.push_back(id);
        proposal.base_element_hashes[id] = reviews::ComputeElementSemanticHash(*element);
    }

    // An anchor is what staleness detection hangs off. A change set that only
    // adds has nothing to be stale against and may leave it empty; one that
    // touches an existing element must name it, so adopt the first such element
    // rather than leaving the proposal unanchored and unverifiable.
    if (proposal.anchor_element_id.empty() && !proposal.affected_existing_element_ids.empty()) {
        proposal.anchor_element_id = proposal.affected_existing_element_ids.front();
    }
}

} // namespace

std::string ChangeSetStore::Begin(std::uint64_t connection_id,
                                  std::string title,
                                  std::string summary,
                                  std::string intent,
                                  std::string client_label,
                                  std::filesystem::path argument_file) {
    // One open change set per connection. A second `begin` means the agent has
    // moved on; keeping the first would leave an orphan on the canvas that
    // nobody is going to finish.
    for (std::size_t index = 0; index < change_sets_.size(); ++index) {
        if (owners_[index] == connection_id && change_sets_[index].open()) {
            change_sets_[index].state = ChangeSetState::Discarded;
        }
    }

    ChangeSet change_set;
    change_set.id = reviews::GenerateReviewProposalId();
    change_set.title = std::move(title);
    change_set.summary = std::move(summary);
    change_set.intent = std::move(intent);
    change_set.client_label = std::move(client_label);
    change_set.created_utc = NowUtcString();
    change_set.argument_file = std::move(argument_file);
    change_set.state = ChangeSetState::Building;

    change_set.proposal.id = change_set.id;
    change_set.proposal.title = change_set.title;
    change_set.proposal.summary = change_set.summary;
    change_set.proposal.created_utc = change_set.created_utc;
    // Attribution the reviewer sees and the audit trail keeps, so an accepted
    // change is traceable to the client that proposed it rather than to "the AI".
    change_set.proposal.author_name = "MCP: " + change_set.client_label;

    ++revision_;
    const std::string id = change_set.id;
    change_sets_.push_back(std::move(change_set));
    owners_.push_back(connection_id);
    ++next_serial_;
    return id;
}

ChangeSet* ChangeSetStore::FindOpen(const std::string& id) {
    for (ChangeSet& change_set : change_sets_) {
        if (change_set.id == id && change_set.open()) {
            return &change_set;
        }
    }
    return nullptr;
}

const ChangeSet* ChangeSetStore::Find(const std::string& id) const {
    for (const ChangeSet& change_set : change_sets_) {
        if (change_set.id == id) {
            return &change_set;
        }
    }
    return nullptr;
}

ChangeSet* ChangeSetStore::OpenFor(std::uint64_t connection_id) {
    for (std::size_t index = 0; index < change_sets_.size(); ++index) {
        if (owners_[index] == connection_id && change_sets_[index].open()) {
            return &change_sets_[index];
        }
    }
    return nullptr;
}

bool ChangeSetStore::Stage(const std::string& id,
                           const std::vector<reviews::PatchOperation>& operations,
                           const parser::AssuranceCase& committed,
                           std::string& error) {
    error.clear();
    ChangeSet* change_set = FindOpen(id);
    if (change_set == nullptr) {
        error = "No open change set has the id \"" + id + "\".";
        return false;
    }
    if (operations.empty()) {
        error = "No operations were given.";
        return false;
    }

    // Staged as a unit against a copy: half-applying a set the agent thought was
    // atomic would leave the canvas showing something nobody asked for.
    ChangeSet candidate = *change_set;
    candidate.proposal.operations.insert(candidate.proposal.operations.end(), operations.begin(), operations.end());
    RefreshAffectedElements(candidate.proposal, committed);

    const ChangeSetDiff diff = ComputeChangeSetDiff(candidate, committed);
    if (!diff.success) {
        error = diff.error;
        return false;
    }

    *change_set = std::move(candidate);
    ++revision_;
    return true;
}

bool ChangeSetStore::Unstage(const std::string& id, std::size_t count, std::string& error) {
    error.clear();
    ChangeSet* change_set = FindOpen(id);
    if (change_set == nullptr) {
        error = "No open change set has the id \"" + id + "\".";
        return false;
    }

    std::vector<reviews::PatchOperation>& operations = change_set->proposal.operations;
    operations.resize(count >= operations.size() ? 0 : operations.size() - count);
    ++revision_;
    return true;
}

bool ChangeSetStore::MarkReady(const std::string& id, std::string& error) {
    error.clear();
    ChangeSet* change_set = FindOpen(id);
    if (change_set == nullptr) {
        error = "No open change set has the id \"" + id + "\".";
        return false;
    }
    if (change_set->proposal.operations.empty()) {
        error = "This change set stages no operations, so there is nothing to review.";
        return false;
    }
    change_set->state = ChangeSetState::Ready;
    ++revision_;
    return true;
}

bool ChangeSetStore::MarkApplied(const std::string& id) {
    ChangeSet* change_set = FindOpen(id);
    if (change_set == nullptr) {
        return false;
    }
    change_set->state = ChangeSetState::Applied;
    ++revision_;
    return true;
}

bool ChangeSetStore::Discard(const std::string& id, std::string& error) {
    error.clear();
    // Looked up across every state, not only the open ones. Reported: a client
    // listed a change set as `ready`, then could not discard the same id, and
    // read that as the two calls disagreeing about the store. They never do --
    // it had been closed in between, most often by the client's own
    // `begin_change_set` -- but "no open change set has that id" cannot be told
    // apart from "no such id", so the state of the store looked incoherent.
    for (ChangeSet& change_set : change_sets_) {
        if (change_set.id != id) {
            continue;
        }
        if (change_set.state == ChangeSetState::Applied) {
            error = "That change set was accepted and is part of the case now. Only the user can "
                    "reverse it, with undo in Assurance Forge.";
            return false;
        }
        if (change_set.state == ChangeSetState::Discarded) {
            // Already withdrawn. Withdrawing it again is what the caller wanted.
            return true;
        }
        change_set.state = ChangeSetState::Discarded;
        ++revision_;
        return true;
    }
    error = "No change set has the id \"" + id + "\".";
    return false;
}

std::vector<const ChangeSet*> ChangeSetStore::Open() const {
    std::vector<const ChangeSet*> open;
    for (const ChangeSet& change_set : change_sets_) {
        if (change_set.open()) {
            open.push_back(&change_set);
        }
    }
    return open;
}

bool ChangeSetStore::has_open() const {
    for (const ChangeSet& change_set : change_sets_) {
        if (change_set.open()) {
            return true;
        }
    }
    return false;
}

void ChangeSetStore::Clear() {
    change_sets_.clear();
    owners_.clear();
    ++revision_;
}

} // namespace core::changesets
