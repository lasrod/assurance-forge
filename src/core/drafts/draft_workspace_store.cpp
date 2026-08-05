#include "core/drafts/draft_workspace_store.h"

#include "core/drafts/draft_persistence.h"
#include "core/reviews/review_proposal.h"
#include "core/time_utils.h"

#include <algorithm>
#include <utility>

namespace core::drafts {
namespace {

std::filesystem::path RelativeToRoot(const std::filesystem::path& project_root, const std::filesystem::path& file) {
    if (project_root.empty() || file.empty())
        return file.filename();
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(file, project_root, ec);
    if (ec || relative.empty() || relative.native().rfind(std::filesystem::path("..").native(), 0) == 0)
        return file.filename();
    return relative;
}

} // namespace

void DraftWorkspaceStore::SetProjectRoot(std::filesystem::path project_root) {
    if (project_root_ == project_root)
        return;
    project_root_ = std::move(project_root);
    Close();
}

void DraftWorkspaceStore::Close() {
    workspace_.reset();
    argument_file_.clear();
    project_relative_argument_file_.clear();
    // The stack holds workspaces for the argument being closed. Carrying it into
    // the next argument would offer to undo one draft into another.
    ClearEditUndoHistory();
    InvalidateMaterialization();
}

void DraftWorkspaceStore::InvalidateMaterialization() {
    materialization_valid_ = false;
}

void DraftWorkspaceStore::SetAuthoritativeIdentities(std::unordered_set<std::string> identities) {
    if (authoritative_identities_ == identities)
        return;
    authoritative_identities_ = std::move(identities);
    InvalidateMaterialization();
}

std::string DraftWorkspaceStore::ArgumentKey() const {
    return ArgumentStableKey(project_relative_argument_file_);
}

bool DraftWorkspaceStore::Open(const std::filesystem::path& argument_file,
                               const core::AssuranceCase& accepted,
                               std::string& error) {
    error.clear();
    Close();

    argument_file_ = argument_file;
    project_relative_argument_file_ = RelativeToRoot(project_root_, argument_file);
    if (project_root_.empty())
        return true;

    DraftWorkspace stored;
    std::string load_error;
    if (!LoadDraftWorkspace(project_root_, ArgumentKey(), stored, load_error)) {
        if (!load_error.empty()) {
            error = load_error;
            return false;
        }
        return true;
    }

    const std::string accepted_hash = reviews::ComputeModelSemanticHash(accepted);
    stored.argument_file = argument_file;
    workspace_ = std::move(stored);

    if (workspace_->pending_promotion.has_value()) {
        const DraftPendingPromotion pending = workspace_->pending_promotion.value();
        if (accepted_hash == pending.expected_model_hash) {
            return RemovePromotedGroups(pending.group_ids, accepted, error);
        }
        if (accepted_hash == workspace_->base_model_hash) {
            return CancelPromotion(error);
        }
        workspace_->state = DraftWorkspaceState::NeedsRebase;
        if (!Save(error))
            return false;
        return true;
    }

    if (!workspace_->has_active_groups()) {
        workspace_.reset();
        std::string cleanup_error;
        DeleteDraftWorkspace(project_root_, ArgumentKey(), cleanup_error);
        return true;
    }

    if (!workspace_->base_model_hash.empty() && workspace_->base_model_hash != accepted_hash) {
        // The argument moved underneath the draft. Nothing is replayed: the user
        // is offered inspect, rebase, export or discard, and until they choose,
        // the draft is inert rather than quietly applied to a document it was
        // not written for.
        workspace_->state = DraftWorkspaceState::NeedsRebase;
    } else if (workspace_->state == DraftWorkspaceState::NeedsRebase) {
        workspace_->state = DraftWorkspaceState::Active;
    }
    return true;
}

DraftWorkspace& DraftWorkspaceStore::EnsureWorkspace(const core::AssuranceCase& accepted) {
    if (!workspace_.has_value()) {
        DraftWorkspace created;
        created.id = "draft-" + ArgumentKey();
        created.argument_file = argument_file_;
        created.base_model_hash = reviews::ComputeModelSemanticHash(accepted);
        created.state = DraftWorkspaceState::Active;
        workspace_ = std::move(created);
    }
    return workspace_.value();
}

void DraftWorkspaceStore::RecordEvent(std::string type, std::string group_id, std::string detail) {
    if (!workspace_.has_value())
        return;
    DraftEvent event;
    event.revision = workspace_->working_revision;
    event.type = std::move(type);
    event.group_id = std::move(group_id);
    event.detail = std::move(detail);
    event.created_utc = NowUtcString();
    workspace_->events.push_back(std::move(event));
}

bool DraftWorkspaceStore::Save(std::string& error) {
    if (!workspace_.has_value() || project_root_.empty())
        return true;
    return SaveDraftWorkspace(project_root_, ArgumentKey(), workspace_.value(), error);
}

namespace {

// Deep enough that an editing session does not run out of undo in practice,
// bounded because each entry is a whole workspace copy and a draft built over a
// long MCP conversation would otherwise grow without limit. Dropping the oldest
// entry costs an editing convenience; the draft content itself is persisted
// separately and is never what is discarded here.
constexpr std::size_t kMaxDraftEditUndoDepth = 64;

} // namespace

void DraftWorkspaceStore::PushEditUndo(std::string label) {
    if (!workspace_.has_value())
        return;
    edit_undo_stack_.push_back(DraftEditUndoEntry{workspace_.value(), std::move(label)});
    if (edit_undo_stack_.size() > kMaxDraftEditUndoDepth)
        edit_undo_stack_.erase(edit_undo_stack_.begin());
}

void DraftWorkspaceStore::ClearEditUndoHistory() {
    edit_undo_stack_.clear();
}

bool DraftWorkspaceStore::CanUndoDraftEdit() const {
    if (!workspace_.has_value() || edit_undo_stack_.empty())
        return false;
    // While a promotion is pending the draft is inert by design, and reversing an
    // edit under it would change what the recorded promotion intent refers to.
    return !workspace_->pending_promotion.has_value();
}

std::string DraftWorkspaceStore::NextDraftUndoLabel() const {
    if (!CanUndoDraftEdit())
        return {};
    return edit_undo_stack_.back().label;
}

bool DraftWorkspaceStore::UndoDraftEdit(std::string& error) {
    error.clear();
    if (!workspace_.has_value()) {
        error = "There is no draft workspace for this argument.";
        return false;
    }
    if (workspace_->pending_promotion.has_value()) {
        error = "A draft promotion is awaiting durable completion, so its edits cannot be undone.";
        return false;
    }
    if (edit_undo_stack_.empty()) {
        error = "There is no draft change left to undo.";
        return false;
    }

    DraftEditUndoEntry entry = std::move(edit_undo_stack_.back());
    edit_undo_stack_.pop_back();

    // Only what the edit changed is restored. The event log is a record of what
    // happened and an undo is one more thing that happened, so it is appended to
    // rather than rewound -- an MCP client polling by revision must not see
    // history it has already read disappear.
    workspace_->groups = std::move(entry.workspace.groups);
    workspace_->state = entry.workspace.state;
    workspace_->next_sequence = std::max(workspace_->next_sequence, entry.workspace.next_sequence);
    ++workspace_->working_revision;
    RecordEvent("edit_undone", {}, entry.label);
    InvalidateMaterialization();
    return Save(error);
}

std::string DraftWorkspaceStore::BeginGroup(const DraftGroupRequest& request,
                                            const core::AssuranceCase& accepted,
                                            std::string& error) {
    error.clear();
    if (argument_file_.empty()) {
        error = "No argument is open, so there is nothing to draft against.";
        return {};
    }
    if (request.source_label.empty()) {
        error = "A draft change group needs a source label so its author is attributable.";
        return {};
    }

    // The first group is what brings the workspace into being. Until then there
    // is no draft, and `has_workspace()` says so -- an agent that connects and
    // reads must not be told there is unaccepted work when there is none.
    DraftWorkspace& workspace = EnsureWorkspace(accepted);
    if (workspace.pending_promotion.has_value()) {
        error = "A draft promotion is awaiting durable completion. Resolve it before adding more changes.";
        return {};
    }
    if (workspace.state == DraftWorkspaceState::NeedsRebase) {
        error = "The argument changed since this draft was written. Rebase or discard it before adding to it.";
        return {};
    }

    PushEditUndo(request.title.empty() ? std::string("New change group") : request.title);

    DraftChangeGroup group;
    group.sequence = workspace.next_sequence++;
    group.id = "group-" + std::to_string(group.sequence);
    group.title = request.title;
    group.summary = request.summary;
    group.rationale = request.rationale;
    group.source = request.source;
    group.source_label = request.source_label;
    group.source_session_id = request.source_session_id;
    group.created_utc = NowUtcString();
    group.updated_utc = group.created_utc;
    group.state = DraftGroupState::Building;
    group.guideline_ids = request.guideline_ids;
    group.review_item_ids = request.review_item_ids;

    const std::string group_id = group.id;
    workspace.groups.push_back(std::move(group));
    ++workspace.working_revision;
    RecordEvent("group_created", group_id, request.title);
    InvalidateMaterialization();

    if (!Save(error))
        return {};
    return group_id;
}

bool DraftWorkspaceStore::StageOperations(const std::string& group_id,
                                          const std::vector<reviews::PatchOperation>& operations,
                                          const core::AssuranceCase& accepted,
                                          std::string& error) {
    error.clear();
    if (!workspace_.has_value()) {
        error = "There is no draft workspace for this argument.";
        return false;
    }
    DraftWorkspace& workspace = workspace_.value();
    if (workspace.pending_promotion.has_value()) {
        error = "A draft promotion is awaiting durable completion. Resolve it before adding more changes.";
        return false;
    }
    if (workspace.state == DraftWorkspaceState::NeedsRebase) {
        error = "The argument changed since this draft was written. Rebase or discard it before adding to it.";
        return false;
    }
    DraftChangeGroup* group = workspace.FindGroup(group_id);
    if (group == nullptr) {
        error = "No draft change group with id " + group_id + ".";
        return false;
    }
    if (!group->open()) {
        error = "Draft change group " + group_id + " is no longer open.";
        return false;
    }
    if (operations.empty()) {
        error = "No operations were supplied.";
        return false;
    }

    // Rehearsed before it is kept. A group holding a patch that cannot be
    // materialized would take the whole working model down with it, and the
    // canvas has to be able to draw the draft at any moment.
    if (!CanStageOperations(workspace, accepted, group_id, operations, error, authoritative_identities_))
        return false;

    PushEditUndo(group->title.empty() ? std::string("Staged operations") : group->title);

    group->operations.insert(group->operations.end(), operations.begin(), operations.end());
    group->updated_utc = NowUtcString();
    ++workspace.working_revision;
    RecordEvent("operations_staged", group_id, std::to_string(operations.size()) + " operations");
    InvalidateMaterialization();
    return Save(error);
}

bool DraftWorkspaceStore::ReplaceOperations(const std::string& group_id,
                                            const std::vector<reviews::PatchOperation>& operations,
                                            const core::AssuranceCase& accepted,
                                            std::string& error) {
    error.clear();
    if (!workspace_.has_value()) {
        error = "There is no draft workspace for this argument.";
        return false;
    }
    DraftWorkspace& workspace = workspace_.value();
    if (workspace.pending_promotion.has_value()) {
        error = "A draft promotion is awaiting durable completion. Resolve it before changing it.";
        return false;
    }
    if (workspace.state == DraftWorkspaceState::NeedsRebase) {
        error = "The argument changed since this draft was written. Rebase or discard it before changing it.";
        return false;
    }
    DraftChangeGroup* group = workspace.FindGroup(group_id);
    if (group == nullptr) {
        error = "No draft change group with id " + group_id + ".";
        return false;
    }
    // A stranded group is the one closed state that can still be written to.
    // Retargeting its operations at something that exists is the only way out of
    // `NeedsAttention`, and without it that state is a place work goes to die
    // rather than an alternative to the cascade.
    if (!group->open() && group->state != DraftGroupState::NeedsAttention) {
        error = "Draft change group " + group_id + " is no longer open.";
        return false;
    }

    const std::vector<reviews::PatchOperation> previous_operations = group->operations;
    const std::map<std::string, std::string> previous_identities = group->generated_ids;

    std::map<std::string, std::string> retained_identities;
    for (const auto& [create_ref, id] : previous_identities) {
        const bool reused = std::any_of(operations.begin(), operations.end(), [&](const reviews::PatchOperation& op) {
            return op.create_ref.has_value() && op.create_ref.value() == create_ref;
        });
        if (reused)
            retained_identities[create_ref] = id;
    }

    // Replacing drops the identities the old operations pinned. Any `create_ref`
    // the new operations reuse keeps its element id, so an author who rewords a
    // creation does not rename the element an agent was already told about;
    // anything it no longer creates releases its id. The retained set is present
    // during rehearsal as well as after it: rehearsing with different identities
    // from the real workspace would validate a model we never intend to publish.
    group->operations.clear();
    group->generated_ids = retained_identities;
    if (!CanStageOperations(workspace, accepted, group_id, operations, error, authoritative_identities_)) {
        group->operations = previous_operations;
        group->generated_ids = previous_identities;
        return false;
    }

    // Taken after the rehearsal has restored the group, so the entry holds the
    // workspace the user last saw rather than the cleared intermediate state the
    // rehearsal needed.
    group->operations = previous_operations;
    group->generated_ids = previous_identities;
    PushEditUndo(group->title.empty() ? std::string("Replaced operations") : group->title);

    group->operations = operations;
    group->generated_ids = std::move(retained_identities);
    group->updated_utc = NowUtcString();
    // The rehearsal above proved these operations apply on top of everything
    // active, which is exactly the condition that stranded it. So it is no longer
    // stranded, and rejoins materialization.
    if (group->state == DraftGroupState::NeedsAttention)
        group->state = DraftGroupState::Building;
    ++workspace.working_revision;
    RecordEvent("operations_replaced", group_id, std::to_string(operations.size()) + " operations");
    InvalidateMaterialization();
    return Save(error);
}

bool DraftWorkspaceStore::MarkGroupReady(const std::string& group_id, std::string& error) {
    error.clear();
    if (!workspace_.has_value()) {
        error = "There is no draft workspace for this argument.";
        return false;
    }
    if (workspace_->pending_promotion.has_value()) {
        error = "A draft promotion is awaiting durable completion.";
        return false;
    }
    DraftChangeGroup* group = workspace_->FindGroup(group_id);
    if (group == nullptr) {
        error = "No draft change group with id " + group_id + ".";
        return false;
    }
    if (group->operations.empty()) {
        error = "Draft change group " + group_id + " has no operations to review.";
        return false;
    }
    PushEditUndo(group->title.empty() ? std::string("Marked ready") : group->title);

    group->state = DraftGroupState::Ready;
    group->updated_utc = NowUtcString();
    ++workspace_->working_revision;
    RecordEvent("group_ready", group_id, group->title);
    InvalidateMaterialization();
    return Save(error);
}

bool DraftWorkspaceStore::RejectGroup(const std::string& group_id, std::string& error) {
    error.clear();
    if (!workspace_.has_value()) {
        error = "There is no draft workspace for this argument.";
        return false;
    }
    if (workspace_->pending_promotion.has_value()) {
        error = "A draft promotion is awaiting durable completion and cannot be rejected.";
        return false;
    }
    DraftChangeGroup* group = workspace_->FindGroup(group_id);
    if (group == nullptr) {
        error = "No draft change group with id " + group_id + ".";
        return false;
    }
    if (group->state == DraftGroupState::Rejected)
        return true;

    PushEditUndo(group->title.empty() ? std::string("Rejected a change") : group->title);

    group->state = DraftGroupState::Rejected;
    group->updated_utc = NowUtcString();
    ++workspace_->working_revision;
    RecordEvent("group_rejected", group_id, group->title);
    InvalidateMaterialization();
    return Save(error);
}

bool DraftWorkspaceStore::MarkGroupNeedsAttention(const std::string& group_id, std::string& error) {
    error.clear();
    if (!workspace_.has_value()) {
        error = "There is no draft workspace for this argument.";
        return false;
    }
    if (workspace_->pending_promotion.has_value()) {
        error = "A draft promotion is awaiting durable completion.";
        return false;
    }
    DraftChangeGroup* group = workspace_->FindGroup(group_id);
    if (group == nullptr) {
        error = "No draft change group with id " + group_id + ".";
        return false;
    }
    // A group the user rejected outright is not quietly revived into a state that
    // still asks them for a decision.
    if (group->state == DraftGroupState::Rejected || group->state == DraftGroupState::NeedsAttention)
        return true;

    PushEditUndo(group->title.empty() ? std::string("Stranded a change") : group->title);

    group->state = DraftGroupState::NeedsAttention;
    group->updated_utc = NowUtcString();
    ++workspace_->working_revision;
    RecordEvent("group_needs_attention", group_id, group->title);
    InvalidateMaterialization();
    return Save(error);
}

bool DraftWorkspaceStore::BeginPromotion(const std::vector<std::string>& group_ids,
                                         const core::AssuranceCase& expected_accepted,
                                         std::string& error) {
    error.clear();
    if (!workspace_.has_value() || group_ids.empty()) {
        error = "There is no draft selection to promote.";
        return false;
    }
    if (workspace_->pending_promotion.has_value()) {
        error = "A draft promotion is already awaiting durable completion.";
        return false;
    }
    for (const std::string& group_id : group_ids) {
        const DraftChangeGroup* group = workspace_->FindGroup(group_id);
        if (group == nullptr || !group->active()) {
            error = "Draft group " + group_id + " is not active and cannot be promoted.";
            return false;
        }
    }

    const DraftWorkspace previous = workspace_.value();
    DraftPendingPromotion pending;
    pending.group_ids = group_ids;
    pending.expected_model_hash = reviews::ComputeModelSemanticHash(expected_accepted);
    pending.started_utc = NowUtcString();
    workspace_->pending_promotion = std::move(pending);
    workspace_->state = DraftWorkspaceState::Promoting;
    ++workspace_->working_revision;
    RecordEvent("promotion_started", {}, std::to_string(group_ids.size()) + " groups");
    if (!Save(error)) {
        workspace_ = previous;
        return false;
    }
    InvalidateMaterialization();
    return true;
}

bool DraftWorkspaceStore::CancelPromotion(std::string& error) {
    error.clear();
    if (!workspace_.has_value() || !workspace_->pending_promotion.has_value())
        return true;

    const DraftWorkspace previous = workspace_.value();
    workspace_->pending_promotion.reset();
    workspace_->state = DraftWorkspaceState::Active;
    ++workspace_->working_revision;
    RecordEvent("promotion_cancelled", {}, "Accepted SACM remained at the draft baseline");
    if (!Save(error)) {
        workspace_ = previous;
        return false;
    }
    InvalidateMaterialization();
    return true;
}

bool DraftWorkspaceStore::RemovePromotedGroups(const std::vector<std::string>& group_ids,
                                               const core::AssuranceCase& new_accepted,
                                               std::string& error) {
    error.clear();
    if (!workspace_.has_value())
        return true;

    const DraftWorkspace previous = workspace_.value();
    DraftWorkspace& workspace = workspace_.value();
    for (const std::string& group_id : group_ids) {
        const auto removed = std::remove_if(workspace.groups.begin(),
                                            workspace.groups.end(),
                                            [&](const DraftChangeGroup& group) { return group.id == group_id; });
        workspace.groups.erase(removed, workspace.groups.end());
    }

    // Rebased: the accepted argument these groups were written against is the
    // one the promotion just produced.
    workspace.base_model_hash = reviews::ComputeModelSemanticHash(new_accepted);
    workspace.pending_promotion.reset();
    workspace.state = DraftWorkspaceState::Active;
    ++workspace.working_revision;
    RecordEvent("groups_promoted", {}, std::to_string(group_ids.size()) + " groups");
    // Every entry below this point describes groups the accepted argument now
    // contains, so undoing into one would re-stage work the user has accepted.
    // Reversing the promotion is the audit stack's job, not this one's.
    ClearEditUndoHistory();
    InvalidateMaterialization();

    // Persist the finalized state before deleting recovery files. If directory
    // cleanup fails, the durable fallback is an empty finalized workspace, not
    // the old promotion marker and not resurrected operations.
    if (!Save(error)) {
        workspace_ = previous;
        return false;
    }

    if (!workspace.has_active_groups()) {
        const std::string key = ArgumentKey();
        workspace_.reset();
        if (!project_root_.empty()) {
            std::string cleanup_error;
            DeleteDraftWorkspace(project_root_, key, cleanup_error);
        }
        return true;
    }
    return true;
}

bool DraftWorkspaceStore::DiscardWorkspace(std::string& error) {
    error.clear();
    if (!workspace_.has_value())
        return true;
    if (workspace_->pending_promotion.has_value()) {
        error = "A draft promotion is awaiting durable completion and cannot be discarded.";
        return false;
    }
    const std::string key = ArgumentKey();
    workspace_.reset();
    ClearEditUndoHistory();
    InvalidateMaterialization();
    if (project_root_.empty())
        return true;
    return DeleteDraftWorkspace(project_root_, key, error);
}

bool DraftWorkspaceStore::SavePromotionSnapshot(std::uint64_t transaction_sequence,
                                                const DraftWorkspace& pre_promotion,
                                                std::string& error) const {
    error.clear();
    if (project_root_.empty()) {
        error = "Cannot record a draft promotion snapshot without a project root.";
        return false;
    }
    return SaveDraftPromotionSnapshot(project_root_, transaction_sequence, pre_promotion, error);
}

bool DraftWorkspaceStore::HasPromotionSnapshot(std::uint64_t transaction_sequence) const {
    if (project_root_.empty())
        return false;
    return DraftPromotionSnapshotExists(project_root_, transaction_sequence);
}

bool DraftWorkspaceStore::LoadPromotionSnapshot(std::uint64_t transaction_sequence,
                                                DraftWorkspace& snapshot,
                                                std::string& error) const {
    error.clear();
    if (project_root_.empty())
        return false;
    return LoadDraftPromotionSnapshot(project_root_, transaction_sequence, snapshot, error);
}

bool DraftWorkspaceStore::DeletePromotionSnapshot(std::uint64_t transaction_sequence, std::string& error) const {
    error.clear();
    if (project_root_.empty())
        return true;
    return DeleteDraftPromotionSnapshot(project_root_, transaction_sequence, error);
}

bool DraftWorkspaceStore::RestorePromotionSnapshot(const DraftWorkspace& snapshot,
                                                   const core::AssuranceCase& restored_accepted,
                                                   std::string& error) {
    error.clear();
    if (!WorkspaceTargetsArgumentFile(snapshot, argument_file_)) {
        error = "That promotion belongs to a different argument file.";
        return false;
    }

    const bool had_workspace = workspace_.has_value();
    const DraftWorkspace previous = had_workspace ? workspace_.value() : DraftWorkspace{};
    if (!had_workspace) {
        // The promotion consumed the last group, so `RemovePromotedGroups`
        // deleted the workspace. The snapshot is the whole of it.
        DraftWorkspace restored = snapshot;
        restored.argument_file = argument_file_;
        restored.pending_promotion.reset();
        restored.state = DraftWorkspaceState::Active;
        workspace_ = std::move(restored);
    } else {
        DraftWorkspace& workspace = workspace_.value();
        std::size_t reinstated = 0;
        for (const DraftChangeGroup& group : snapshot.groups) {
            if (workspace.FindGroup(group.id) != nullptr)
                continue;
            workspace.groups.push_back(group);
            ++reinstated;
        }
        if (reinstated == 0) {
            error = "That promotion's draft changes are already present.";
            return false;
        }
        std::sort(
            workspace.groups.begin(),
            workspace.groups.end(),
            [](const DraftChangeGroup& left, const DraftChangeGroup& right) { return left.sequence < right.sequence; });
        workspace.next_sequence = std::max(workspace.next_sequence, snapshot.next_sequence);
        workspace.pending_promotion.reset();
        workspace.state = DraftWorkspaceState::Active;
    }

    DraftWorkspace& workspace = workspace_.value();
    // The baseline moves back with the groups. Undo restored the argument these
    // groups were authored against, so leaving the post-promotion hash here would
    // call the draft stale against the very model it was written for.
    workspace.base_model_hash = reviews::ComputeModelSemanticHash(restored_accepted);
    workspace.working_revision = std::max(workspace.working_revision, snapshot.working_revision) + 1;
    RecordEvent("promotion_undone", {}, std::to_string(snapshot.groups.size()) + " groups restored");
    // The groups came back from a snapshot, not from an edit. Any entry still
    // here predates the promotion and describes a workspace built on a baseline
    // two accepted-model changes ago.
    ClearEditUndoHistory();
    InvalidateMaterialization();

    if (!Save(error)) {
        if (had_workspace) {
            workspace_ = previous;
        } else {
            workspace_.reset();
        }
        InvalidateMaterialization();
        return false;
    }
    return true;
}

std::shared_ptr<const DraftMaterializationResult>
DraftWorkspaceStore::MaterializeSnapshot(const core::AssuranceCase& accepted, std::uint64_t accepted_revision) {
    if (materialization_valid_ && materialized_accepted_revision_ == accepted_revision &&
        materialized_workspace_revision_ == revision()) {
        return materialization_;
    }

    std::shared_ptr<DraftMaterializationResult> next = std::make_shared<DraftMaterializationResult>();
    bool cacheable = true;

    if (workspace_.has_value() && workspace_->state != DraftWorkspaceState::NeedsRebase) {
        std::string conflicting_identity;
        for (const DraftChangeGroup* group : workspace_->ActiveGroups()) {
            for (const auto& generated : group->generated_ids) {
                if (authoritative_identities_.count(generated.second) > 0) {
                    conflicting_identity = generated.second;
                    break;
                }
            }
            if (!conflicting_identity.empty())
                break;
        }
        if (!conflicting_identity.empty()) {
            workspace_->state = DraftWorkspaceState::NeedsRebase;
            ++workspace_->working_revision;
            RecordEvent("identity_conflict",
                        {},
                        "Draft identity " + conflicting_identity + " is already used by the authoritative document");
            std::string save_error;
            Save(save_error);
        }
    }

    if (workspace_.has_value() && workspace_->state != DraftWorkspaceState::NeedsRebase &&
        !workspace_->base_model_hash.empty() &&
        workspace_->base_model_hash != reviews::ComputeModelSemanticHash(accepted)) {
        // Open() catches changes made while the argument was closed. This check
        // catches the equally important case where the accepted argument changes
        // while its draft remains open. Replaying against the new model would
        // turn ordinary drift into misleading patch or duplicate-id failures.
        workspace_->state = DraftWorkspaceState::NeedsRebase;
        ++workspace_->working_revision;
        RecordEvent("baseline_changed", {}, "Accepted argument changed while the draft was open");
        std::string save_error;
        Save(save_error);
    }

    if (!workspace_.has_value() || workspace_->state == DraftWorkspaceState::NeedsRebase ||
        workspace_->state == DraftWorkspaceState::Promoting) {
        // A workspace awaiting a rebase contributes nothing to what the user
        // sees. The accepted argument is shown, unmodified, until they decide.
        next->success = true;
        next->working_model = accepted;
    } else {
        const DraftWorkspace before_materialization = workspace_.value();
        *next = MaterializeDraft(workspace_.value(), accepted, authoritative_identities_);
        workspace_->state = next->success ? DraftWorkspaceState::Active : DraftWorkspaceState::Blocked;
        if (next->allocated_identities) {
            // Allocated once, then replayed forever. Persisting here is what
            // makes that true across a restart -- without it the next run would
            // allocate again and rename every proposed element.
            std::string save_error;
            if (!Save(save_error)) {
                // The materializer mutates the workspace as it pins ids. Roll
                // those allocations back if their durable write failed: an id
                // that exists only in memory has not been allocated once, and a
                // later retry must allocate and persist before publishing it.
                workspace_ = before_materialization;
                workspace_->state = DraftWorkspaceState::Blocked;
                *next = DraftMaterializationResult{};
                next->working_model = accepted;
                next->error = "Could not persist newly allocated draft identities: " + save_error;
                cacheable = false;
            }
        }
    }

    materialization_ = std::move(next);
    materialization_valid_ = cacheable;
    materialized_accepted_revision_ = accepted_revision;
    materialized_workspace_revision_ = revision();
    return materialization_;
}

const DraftMaterializationResult& DraftWorkspaceStore::Materialize(const core::AssuranceCase& accepted,
                                                                   std::uint64_t accepted_revision) {
    return *MaterializeSnapshot(accepted, accepted_revision);
}

} // namespace core::drafts
