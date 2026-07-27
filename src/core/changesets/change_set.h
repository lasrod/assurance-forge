#pragma once

// A change an AI agent is building, visible from its first operation.
//
// The design this replaces had the agent accumulate operations privately and
// then drop one finished file into the project. Nobody could see the work in
// progress, nobody could tell where in the argument it would land, and the
// agent could not revise it after the user said "not there". A change set fixes
// all three by existing, in the application, from the first staged operation.
//
// **A change set is not a mutation.** Staging touches only the change set: no
// SACM is written, no command runs, no audit transaction is recorded. The model
// changes exactly once, when a human accepts -- and then through
// `ApplyProposalCommand`, the same audited, undoable path an edit made with the
// mouse takes.
//
// It carries a `ReviewProposal` rather than a new operation format, so
// acceptance needs no new command type and staleness detection, replay and undo
// all keep working unchanged.

#include "core/reviews/review_proposal.h"

#include <string>
#include <vector>

namespace core::changesets {

enum class ChangeSetState {
    // The agent is still adding to it.
    Building,
    // The agent has said it is finished and is asking for a decision.
    Ready,
    // Accepted; the operations are in the model and in the audit log.
    Applied,
    // Rejected by the user, or abandoned by the agent.
    Discarded,
};

const char* ChangeSetStateToString(ChangeSetState state);

struct ChangeSet {
    std::string id;
    std::string title;
    std::string summary;
    // What the agent is trying to achieve, in its own words. Shown to the
    // reviewer, who is being asked to accept a change to a safety argument and
    // deserves the reasoning, not just the diff.
    std::string intent;
    // Which client is building it, so a reviewer can tell two connected agents
    // apart and attribution survives into the audit trail.
    std::string client_label;
    std::string created_utc;

    ChangeSetState state = ChangeSetState::Building;

    // The operations, in the same vocabulary the application's own proposals
    // use. Acceptance hands this straight to `ApplyProposalCommand`.
    reviews::ReviewProposal proposal;

    bool open() const { return state == ChangeSetState::Building || state == ChangeSetState::Ready; }
};

// What a change set does to one element.
enum class ElementChange {
    Unchanged,
    Added,
    Modified,
    Removed,
};

const char* ElementChangeToString(ElementChange change);

// The result of applying a change set to the committed model, without applying
// it. `preview_model` is a full assurance case the canvas can render directly,
// which is what makes the agent's work visible in place rather than as a list.
struct ChangeSetDiff {
    bool        success = false;
    std::string error;

    parser::AssuranceCase             preview_model;
    std::map<std::string, ElementChange> status_by_id;
    // Elements the change set removes. They are absent from `preview_model` by
    // definition, so a renderer that wants to ghost them needs them from here.
    std::vector<parser::SacmElement>  removed;
    // `create_ref` -> the id the operation produced, so a caller can report
    // "$goal became G7" rather than leaving the agent to guess.
    std::map<std::string, std::string> generated_ids;

    int added_count    = 0;
    int modified_count = 0;
    int removed_count  = 0;

    bool touches_anything() const {
        return added_count > 0 || modified_count > 0 || removed_count > 0;
    }
};

// Runs the change set's operations against `committed` on a scratch copy and
// reports what they would do. Never mutates `committed`.
ChangeSetDiff ComputeChangeSetDiff(const ChangeSet&             change_set,
                                   const parser::AssuranceCase& committed);

} // namespace core::changesets
