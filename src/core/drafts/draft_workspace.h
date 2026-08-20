#pragma once

// The unaccepted work in progress against one argument file.
//
// A workspace is the single place proposed changes accumulate, whatever made
// them: an MCP client, an SCCG AI review, the user, or a legacy proposal file
// imported at migration. The design this replaces gave each source its own
// store -- a `ChangeSet` per MCP connection, a `ReviewProposal` file per review
// item -- and nothing anywhere held their combination. Two individually
// reasonable proposals could therefore be authored against different effective
// versions of one argument, and the conflict between them first appeared in the
// accepted model.
//
// **A workspace is not a mutation.** Adding to it writes no SACM, runs no
// command, and records no audit transaction. The accepted model changes exactly
// once, when a human promotes -- and then through `ApplyProposalCommand`, the
// same audited, undoable path an edit made with the mouse takes.
//
// Groups carry `core::reviews::PatchOperation` rather than a new operation
// format, so promotion needs no new command type and replay and undo keep
// working unchanged.
//
// See docs/architecture/decisions/0009-one-integrated-working-draft-per-argument.md
// and docs/architecture/decisions/0010-draft-provenance-persistence-and-human-promotion.md.

#include "core/reviews/review_proposal.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace core::drafts {

// Where a group came from. Carried into the audit record at promotion, so a
// reviewer approving a change to a safety argument can tell who wrote it.
enum class DraftSource {
    Human,
    Mcp,
    SccgAiReview,
    // Converted from a `reviews/proposals/*.json` file during migration. Never
    // produced by a live source.
    ImportedLegacyProposal,
};

const char* DraftSourceToString(DraftSource source);
bool DraftSourceFromString(const std::string& value, DraftSource& source);

enum class DraftGroupState {
    // Its author is still adding to it.
    Building,
    // Its author has finished and is asking for a decision.
    Ready,
    // A group it depends on was rejected, or it no longer materializes.
    NeedsAttention,
    // Rejected by the user. Retained for the record; excluded from
    // materialization.
    Rejected,
};

const char* DraftGroupStateToString(DraftGroupState state);
bool DraftGroupStateFromString(const std::string& value, DraftGroupState& state);

enum class DraftWorkspaceState {
    Active,
    // Promotion intent is durably recorded, but cleanup has not yet confirmed
    // that the accepted SACM file contains it. Active groups are inert here so
    // they cannot be applied twice over the prospective accepted result.
    Promoting,
    // The accepted argument changed underneath the draft. Operations are never
    // replayed silently in this state.
    NeedsRebase,
    // Materialization fails and the working model cannot be shown.
    Blocked,
    Closed,
};

const char* DraftWorkspaceStateToString(DraftWorkspaceState state);
bool DraftWorkspaceStateFromString(const std::string& value, DraftWorkspaceState& state);

// One coherent, user-reviewable change: clarify a claim, add a misuse-hazard
// branch, apply one SCCG finding. The unit the user accepts or rejects.
//
// It may hold several operations, because a new argument branch is not
// meaningful without the strategy and relationships that carry it, and offering
// the claim alone would invite an incoherent acceptance.
struct DraftChangeGroup {
    std::string id;

    // Position in the workspace, assigned once and never reused. Materialization
    // order is by this and nothing else -- not by source, not by the order the
    // UI happens to list them in -- because the working model has to be the same
    // model after a save and load.
    std::uint64_t sequence = 0;

    std::string title;
    std::string summary;
    // Why, in the author's own words. The reviewer is being asked to accept a
    // change to a safety argument and deserves the reasoning, not just the diff.
    std::string rationale;

    DraftSource source = DraftSource::Human;
    // Which client, review run or person, so two connected agents can be told
    // apart and attribution survives into the audit trail.
    std::string source_label;
    std::string source_session_id;

    std::string created_utc;
    std::string updated_utc;

    DraftGroupState state = DraftGroupState::Building;

    std::vector<reviews::PatchOperation> operations;

    // `create_ref` -> the element id the operation produced.
    //
    // **Allocated once and then replayed.** Materialization runs whenever the
    // workspace or the accepted model changes, and
    // `ReviewProposalPatchService::ApplyProposal` generates ids as it goes -- so
    // re-materializing without this would rename proposed elements between
    // frames, breaking canvas selection, the inspector, and an agent's reference
    // to the element it was just told it created.
    std::map<std::string, std::string> generated_ids;

    // SCCG guidelines this group serves, and the review items it answers.
    std::vector<std::string> guideline_ids;
    std::vector<std::string> review_item_ids;

    // Problem-severity findings that stood against this group when its author
    // submitted it, acknowledged rather than resolved. Submission refuses such
    // a group unless the author explicitly acknowledges, and the acknowledgment
    // is provenance: the reviewer sees that the shape was flagged and the
    // author chose to hand it over anyway. English, like every other stored
    // provenance string; cleared when the group's operations change.
    std::vector<std::string> acknowledged_findings;

    // Groups this one cannot be promoted without. Inferred during
    // materialization and stored, so the UI can explain a closure rather than
    // assert one.
    std::vector<std::string> depends_on_group_ids;

    // Whether this group takes part in materialization.
    //
    // `NeedsAttention` is excluded deliberately. Such a group cannot apply -- the
    // group that created what it edits is gone -- so materializing it would fail
    // and take the whole working model down with it, leaving the user's draft
    // `Blocked` because they declined a cascade. Excluding it keeps the rest of
    // the draft drawable while the stranded group waits for its author.
    bool active() const {
        return state == DraftGroupState::Building || state == DraftGroupState::Ready;
    }

    // Whether the workspace is worth keeping for this group's sake.
    //
    // Wider than `active()`: a stranded group is real work whose only copy this
    // is, and a workspace holding nothing else must not be cleaned up as though
    // it were empty.
    bool recoverable() const {
        return state != DraftGroupState::Rejected;
    }

    bool open() const {
        return state == DraftGroupState::Building || state == DraftGroupState::Ready;
    }
};

// What happened to the workspace, in order. Gives MCP clients and the UI a
// revisioned feedback channel: an agent that has been away for several turns can
// ask what changed rather than re-reading the whole argument.
struct DraftEvent {
    std::uint64_t revision = 0;
    std::string type;
    std::string group_id;
    std::string detail;
    std::string created_utc;
};

// Recovery marker spanning the audit/SACM commit and draft cleanup. Written
// before promotion touches the accepted document and removed only after the
// expected accepted-model hash is durable.
struct DraftPendingPromotion {
    std::vector<std::string> group_ids;
    std::string expected_model_hash;
    std::string started_utc;
};

// At most one of these exists per argument file.
struct DraftWorkspace {
    std::string id;

    // The argument this draft is written against.
    //
    // Recorded because element ids repeat across a project's arguments -- every
    // argument seeded from the template starts with the same top goal -- so a
    // draft applied to the wrong one would land on ids that happen to match.
    // This is the defect that made a change set built against `main2.sacm`
    // decorate `main.sacm`'s G1.
    std::filesystem::path argument_file;

    // The accepted model this draft was built on. Checked on reopen: a mismatch
    // means the argument changed underneath the draft, and operations are never
    // replayed silently against a baseline they were not written for.
    std::string base_model_hash;

    // Bumped by every call that changes what a reader would see. This is the
    // staleness token a modifying MCP call must name.
    //
    // Group staleness is *positional*, not baseline-relative: a group at
    // position n was authored against the working model after groups 1..n-1, so
    // `ReviewProposal`'s `base_model_hash` cannot express it. The workspace
    // revision can.
    std::uint64_t working_revision = 0;

    std::uint64_t next_sequence = 1;

    DraftWorkspaceState state = DraftWorkspaceState::Active;

    std::optional<DraftPendingPromotion> pending_promotion;

    std::vector<DraftChangeGroup> groups;
    std::vector<DraftEvent> events;

    const DraftChangeGroup* FindGroup(const std::string& group_id) const;
    DraftChangeGroup* FindGroup(const std::string& group_id);

    // Groups that take part in materialization, in sequence order.
    std::vector<const DraftChangeGroup*> ActiveGroups() const;

    bool has_active_groups() const;

    // How many active groups carry at least one operation. What the draft banner
    // counts, so an empty group an agent has opened but not staged into does not
    // read as an unaccepted change.
    std::size_t staged_group_count() const;
};

// Whether `workspace` was written against `argument_file`. A workspace with no
// recorded file belongs to whatever is open -- there is nothing to contradict.
bool WorkspaceTargetsArgumentFile(const DraftWorkspace& workspace, const std::filesystem::path& argument_file);

// The key a workspace is stored under: the project-relative path of its argument
// file with separators and case normalized, so the same argument resolves to the
// same directory on every platform.
//
// A rename produces a different key and orphans the workspace. That is surfaced
// rather than silently discarded -- see `DraftWorkspaceStore`.
std::string ArgumentStableKey(const std::filesystem::path& project_relative_argument_file);

} // namespace core::drafts
