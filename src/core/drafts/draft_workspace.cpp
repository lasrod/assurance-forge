#include "core/drafts/draft_workspace.h"

#include <algorithm>
#include <cctype>

namespace core::drafts {
namespace {

struct SourceName {
    DraftSource source;
    const char* name;
};

constexpr SourceName kSourceNames[] = {
    {DraftSource::Human, "human"},
    {DraftSource::Mcp, "mcp"},
    {DraftSource::SccgAiReview, "sccg_ai_review"},
    {DraftSource::ImportedLegacyProposal, "imported_legacy_proposal"},
};

struct GroupStateName {
    DraftGroupState state;
    const char* name;
};

constexpr GroupStateName kGroupStateNames[] = {
    {DraftGroupState::Building, "building"},
    {DraftGroupState::Ready, "ready"},
    {DraftGroupState::NeedsAttention, "needs_attention"},
    {DraftGroupState::Rejected, "rejected"},
};

struct WorkspaceStateName {
    DraftWorkspaceState state;
    const char* name;
};

constexpr WorkspaceStateName kWorkspaceStateNames[] = {
    {DraftWorkspaceState::Active, "active"},
    {DraftWorkspaceState::Promoting, "promoting"},
    {DraftWorkspaceState::NeedsRebase, "needs_rebase"},
    {DraftWorkspaceState::Blocked, "blocked"},
    {DraftWorkspaceState::Closed, "closed"},
};

} // namespace

const char* DraftSourceToString(DraftSource source) {
    for (const SourceName& entry : kSourceNames) {
        if (entry.source == source)
            return entry.name;
    }
    return "human";
}

bool DraftSourceFromString(const std::string& value, DraftSource& source) {
    for (const SourceName& entry : kSourceNames) {
        if (value == entry.name) {
            source = entry.source;
            return true;
        }
    }
    return false;
}

const char* DraftGroupStateToString(DraftGroupState state) {
    for (const GroupStateName& entry : kGroupStateNames) {
        if (entry.state == state)
            return entry.name;
    }
    return "building";
}

bool DraftGroupStateFromString(const std::string& value, DraftGroupState& state) {
    for (const GroupStateName& entry : kGroupStateNames) {
        if (value == entry.name) {
            state = entry.state;
            return true;
        }
    }
    return false;
}

const char* DraftWorkspaceStateToString(DraftWorkspaceState state) {
    for (const WorkspaceStateName& entry : kWorkspaceStateNames) {
        if (entry.state == state)
            return entry.name;
    }
    return "active";
}

bool DraftWorkspaceStateFromString(const std::string& value, DraftWorkspaceState& state) {
    for (const WorkspaceStateName& entry : kWorkspaceStateNames) {
        if (value == entry.name) {
            state = entry.state;
            return true;
        }
    }
    return false;
}

const DraftChangeGroup* DraftWorkspace::FindGroup(const std::string& group_id) const {
    for (const DraftChangeGroup& group : groups) {
        if (group.id == group_id)
            return &group;
    }
    return nullptr;
}

DraftChangeGroup* DraftWorkspace::FindGroup(const std::string& group_id) {
    for (DraftChangeGroup& group : groups) {
        if (group.id == group_id)
            return &group;
    }
    return nullptr;
}

std::vector<const DraftChangeGroup*> DraftWorkspace::ActiveGroups() const {
    std::vector<const DraftChangeGroup*> active;
    active.reserve(groups.size());
    for (const DraftChangeGroup& group : groups) {
        if (group.active())
            active.push_back(&group);
    }
    // By sequence and nothing else. The stored order is whatever the store
    // happened to append; the working model must not depend on it.
    std::stable_sort(active.begin(), active.end(), [](const DraftChangeGroup* left, const DraftChangeGroup* right) {
        return left->sequence < right->sequence;
    });
    return active;
}

bool DraftWorkspace::has_active_groups() const {
    // `recoverable()`, not `active()`. This decides whether the workspace is
    // deleted, and a draft left holding only stranded groups still holds work
    // nobody has accepted or rejected.
    return std::any_of(groups.begin(), groups.end(), [](const DraftChangeGroup& group) { return group.recoverable(); });
}

std::size_t DraftWorkspace::staged_group_count() const {
    std::size_t count = 0;
    for (const DraftChangeGroup& group : groups) {
        // A stranded group is counted although it does not draw. It is still an
        // unaccepted change and still needs a decision; leaving it out of the
        // banner is how work goes quiet and then goes missing.
        if (group.recoverable() && !group.operations.empty())
            ++count;
    }
    return count;
}

bool WorkspaceTargetsArgumentFile(const DraftWorkspace& workspace, const std::filesystem::path& argument_file) {
    if (workspace.argument_file.empty())
        return true;
    if (argument_file.empty())
        return false;

    std::error_code ec;
    const std::filesystem::path workspace_path = std::filesystem::weakly_canonical(workspace.argument_file, ec);
    const std::filesystem::path candidate_path = std::filesystem::weakly_canonical(argument_file, ec);
    if (!workspace_path.empty() && !candidate_path.empty())
        return workspace_path == candidate_path;
    return workspace.argument_file == argument_file;
}

std::string ArgumentStableKey(const std::filesystem::path& project_relative_argument_file) {
    std::string key = project_relative_argument_file.generic_string();
    std::string normalized;
    normalized.reserve(key.size());
    for (char character : key) {
        const unsigned char raw = static_cast<unsigned char>(character);
        if (character == '/' || character == '\\') {
            normalized.push_back('_');
        } else if (std::isalnum(raw) != 0 || character == '-' || character == '.') {
            normalized.push_back(static_cast<char>(std::tolower(raw)));
        } else {
            normalized.push_back('_');
        }
    }
    if (normalized.empty())
        normalized = "argument";
    return normalized;
}

} // namespace core::drafts
