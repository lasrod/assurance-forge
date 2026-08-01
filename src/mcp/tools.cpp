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

ToolResult SuggestPlacement(Session& session, const nlohmann::json& arguments) {
    return Run(session, "suggest_placement", arguments);
}

ToolResult BeginChangeSet(Session& session, const nlohmann::json& arguments) {
    return Run(session, "begin_change_set", arguments);
}

ToolResult StageOperations(Session& session, const nlohmann::json& arguments) {
    return Run(session, "stage_operations", arguments);
}

ToolResult UnstageOperations(Session& session, const nlohmann::json& arguments) {
    return Run(session, "unstage_operations", arguments);
}

ToolResult DescribeChangeSet(Session& session, const nlohmann::json& arguments) {
    return Run(session, "describe_change_set", arguments);
}

ToolResult SubmitChangeSet(Session& session, const nlohmann::json& arguments) {
    return Run(session, "submit_change_set", arguments);
}

ToolResult DiscardChangeSet(Session& session, const nlohmann::json& arguments) {
    return Run(session, "discard_change_set", arguments);
}

ToolResult ListChangeSets(Session& session, const nlohmann::json& arguments) {
    return Run(session, "list_change_sets", arguments);
}

// The operation vocabulary, published from the same list the parser enforces so
// the schema and the check cannot drift.
nlohmann::json OperationTypeEnum() {
    nlohmann::json values = nlohmann::json::array();
    for (const std::string& name : agent::PatchOperationTypeNames()) {
        values.push_back(name);
    }
    return values;
}

nlohmann::json OperationsSchema() {
    return nlohmann::json{
        {"type", "array"},
        {"description",
         "Patch operations, applied in order. Support runs upwards: "
         "{\"type\":\"AddSupportedBy\",\"source\":{\"ref\":\"$sub\"},"
         "\"target\":{\"id\":\"G1\"}} puts the new element UNDER G1."},
        {"items",
         {{"type", "object"},
          {"properties",
           {{"type", {{"type", "string"}, {"enum", OperationTypeEnum()}}},
            {"create_ref",
             {{"type", "string"},
              {"description",
               "For Create* operations: a name starting with '$' that later "
               "operations refer to as {\"ref\": \"$name\"}."}}},
            {"element",
             {{"type", "object"},
              {"description",
               "Target of an update or removal: {\"id\": \"G1\"} or "
               "{\"ref\": \"$goal\"}."}}},
            // The direction, spelled out. It was "Relationship source." and
            // "Relationship target.", which says nothing, and a client given
            // that has even odds of hanging the case's top goal underneath a
            // claim it has just invented -- an upside-down safety argument, from
            // one ambiguous word.
            {"source",
             {{"type", "object"},
              {"description",
               "The SUPPORTING element -- the one that ends up BELOW. For "
               "AddSupportedBy, the new sub-claim, strategy or solution."}}},
            {"target",
             {{"type", "object"},
              {"description",
               "The SUPPORTED element -- the one that ends up ABOVE. For "
               "AddSupportedBy, the existing goal you are developing. Read it as "
               "\"target is supported by source\"."}}},
            {"field", {{"type", "string"}, {"description", "For UpdateElementText: which field, e.g. \"content\"."}}},
            {"old_value", {{"type", "string"}}},
            {"new_value", {{"type", "string"}}},
            {"text", {{"type", "string"}, {"description", "Initial text for a Create* operation."}}}}}}}};
}

nlohmann::json ChangeSetIdSchema() {
    return nlohmann::json{
        {"change_set_id",
         {{"type", "string"}, {"description", "Defaults to the change set this connection has open."}}}};
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
               {{"type", "string"}, {"description", "SACM type filter, e.g. \"claim\", \"argumentreasoning\"."}}},
              {"limit",
               {{"type", "integer"},
                {"description",
                 "Maximum elements to return (default " + std::to_string(agent::kDefaultResultLimit) + ", maximum " +
                     std::to_string(agent::kMaxResultLimit) + ")."}}}}}},
        true,
        &FindElements,
    });

    tools.push_back(ToolDefinition{
        "get_element",
        "Fetch one element by id with its full fields, its incoming and outgoing SACM "
        "relationships, and its GSN role and immediate neighbours.",
        nlohmann::json{{"type", "object"},
                       {"properties", {{"id", {{"type", "string"}, {"description", "Element id."}}}}},
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
               {{"type", "string"}, {"description", "Project-relative path, e.g. \"arguments/main2.sacm\"."}}}}},
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
               {{"type", "string"}, {"description", "Element to root the tree at. Defaults to the case's top goal."}}},
              {"depth",
               {{"type", "integer"},
                {"description",
                 "Levels to descend (default " + std::to_string(agent::kDefaultTreeDepth) + ", maximum " +
                     std::to_string(agent::kMaxTreeDepth) + ")."}}}}}},
        true,
        &GetArgumentTree,
    });

    tools.push_back(ToolDefinition{
        "suggest_placement",
        "Find where new argument about a topic would fit. Returns ranked candidate anchors with "
        "the path from the top goal, the sub-claims already there, and the context in scope, so "
        "you can attach to the branch this extends instead of guessing. Use this before staging "
        "anything that adds argument to an existing case.",
        nlohmann::json{
            {"type", "object"},
            {"properties",
             {{"topic", {{"type", "string"}, {"description", "What the new argument would establish."}}},
              {"limit",
               {{"type", "integer"}, {"description", "Maximum candidates to return (default 5, maximum 20)."}}}}},
            {"required", nlohmann::json::array({"topic"})}},
        true,
        &SuggestPlacement,
    });

    // ---------------------------------------------------------------------
    // Change sets
    //
    // The descriptions carry one message repeatedly and deliberately: staging
    // does not change the safety case. An agent that believes it has edited the
    // argument will say so, and the user will believe it.
    // ---------------------------------------------------------------------

    tools.push_back(ToolDefinition{
        "begin_change_set",
        "Start a change the user watches you build. From this call onward, Assurance Forge shows "
        "your work on the canvas as you stage it: new elements appear in a proposed style, edits "
        "are marked in place, removals are ghosted. Nothing is applied to the safety case -- the "
        "user accepts or rejects the finished change set in the application. Requires Assurance "
        "Forge to be running with the project open.",
        nlohmann::json{
            {"type", "object"},
            {"properties",
             {{"title", {{"type", "string"}, {"description", "Short title the user sees while you build this."}}},
              {"summary", {{"type", "string"}, {"description", "What the change does."}}},
              {"intent",
               {{"type", "string"},
                {"description",
                 "Why you are making it. The reviewer is being asked to accept a "
                 "change to a safety argument and needs the reasoning."}}}}},
            {"required", nlohmann::json::array({"title"})}},
        true,
        &BeginChangeSet,
    });

    tools.push_back(ToolDefinition{
        "stage_operations",
        "Add operations to the open change set. They are checked against the current case and "
        "refused as a group if they would not apply, so a clean result means the user is now "
        "seeing exactly this on the canvas. Returns what changed and the ids given to elements "
        "you created. This does NOT change the safety case.",
        nlohmann::json{{"type", "object"},
                       {"properties",
                        [&] {
                            nlohmann::json properties = ChangeSetIdSchema();
                            properties["operations"] = OperationsSchema();
                            return properties;
                        }()},
                       {"required", nlohmann::json::array({"operations"})}},
        true,
        &StageOperations,
    });

    tools.push_back(ToolDefinition{
        "unstage_operations",
        "Remove the most recently staged operations from the open change set, so you can revise "
        "after the user says it is not what they wanted rather than starting over.",
        nlohmann::json{{"type", "object"},
                       {"properties",
                        [&] {
                            nlohmann::json properties = ChangeSetIdSchema();
                            properties["count"] = nlohmann::json{
                                {"type", "integer"}, {"description", "How many to drop from the end (default 1)."}};
                            return properties;
                        }()}},
        true,
        &UnstageOperations,
    });

    tools.push_back(ToolDefinition{
        "describe_change_set",
        "Report what a change set would do to the case: elements added, modified and removed, "
        "with their text. Also reports whether it still applies -- the user may have edited the "
        "argument while you were working.",
        nlohmann::json{{"type", "object"}, {"properties", ChangeSetIdSchema()}},
        true,
        &DescribeChangeSet,
    });

    tools.push_back(ToolDefinition{
        "submit_change_set",
        "Hand the change set to the user for a decision. After this, tell them it is waiting for "
        "review in Assurance Forge. Do not tell them the safety case has been changed: it has "
        "not, and only they can change it.",
        nlohmann::json{{"type", "object"}, {"properties", ChangeSetIdSchema()}},
        true,
        &SubmitChangeSet,
    });

    tools.push_back(ToolDefinition{
        "discard_change_set",
        "Abandon a change set and remove it from the user's canvas.",
        nlohmann::json{{"type", "object"}, {"properties", ChangeSetIdSchema()}},
        true,
        &DiscardChangeSet,
    });

    tools.push_back(ToolDefinition{
        "list_change_sets",
        "List the change sets currently open against this project, including any built by other "
        "connected clients.",
        nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}},
        true,
        &ListChangeSets,
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
