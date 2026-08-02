#include "core/drafts/draft_workspace_store.h"

#include "core/drafts/draft_persistence.h"
#include "core/reviews/review_proposal.h"
#include "core/time_utils.h"

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
    materialization_ = DraftMaterializationResult{};
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
    if (!CanStageOperations(workspace, accepted, group_id, operations, error))
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
    if (!CanStageOperations(workspace, accepted, group_id, operations, error)) {
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

const DraftMaterializationResult& DraftWorkspaceStore::Materialize(const core::AssuranceCase& accepted,
                                                                   std::uint64_t accepted_revision) {
    if (materialization_valid_ && materialized_accepted_revision_ == accepted_revision &&
        materialized_workspace_revision_ == revision()) {
        return materialization_;
    }

    if (!workspace_.has_value() || workspace_->state == DraftWorkspaceState::NeedsRebase) {
        // A workspace awaiting a rebase contributes nothing to what the user
        // sees. The accepted argument is shown, unmodified, until they decide.
        materialization_ = DraftMaterializationResult{};
        materialization_.success = true;
        materialization_.working_model = accepted;
    } else {
        materialization_ = MaterializeDraft(workspace_.value(), accepted);
        if (materialization_.allocated_identities) {
            // Allocated once, then replayed forever. Persisting here is what
            // makes that true across a restart -- without it the next run would
            // allocate again and rename every proposed element.
            std::string save_error;
            Save(save_error);
        }
        workspace_->state = materialization_.success ? DraftWorkspaceState::Active : DraftWorkspaceState::Blocked;
    }

    materialization_valid_ = true;
    materialized_accepted_revision_ = accepted_revision;
    materialized_workspace_revision_ = revision();
    return materialization_;
}

} // namespace core::drafts
