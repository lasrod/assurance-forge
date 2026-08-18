#include "app/agent_request_handler.h"

#include "agent/operations.h"

#include <string>

namespace app {
namespace {

agent::ReadContext CurrentReadContext(const AgentRequestContext& context) {
    AgentArgumentView view;
    if (context.current_argument_view)
        view = context.current_argument_view();
    return agent::ReadContext{context.state,
                              context.project_path,
                              context.context_generation,
                              view.model,
                              view.workspace,
                              view.is_working_draft};
}

// A read operation reports "no case" or "unknown element" as a *tool* failure,
// not a transport failure: the model reading it should adjust and try again,
// rather than see the connection fault on something it can fix. The bridge
// distinguishes the two the same way MCP does, so `ok` stays true and the
// payload carries the error.
bridge::Response FromAgentResult(std::uint64_t id, const agent::Result& result) {
    bridge::Response response = bridge::MakeResult(id, result.payload);
    response.result["isError"] = result.is_error;
    return response;
}

// Moving the application to another argument file is a visible act: the user's
// canvas changes under them. That is deliberate and preferable to the
// alternative, which is an agent silently reasoning about a document nobody is
// looking at -- the fault that made an earlier design propose changes against
// the wrong file.
agent::Result OpenCaseFile(const nlohmann::json& args, const AgentRequestContext& context) {
    if (!context.state.current_project.has_value()) {
        return agent::Result::Error("This is a standalone SACM file, so there is nothing to switch between.");
    }
    const nlohmann::json::const_iterator path = args.find("path");
    if (path == args.end() || !path->is_string() || path->get<std::string>().empty()) {
        return agent::Result::Error("Argument \"path\" is required; call list_case_files for the "
                                    "paths this project holds.");
    }
    if (!context.open_case_file) {
        return agent::Result::Error("Assurance Forge cannot switch argument files right now.");
    }

    std::string error;
    if (!context.open_case_file(path->get<std::string>(), error)) {
        return agent::Result::Error(error);
    }
    // Re-read so the caller sees the case it moved to, not the one it left.
    return agent::GetCaseOverview(CurrentReadContext(context));
}

bool IsDraftOperation(const std::string& op) {
    return op == "get_draft_status" || op == "begin_change_group" || op == "begin_change_set" ||
           op == "stage_operations" || op == "replace_change_group" || op == "remove_change_group" ||
           op == "describe_change_group" || op == "describe_change_set" || op == "submit_change_group" ||
           op == "submit_change_set" || op == "describe_working_draft" || op == "get_draft_events" ||
           op == "close_change_group" || op == "unstage_operations" || op == "discard_change_set" ||
           op == "list_change_sets";
}

} // namespace

bridge::Response HandleAgentRequest(const bridge::Request& request, const AgentRequestContext& context) {
    const agent::ReadContext read = CurrentReadContext(context);

    if (request.op == "get_case_overview") {
        return FromAgentResult(request.id, agent::GetCaseOverview(read));
    }
    if (request.op == "find_elements") {
        return FromAgentResult(request.id, agent::FindElements(read, request.args));
    }
    if (request.op == "get_element") {
        return FromAgentResult(request.id, agent::GetElement(read, request.args));
    }
    if (request.op == "list_terms") {
        return FromAgentResult(request.id, agent::ListTerms(read));
    }
    if (request.op == "list_assurance_claim_points") {
        return FromAgentResult(request.id, agent::ListAssuranceClaimPoints(read));
    }
    if (request.op == "get_argument_tree") {
        return FromAgentResult(request.id, agent::GetArgumentTree(read, request.args));
    }
    if (request.op == "list_case_files") {
        return FromAgentResult(request.id, agent::ListCaseFiles(read));
    }
    if (request.op == "suggest_placement") {
        return FromAgentResult(request.id, agent::SuggestPlacement(read, request.args));
    }
    if (request.op == "open_case_file") {
        return FromAgentResult(request.id, OpenCaseFile(request.args, context));
    }

    if (IsDraftOperation(request.op)) {
        if (context.draft_workspace == nullptr) {
            return FromAgentResult(request.id,
                                   agent::Result::Error("This is a standalone SACM file rather than a project, so "
                                                        "there is nowhere to record a proposed change."));
        }
        agent::DraftContext draft{context.state,
                                  *context.draft_workspace,
                                  context.connection_id,
                                  context.client_label,
                                  context.source_session_id,
                                  context.context_generation};
        // Taken from the read view the handler already computed, so a mutation
        // and a read in the same session never disagree about whether the
        // client is looking at accepted content.
        draft.working_draft_active = read.is_working_draft;
        draft.document = context.draft_document;
        draft.accepted_document = context.state.library_document.get();

        if (request.op == "get_draft_status" || request.op == "list_change_sets") {
            return FromAgentResult(request.id, agent::GetDraftStatus(draft));
        }
        if (request.op == "begin_change_group" || request.op == "begin_change_set")
            return FromAgentResult(request.id, agent::BeginChangeGroup(draft, request.args));
        if (request.op == "stage_operations") {
            return FromAgentResult(request.id, agent::StageDraftOperations(draft, request.args));
        }
        if (request.op == "replace_change_group")
            return FromAgentResult(request.id, agent::ReplaceChangeGroup(draft, request.args));
        if (request.op == "remove_change_group" || request.op == "discard_change_set")
            return FromAgentResult(request.id, agent::RemoveChangeGroup(draft, request.args));
        if (request.op == "describe_change_group" || request.op == "describe_change_set")
            return FromAgentResult(request.id, agent::DescribeChangeGroup(draft, request.args));
        if (request.op == "submit_change_group" || request.op == "submit_change_set")
            return FromAgentResult(request.id, agent::SubmitChangeGroup(draft, request.args));
        if (request.op == "describe_working_draft")
            return FromAgentResult(request.id, agent::DescribeWorkingDraft(draft));
        if (request.op == "get_draft_events")
            return FromAgentResult(request.id, agent::GetDraftEvents(draft, request.args));
        if (request.op == "close_change_group")
            return FromAgentResult(request.id, agent::CloseChangeGroup(draft, request.args));
        return FromAgentResult(request.id, agent::UnstageDraftOperations(draft, request.args));
    }

    return bridge::MakeError(request.id,
                             bridge::error_code::kUnknownOperation,
                             "Assurance Forge does not support the operation \"" + request.op +
                                 "\". It may come from a newer assurance-forge-mcp than this "
                                 "application.");
}

} // namespace app
