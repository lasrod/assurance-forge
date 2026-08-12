#pragma once

// What an AI agent can ask of a safety case.
//
// These operations are executed in two places and must behave identically in
// both: inside the running application, against the model the user is looking
// at, and inside `assurance-forge-mcp` when no application is running, against
// a copy it loaded itself. Writing them twice would let the two drift, and the
// drift would show up as an agent that answers differently depending on whether
// a window happens to be open.
//
// **This layer is the domain vocabulary; `bridge/` is the transport.** The
// separation is what lets the wire contract stay ignorant of safety cases while
// these functions stay ignorant of pipes.
//
// Read operations only. Everything that changes a safety case goes through a
// change set and the command bus, and is therefore available only where a
// command bus exists -- which is to say, only in the application.

#include "core/app_state.h"
#include "core/changesets/change_set_store.h"
#include "core/drafts/draft_workspace.h"
#include "core/drafts/draft_workspace_store.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace agent {

struct Result {
    nlohmann::json payload = nlohmann::json::object();
    bool is_error = false;

    static Result Ok(nlohmann::json payload);
    // Worded for a model to read and act on, not for a log.
    static Result Error(std::string message);
};

// Everything a read operation needs. Deliberately a reference to state the
// caller owns: online this is the application's live `AppState`, so an
// operation sees the user's current edits with no copying and no locking --
// it runs on the frame thread, which is the thread that owns the model.
struct ReadContext {
    const core::AppState& state;
    // How this case was addressed: the project directory, or the file itself
    // when a bare SACM document was opened outside a project.
    std::string project_path;

    // Connected mode supplies the immutable integrated working model that the
    // application is rendering. Offline mode leaves this null and therefore
    // reads only the accepted model loaded from SACM.
    const parser::AssuranceCase* argument_view = nullptr;
    const core::drafts::DraftWorkspace* draft_workspace = nullptr;
    bool is_working_draft = false;

    const parser::AssuranceCase* argument() const {
        if (argument_view != nullptr)
            return argument_view;
        return state.loaded_case.has_value() ? &state.loaded_case.value() : nullptr;
    }
};

// Adds the read-view contract shared by every case-content operation. An MCP
// client must never have to guess whether returned argument text is accepted or
// still part of the integrated draft.
Result ReadResult(const ReadContext& context, nlohmann::json payload);

Result GetCaseOverview(const ReadContext& context);
Result FindElements(const ReadContext& context, const nlohmann::json& arguments);
Result GetElement(const ReadContext& context, const nlohmann::json& arguments);
// Every assurance claim point in the case: id, name, what it annotates, and how
// it is resolved (inline text or a confidence argument). Read-only: the patch
// vocabulary has no ACP operation, and extending it is an ADR 0009 decision.
Result ListAssuranceClaimPoints(const ReadContext& context);
Result GetArgumentTree(const ReadContext& context, const nlohmann::json& arguments);
Result ListCaseFiles(const ReadContext& context);

// Where new argument about a topic would fit. Ranked candidate anchors with the
// structural facts behind each: the path from the root, the sibling claims
// already there, and the context in scope.
//
// The agent still decides. This exists because it cannot decide well from
// substring search alone -- `find_elements` says where a word appears, which is
// not the same question as where an argument belongs, and an agent left to guess
// attaches things at the root.
Result SuggestPlacement(const ReadContext& context, const nlohmann::json& arguments);

// ---------------------------------------------------------------------------
// Change sets
//
// Building a change is available only where a command bus, an audit log and a
// human to accept it exist -- which is to say, only inside the application.
// These take the store as well as the model for that reason.
// ---------------------------------------------------------------------------

struct ChangeContext {
    const core::AppState& state;
    core::changesets::ChangeSetStore& store;
    // Which connection is asking, so a client's operations reach its own change
    // set and not another client's.
    std::uint64_t connection_id = 0;
    std::string client_label;
};

Result BeginChangeSet(const ChangeContext& context, const nlohmann::json& arguments);
Result StageOperations(const ChangeContext& context, const nlohmann::json& arguments);
Result UnstageOperations(const ChangeContext& context, const nlohmann::json& arguments);
Result DescribeChangeSet(const ChangeContext& context, const nlohmann::json& arguments);
Result SubmitChangeSet(const ChangeContext& context, const nlohmann::json& arguments);
Result DiscardChangeSet(const ChangeContext& context, const nlohmann::json& arguments);
Result ListChangeSets(const ChangeContext& context);

// ---------------------------------------------------------------------------
// Integrated draft groups
//
// MCP now contributes to the same persisted workspace as human draft editing
// and SCCG review. The accepted AppState remains the immutable baseline; every
// mutation is revision checked and touches only DraftWorkspaceStore.
// ---------------------------------------------------------------------------

struct DraftContext {
    const core::AppState& state;
    core::drafts::DraftWorkspaceStore& store;
    std::uint64_t connection_id = 0;
    std::string client_label;
    std::string session_id;
};

Result GetDraftStatus(const DraftContext& context);
Result BeginChangeGroup(const DraftContext& context, const nlohmann::json& arguments);
Result StageDraftOperations(const DraftContext& context, const nlohmann::json& arguments);
Result ReplaceChangeGroup(const DraftContext& context, const nlohmann::json& arguments);
Result RemoveChangeGroup(const DraftContext& context, const nlohmann::json& arguments);
Result DescribeChangeGroup(const DraftContext& context, const nlohmann::json& arguments);
Result SubmitChangeGroup(const DraftContext& context, const nlohmann::json& arguments);
Result DescribeWorkingDraft(const DraftContext& context);
Result GetDraftEvents(const DraftContext& context, const nlohmann::json& arguments);
Result CloseChangeGroup(const DraftContext& context, const nlohmann::json& arguments);
Result UnstageDraftOperations(const DraftContext& context, const nlohmann::json& arguments);

// Parses the operation vocabulary an agent sends. Exposed so the operation
// schema published over MCP and the parser that enforces it stay one thing.
bool ParsePatchOperations(const nlohmann::json& source,
                          std::vector<core::reviews::PatchOperation>& out,
                          std::string& error);

// The operation type names an agent may use, in schema order.
const std::vector<std::string>& PatchOperationTypeNames();

// A chat client pays for every token an operation returns, and a real safety
// case does not fit in a context window. These are a correctness requirement
// rather than a nicety: an unbounded search would spend the whole conversation
// on one call. Exposed so the tool descriptions can quote the real numbers.
inline constexpr int kDefaultResultLimit = 50;
inline constexpr int kMaxResultLimit = 200;
inline constexpr int kDefaultTreeDepth = 4;
inline constexpr int kMaxTreeDepth = 12;

} // namespace agent
