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
    if (!stored.base_model_hash.empty() && stored.base_model_hash != accepted_hash) {
        // The argument moved underneath the draft. Nothing is replayed: the user
        // is offered inspect, rebase, export or discard, and until they choose,
        // the draft is inert rather than quietly applied to a document it was
        // not written for.
        stored.state = DraftWorkspaceState::NeedsRebase;
    } else if (stored.state == DraftWorkspaceState::NeedsRebase) {
        stored.state = DraftWorkspaceState::Active;
    }
    stored.argument_file = argument_file;

    workspace_ = std::move(stored);
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
    if (workspace.state == DraftWorkspaceState::NeedsRebase) {
        error = "The argument changed since this draft was written. Rebase or discard it before adding to it.";
        return {};
    }

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
    if (workspace.state == DraftWorkspaceState::NeedsRebase) {
        error = "The argument changed since this draft was written. Rebase or discard it before changing it.";
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

    const std::vector<reviews::PatchOperation> previous_operations = group->operations;
    const std::map<std::string, std::string> previous_identities = group->generated_ids;

    // Replacing drops the identities the old operations pinned. Any `create_ref`
    // the new operations reuse keeps its element id, so an author who rewords a
    // creation does not rename the element an agent was already told about;
    // anything it no longer creates releases its id.
    group->operations.clear();
    group->generated_ids.clear();
    if (!CanStageOperations(workspace, accepted, group_id, operations, error, authoritative_identities_)) {
        group->operations = previous_operations;
        group->generated_ids = previous_identities;
        return false;
    }

    group->operations = operations;
    for (const auto& [create_ref, id] : previous_identities) {
        for (const reviews::PatchOperation& operation : group->operations) {
            if (operation.create_ref.has_value() && operation.create_ref.value() == create_ref) {
                group->generated_ids[create_ref] = id;
                break;
            }
        }
    }
    group->updated_utc = NowUtcString();
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
    DraftChangeGroup* group = workspace_->FindGroup(group_id);
    if (group == nullptr) {
        error = "No draft change group with id " + group_id + ".";
        return false;
    }
    if (group->operations.empty()) {
        error = "Draft change group " + group_id + " has no operations to review.";
        return false;
    }
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
    DraftChangeGroup* group = workspace_->FindGroup(group_id);
    if (group == nullptr) {
        error = "No draft change group with id " + group_id + ".";
        return false;
    }
    if (group->state == DraftGroupState::Rejected)
        return true;

    group->state = DraftGroupState::Rejected;
    group->updated_utc = NowUtcString();
    ++workspace_->working_revision;
    RecordEvent("group_rejected", group_id, group->title);
    InvalidateMaterialization();
    return Save(error);
}

bool DraftWorkspaceStore::RemovePromotedGroups(const std::vector<std::string>& group_ids,
                                               const core::AssuranceCase& new_accepted,
                                               std::string& error) {
    error.clear();
    if (!workspace_.has_value())
        return true;

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
    ++workspace.working_revision;
    RecordEvent("groups_promoted", {}, std::to_string(group_ids.size()) + " groups");
    InvalidateMaterialization();

    // Nothing left to recover, so the recovery data goes rather than lingering
    // as an empty draft that the banner would have to explain.
    if (!workspace.has_active_groups()) {
        const std::string key = ArgumentKey();
        workspace_.reset();
        if (project_root_.empty())
            return true;
        return DeleteDraftWorkspace(project_root_, key, error);
    }
    return Save(error);
}

bool DraftWorkspaceStore::DiscardWorkspace(std::string& error) {
    error.clear();
    if (!workspace_.has_value())
        return true;
    const std::string key = ArgumentKey();
    workspace_.reset();
    InvalidateMaterialization();
    if (project_root_.empty())
        return true;
    return DeleteDraftWorkspace(project_root_, key, error);
}

std::shared_ptr<const DraftMaterializationResult>
DraftWorkspaceStore::MaterializeSnapshot(const core::AssuranceCase& accepted, std::uint64_t accepted_revision) {
    if (materialization_valid_ && materialized_accepted_revision_ == accepted_revision &&
        materialized_workspace_revision_ == revision()) {
        return materialization_;
    }

    std::shared_ptr<DraftMaterializationResult> next = std::make_shared<DraftMaterializationResult>();

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

    if (!workspace_.has_value() || workspace_->state == DraftWorkspaceState::NeedsRebase) {
        // A workspace awaiting a rebase contributes nothing to what the user
        // sees. The accepted argument is shown, unmodified, until they decide.
        next->success = true;
        next->working_model = accepted;
    } else {
        *next = MaterializeDraft(workspace_.value(), accepted, authoritative_identities_);
        if (next->allocated_identities) {
            // Allocated once, then replayed forever. Persisting here is what
            // makes that true across a restart -- without it the next run would
            // allocate again and rename every proposed element.
            std::string save_error;
            Save(save_error);
        }
        workspace_->state = next->success ? DraftWorkspaceState::Active : DraftWorkspaceState::Blocked;
    }

    materialization_ = std::move(next);
    materialization_valid_ = true;
    materialized_accepted_revision_ = accepted_revision;
    materialized_workspace_revision_ = revision();
    return materialization_;
}

const DraftMaterializationResult& DraftWorkspaceStore::Materialize(const core::AssuranceCase& accepted,
                                                                   std::uint64_t accepted_revision) {
    return *MaterializeSnapshot(accepted, accepted_revision);
}

} // namespace core::drafts
