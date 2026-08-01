#include "app/agent_request_handler.h"

#include "agent/operations.h"

#include <string>

namespace app {
namespace {

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
agent::Result
OpenCaseFile(const agent::ReadContext& read, const nlohmann::json& args, const AgentRequestContext& context) {
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
    return agent::GetCaseOverview(read);
}

bool IsChangeOperation(const std::string& op) {
    return op == "begin_change_set" || op == "stage_operations" || op == "unstage_operations" ||
           op == "describe_change_set" || op == "submit_change_set" || op == "discard_change_set" ||
           op == "list_change_sets";
}

} // namespace

bridge::Response HandleAgentRequest(const bridge::Request& request, const AgentRequestContext& context) {
    const agent::ReadContext read{context.state, context.project_path};

    if (request.op == "get_case_overview") {
        return FromAgentResult(request.id, agent::GetCaseOverview(read));
    }
    if (request.op == "find_elements") {
        return FromAgentResult(request.id, agent::FindElements(read, request.args));
    }
    if (request.op == "get_element") {
        return FromAgentResult(request.id, agent::GetElement(read, request.args));
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
        return FromAgentResult(request.id, OpenCaseFile(read, request.args, context));
    }

    if (IsChangeOperation(request.op)) {
        if (context.change_sets == nullptr) {
            return FromAgentResult(request.id,
                                   agent::Result::Error("This is a standalone SACM file rather than a project, so "
                                                        "there is nowhere to record a proposed change."));
        }
        const agent::ChangeContext change{
            context.state, *context.change_sets, context.connection_id, context.client_label};

        if (request.op == "begin_change_set") {
            return FromAgentResult(request.id, agent::BeginChangeSet(change, request.args));
        }
        if (request.op == "stage_operations") {
            return FromAgentResult(request.id, agent::StageOperations(change, request.args));
        }
        if (request.op == "unstage_operations") {
            return FromAgentResult(request.id, agent::UnstageOperations(change, request.args));
        }
        if (request.op == "describe_change_set") {
            return FromAgentResult(request.id, agent::DescribeChangeSet(change, request.args));
        }
        if (request.op == "submit_change_set") {
            return FromAgentResult(request.id, agent::SubmitChangeSet(change, request.args));
        }
        if (request.op == "discard_change_set") {
            return FromAgentResult(request.id, agent::DiscardChangeSet(change, request.args));
        }
        return FromAgentResult(request.id, agent::ListChangeSets(change));
    }

    return bridge::MakeError(request.id,
                             bridge::error_code::kUnknownOperation,
                             "Assurance Forge does not support the operation \"" + request.op +
                                 "\". It may come from a newer assurance-forge-mcp than this "
                                 "application.");
}

} // namespace app
