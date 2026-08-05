#pragma once

// The one draft workspace the application has open, and the only thing that
// writes draft recovery state.
//
// ADR 0008's single-owner rule is unchanged by drafts existing: the running
// application owns the open project, and this store is the application's. The
// MCP adapter holds no workspace, and the offline adapter does not read one --
// the accepted baseline is the only content a human has accepted, and a headless
// read has no UI to mark unaccepted content as unaccepted.
//
// **Every mutation here bumps the revision and autosaves.** The revision is the
// staleness token a modifying MCP call must name, and the autosave is what makes
// a long conversation survive a restart. Neither writes SACM, runs a command,
// nor records an audit transaction: the accepted `.sacm` stays byte-identical
// until a human promotes.

#include "core/drafts/draft_materializer.h"
#include "core/drafts/draft_workspace.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace core::drafts {

// What a caller must supply to open a group. Grouped into a struct because the
// provenance is the point -- a group with no source label is a change to a safety
// argument that nobody is accountable for.
struct DraftGroupRequest {
    std::string title;
    std::string summary;
    std::string rationale;
    DraftSource source = DraftSource::Human;
    std::string source_label;
    std::string source_session_id;
    std::vector<std::string> guideline_ids;
    std::vector<std::string> review_item_ids;
};

class DraftWorkspaceStore {
public:
    void SetProjectRoot(std::filesystem::path project_root);
    const std::filesystem::path& project_root() const {
        return project_root_;
    }

    // The argument this store has open, whether or not a workspace exists for it.
    const std::filesystem::path& argument_file() const {
        return argument_file_;
    }

    // Opens the workspace for `argument_file`, restoring stored recovery data
    // when there is any.
    //
    // A stored workspace whose `base_model_hash` does not match `accepted`
    // enters `NeedsRebase`: the argument changed underneath the draft, and its
    // operations are **not** replayed. Replaying a patch against a document it
    // was not written for is how a tool silently reinterprets a safety argument.
    //
    // Returns false only on a real failure to read; a project with no draft is
    // the normal case and succeeds with an empty workspace.
    bool Open(const std::filesystem::path& argument_file, const core::AssuranceCase& accepted, std::string& error);

    // Forgets the in-memory workspace without touching what is on disk, so the
    // draft is still there when the argument is opened again.
    void Close();

    bool has_workspace() const {
        return workspace_.has_value();
    }
    const DraftWorkspace* workspace() const {
        return workspace_.has_value() ? &workspace_.value() : nullptr;
    }

    std::uint64_t revision() const {
        return workspace_.has_value() ? workspace_->working_revision : 0;
    }

    // The complete id domain from the authoritative SACM document, including
    // packages and utility elements omitted by the canvas projection. New draft
    // ids are allocated against this set so promotion never discovers a hidden
    // collision after an identity has already been shown to a user or agent.
    void SetAuthoritativeIdentities(std::unordered_set<std::string> identities);
    const std::unordered_set<std::string>& authoritative_identities() const {
        return authoritative_identities_;
    }

    // Opens a change group and returns its id, or an empty string on failure.
    //
    // Creates the workspace if this is the first group. `accepted` is needed for
    // that: the base hash is what a later reopen compares against to decide
    // whether the argument moved underneath the draft, and a workspace without
    // one would replay silently.
    std::string BeginGroup(const DraftGroupRequest& request, const core::AssuranceCase& accepted, std::string& error);

    // Appends operations. Refused as a whole if the result would not materialize
    // on top of everything already active, so a group never holds a patch that
    // cannot be drawn -- which is what lets the canvas render the working model
    // at any moment.
    bool StageOperations(const std::string& group_id,
                         const std::vector<reviews::PatchOperation>& operations,
                         const core::AssuranceCase& accepted,
                         std::string& error);

    // Replaces a group's operations wholesale, so an author can respond to "not
    // there" without abandoning the group and losing its rationale and links.
    bool ReplaceOperations(const std::string& group_id,
                           const std::vector<reviews::PatchOperation>& operations,
                           const core::AssuranceCase& accepted,
                           std::string& error);

    bool MarkGroupReady(const std::string& group_id, std::string& error);

    // Rejects a group. It stops taking part in materialization but is kept, so
    // the record of what was proposed and declined survives.
    bool RejectGroup(const std::string& group_id, std::string& error);

    // Marks a group stranded: something it depended on was rejected, so it can no
    // longer apply.
    //
    // Offered instead of a cascade, for a user who rejects one change but does
    // not want the changes built on top of it thrown away with it. The group
    // leaves materialization -- it would fail and block the whole draft -- but
    // stays in the workspace, and its author can `ReplaceOperations` to retarget
    // it at something that exists.
    bool MarkGroupNeedsAttention(const std::string& group_id, std::string& error);

    // Durably records promotion intent before the accepted SACM/audit command
    // runs. While pending, draft operations are inert. Recovery can then decide
    // from the accepted-model hash whether to finalize or cancel after a crash.
    bool BeginPromotion(const std::vector<std::string>& group_ids,
                        const core::AssuranceCase& expected_accepted,
                        std::string& error);
    bool CancelPromotion(std::string& error);

    // Drops groups that have just been promoted and rebases what is left onto
    // `new_accepted`.
    //
    // The base hash moves with them. Without that, the next open would compare
    // the draft against the argument the user just accepted into and call it
    // stale -- refusing to apply the remaining groups because of a change the
    // user themselves made through this very path.
    bool RemovePromotedGroups(const std::vector<std::string>& group_ids,
                              const core::AssuranceCase& new_accepted,
                              std::string& error);

    // Discards the whole workspace, in memory and on disk. The accepted `.sacm`
    // is left byte-identical.
    bool DiscardWorkspace(std::string& error);

    // --- Undoing a promotion ---------------------------------------------
    //
    // `RemovePromotedGroups` is not reversible on its own: it erases the groups,
    // and when none are left it deletes the workspace outright. Undo restores the
    // accepted model from the audit log, which would otherwise leave the promoted
    // work in neither place.
    //
    // So promotion records the workspace as it stood before it, keyed by the
    // audit transaction sequence it produced, and undo puts it back.

    // Writes the pre-promotion workspace for `transaction_sequence`. Called after
    // the promotion's audited command has committed, because that is when the
    // sequence exists -- but before the groups are removed, so the snapshot is
    // durable before the thing it recovers is gone.
    bool SavePromotionSnapshot(std::uint64_t transaction_sequence,
                               const DraftWorkspace& pre_promotion,
                               std::string& error) const;

    // Convenience for tests and diagnostics. **Not for a fail-closed decision**
    // -- it answers false both for "no snapshot" and for "the path could not be
    // interrogated". Undo must tell those apart, so it uses the loader below.
    bool HasPromotionSnapshot(std::uint64_t transaction_sequence) const;

    // Returns false with an empty `error` when `transaction_sequence` is not a
    // promotion, which is the ordinary case, and false with a non-empty `error`
    // when there may be a snapshot that could not be read. A caller about to
    // discard unaccepted work has to refuse on the second and continue on the
    // first, so it must call this rather than test existence first.
    bool LoadPromotionSnapshot(std::uint64_t transaction_sequence, DraftWorkspace& snapshot, std::string& error) const;

    bool DeletePromotionSnapshot(std::uint64_t transaction_sequence, std::string& error) const;

    // Puts the groups a promotion consumed back into the live workspace, once
    // that promotion has been undone and `restored_accepted` is the model the
    // undo produced.
    //
    // Groups the workspace already holds are kept as they are, and only those
    // missing from it are reinstated. Work staged *after* the promotion is
    // therefore not lost -- which wholesale replacement with the snapshot would
    // do, trading one silent loss for another. Reinstated groups keep their
    // original sequence and their `generated_ids`, so a later group that
    // developed a promoted element still resolves against it.
    bool RestorePromotionSnapshot(const DraftWorkspace& snapshot,
                                  const core::AssuranceCase& restored_accepted,
                                  std::string& error);

    // --- Undoing one draft edit ------------------------------------------
    //
    // Draft mutation is deliberately not a command: it records no audit
    // transaction and triggers no `.sacm` autosave, which is what keeps the
    // accepted file byte-stable while a draft is built. So it cannot use the
    // accepted model's undo stack, and needs one of its own.
    //
    // This stack holds the workspace as it stood before each edit. It is
    // **session state, not recovery state**: the draft's content is persisted so
    // a restart recovers the work, but the edit history is not, so a restart
    // recovers the work with nothing left to undo. Persisting it would grow the
    // stored draft by a whole workspace copy per keystroke to buy back a
    // convenience, and the thing that must survive a crash is the work itself.
    //
    // A promotion clears it. The states below it describe groups the accepted
    // argument now contains, and undoing into one would re-stage work the user
    // has already accepted. Undoing the promotion itself is a different
    // mechanism -- an audit boundary plus `RestorePromotionSnapshot`.

    bool CanUndoDraftEdit() const;

    // Collapses every mutation made during its lifetime into one undo entry.
    //
    // One user gesture can be several store mutations: rejecting a change and
    // stranding what depended on it is two, and the whole point of putting that
    // choice to the user is that the two go together. Without this, one Ctrl+Z
    // reverses half of it and leaves the draft in exactly the incoherent state
    // the choice existed to prevent.
    //
    // An entry is pushed only if the revision actually moved, so a gesture that
    // was refused leaves nothing to undo.
    class [[nodiscard]] EditUndoScope {
    public:
        EditUndoScope(DraftWorkspaceStore& store, std::string label);
        ~EditUndoScope();

        EditUndoScope(const EditUndoScope&) = delete;
        EditUndoScope& operator=(const EditUndoScope&) = delete;

    private:
        DraftWorkspaceStore& store_;
        std::string label_;
        std::optional<DraftWorkspace> before_;
        std::uint64_t revision_on_entry_ = 0;
        bool outermost_ = false;
    };

    // What the next undo would reverse, for the status line. Empty when there is
    // nothing to undo.
    std::string NextDraftUndoLabel() const;

    // Restores the workspace to its state before the most recent edit.
    //
    // The revision still moves **forward**: an undo changes what a reader would
    // see, and a token minted before it must not silently become valid again
    // because the content happens to match. `next_sequence` likewise never goes
    // backwards, so a reinstated group id can never name two different groups in
    // one event log.
    bool UndoDraftEdit(std::string& error);

    // The working model, and everything derived from it.
    //
    // `accepted_revision` is the caller's own token for "the accepted model has
    // changed" -- the application passes `AppState::case_revision`. Passing it
    // rather than hashing the model here keeps a per-frame call cheap.
    //
    // Identities allocated by this call are persisted before it returns.
    const DraftMaterializationResult& Materialize(const core::AssuranceCase& accepted, std::uint64_t accepted_revision);

    // Immutable published snapshot for UI/frame consumers. A caller that keeps
    // this handle may continue rendering it after the workspace is mutated or
    // discarded; invalidation publishes a replacement later and never destroys
    // a snapshot still in use.
    std::shared_ptr<const DraftMaterializationResult> MaterializeSnapshot(const core::AssuranceCase& accepted,
                                                                          std::uint64_t accepted_revision);

    // Forces the next `Materialize` to recompute. Needed when the accepted model
    // changed in place without the caller's revision token moving.
    void InvalidateMaterialization();

private:
    // The workspace as it stood before one edit, and what that edit was.
    struct DraftEditUndoEntry {
        DraftWorkspace workspace;
        // Shown in the status line when this entry is undone, so the user is told
        // what came back rather than only that something did.
        std::string label;
    };

    DraftWorkspace& EnsureWorkspace(const core::AssuranceCase& accepted);
    void RecordEvent(std::string type, std::string group_id, std::string detail);
    // Records the pre-edit workspace. Called by a mutation once it is certain to
    // succeed, so a refused edit leaves no undo entry that would reverse nothing.
    void PushEditUndo(std::string label);
    void ClearEditUndoHistory();
    bool Save(std::string& error);
    std::string ArgumentKey() const;

    std::filesystem::path project_root_;
    std::filesystem::path argument_file_;
    std::filesystem::path project_relative_argument_file_;
    std::optional<DraftWorkspace> workspace_;
    std::unordered_set<std::string> authoritative_identities_;
    std::vector<DraftEditUndoEntry> edit_undo_stack_;
    // Non-zero while an `EditUndoScope` is open, which suppresses the per-mutation
    // entries in favour of the one the scope pushes.
    int edit_undo_scope_depth_ = 0;

    std::shared_ptr<DraftMaterializationResult> materialization_ = std::make_shared<DraftMaterializationResult>();
    bool materialization_valid_ = false;
    std::uint64_t materialized_workspace_revision_ = 0;
    std::uint64_t materialized_accepted_revision_ = 0;
};

} // namespace core::drafts
