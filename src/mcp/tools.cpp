#include "mcp/tools.h"

#include "mcp/session.h"

#include "agent/operations.h"

#include <string>

namespace mcp {
namespace {

// Every tool has the same shape: name an operation, hand over the arguments,
// carry the result back into MCP vocabulary. The session decides whether that
// operation is answered by a running Assurance Forge or by a copy this process
// loaded, and no tool knows which -- which is what stops the two modes
// answering differently.
ToolResult Run(Session& session, const char* op, const nlohmann::json& arguments) {
    const Session::OperationResult result = session.Run(op, arguments);
    return ToolResult{result.payload, result.is_error};
}

ToolResult GetCaseOverview(Session& session, const nlohmann::json& arguments) {
    return Run(session, "get_case_overview", arguments);
}

ToolResult FindElements(Session& session, const nlohmann::json& arguments) {
    return Run(session, "find_elements", arguments);
}

ToolResult GetElement(Session& session, const nlohmann::json& arguments) {
    return Run(session, "get_element", arguments);
}

ToolResult GetArgumentTree(Session& session, const nlohmann::json& arguments) {
    return Run(session, "get_argument_tree", arguments);
}

ToolResult ListCaseFiles(Session& session, const nlohmann::json& arguments) {
    return Run(session, "list_case_files", arguments);
}

ToolResult OpenCaseFile(Session& session, const nlohmann::json& arguments) {
    return Run(session, "open_case_file", arguments);
}

std::vector<ToolDefinition> BuildTools() {
    std::vector<ToolDefinition> tools;

    tools.push_back(ToolDefinition{
        "get_case_overview",
        "Summarize the loaded assurance case: name, element counts by SACM type, "
        "assurance claim points, and any warnings raised when the file was loaded.",
        nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}},
        true,
        &GetCaseOverview,
    });

    tools.push_back(ToolDefinition{
        "find_elements",
        "Search the assurance case for elements whose id, name, content or description "
        "contains a substring. Case-insensitive. Optionally filter by SACM type.",
        nlohmann::json{
            {"type", "object"},
            {"properties",
             {{"query", {{"type", "string"}, {"description", "Case-insensitive substring."}}},
              {"type",
               {{"type", "string"},
                {"description", "SACM type filter, e.g. \"claim\", \"argumentreasoning\"."}}},
              {"limit",
               {{"type", "integer"},
                {"description", "Maximum elements to return (default " +
                                    std::to_string(agent::kDefaultResultLimit) + ", maximum " +
                                    std::to_string(agent::kMaxResultLimit) + ")."}}}}}},
        true,
        &FindElements,
    });

    tools.push_back(ToolDefinition{
        "get_element",
        "Fetch one element by id with its full fields, its incoming and outgoing SACM "
        "relationships, and its GSN role and immediate neighbours.",
        nlohmann::json{{"type", "object"},
                       {"properties",
                        {{"id", {{"type", "string"}, {"description", "Element id."}}}}},
                       {"required", nlohmann::json::array({"id"})}},
        true,
        &GetElement,
    });

    tools.push_back(ToolDefinition{
        "list_case_files",
        "List the SACM argument files this project holds and which one is currently loaded. A "
        "project can contain several arguments; the other tools read the loaded one.",
        nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}},
        true,
        &ListCaseFiles,
    });

    tools.push_back(ToolDefinition{
        "open_case_file",
        "Switch which of the project's argument files the other tools read, and return an "
        "overview of it. Use a path from list_case_files. When Assurance Forge is running, this "
        "moves what the user is looking at too.",
        nlohmann::json{
            {"type", "object"},
            {"properties",
             {{"path",
               {{"type", "string"},
                {"description", "Project-relative path, e.g. \"arguments/main2.sacm\"."}}}}},
            {"required", nlohmann::json::array({"path"})}},
        true,
        &OpenCaseFile,
    });

    tools.push_back(ToolDefinition{
        "get_argument_tree",
        "Return the GSN argument structure as a nested tree of supported-by and "
        "in-context-of links. Truncated branches are marked so a partial tree is never "
        "mistaken for a complete one.",
        nlohmann::json{
            {"type", "object"},
            {"properties",
             {{"root_id",
               {{"type", "string"},
                {"description", "Element to root the tree at. Defaults to the case's top goal."}}},
              {"depth",
               {{"type", "integer"},
                {"description", "Levels to descend (default " +
                                    std::to_string(agent::kDefaultTreeDepth) + ", maximum " +
                                    std::to_string(agent::kMaxTreeDepth) + ")."}}}}}},
        true,
        &GetArgumentTree,
    });

    return tools;
}

} // namespace

ToolResult ToolResult::Ok(nlohmann::json payload) {
    return ToolResult{std::move(payload), false};
}

ToolResult ToolResult::Error(std::string message) {
    return ToolResult{nlohmann::json{{"error", std::move(message)}}, true};
}

const std::vector<ToolDefinition>& BuiltinTools() {
    static const std::vector<ToolDefinition> tools = BuildTools();
    return tools;
}

const ToolDefinition* FindTool(std::string_view name) {
    for (const ToolDefinition& tool : BuiltinTools()) {
        if (tool.name == name) {
            return &tool;
        }
    }
    return nullptr;
}

} // namespace mcp
