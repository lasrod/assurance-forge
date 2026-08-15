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

ToolResult GetConnectionStatus(Session& session, const nlohmann::json& arguments) {
    return Run(session, "get_connection_status", arguments);
}

ToolResult RequestProjectAccess(Session& session, const nlohmann::json& arguments) {
    return Run(session, "request_project_access", arguments);
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

ToolResult ListAssuranceClaimPoints(Session& session, const nlohmann::json& arguments) {
    return Run(session, "list_assurance_claim_points", arguments);
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

ToolResult GetDraftStatus(Session& session, const nlohmann::json& arguments) {
    return Run(session, "get_draft_status", arguments);
}

ToolResult BeginChangeGroup(Session& session, const nlohmann::json& arguments) {
    return Run(session, "begin_change_group", arguments);
}

ToolResult ReplaceChangeGroup(Session& session, const nlohmann::json& arguments) {
    return Run(session, "replace_change_group", arguments);
}

ToolResult RemoveChangeGroup(Session& session, const nlohmann::json& arguments) {
    return Run(session, "remove_change_group", arguments);
}

ToolResult DescribeChangeGroup(Session& session, const nlohmann::json& arguments) {
    return Run(session, "describe_change_group", arguments);
}

ToolResult SubmitChangeGroup(Session& session, const nlohmann::json& arguments) {
    return Run(session, "submit_change_group", arguments);
}

ToolResult DescribeWorkingDraft(Session& session, const nlohmann::json& arguments) {
    return Run(session, "describe_working_draft", arguments);
}

ToolResult GetDraftEvents(Session& session, const nlohmann::json& arguments) {
    return Run(session, "get_draft_events", arguments);
}

ToolResult CloseChangeGroup(Session& session, const nlohmann::json& arguments) {
    return Run(session, "close_change_group", arguments);
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
         "\"target\":{\"id\":\"G1\"}} puts the new element UNDER G1. When the case is maintained in "
         "more than one language, state each new element in all of them in the one operation that "
         "creates it -- see \"translations\"."},
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
            {"text", {{"type", "string"}, {"description", "Initial text for a Create* operation."}}},
            // A safety case read by reviewers in two languages has to be
            // written in both, and one operation carrying both is what makes
            // that atomic: the reviewer accepts a bilingual claim or none of
            // it, and no group can be promoted half-translated.
            {"translations",
             {{"type", "object"},
              {"additionalProperties", {{"type", "string"}}},
              {"description",
               "Secondary-language text for this operation's \"text\" or \"new_value\", keyed by "
               "language code, e.g. {\"ja\": \"...\"}. The English goes in \"text\"/\"new_value\" and "
               "must not be repeated here. On a Create* operation \"text\" is required alongside it. "
               "An UpdateElementText carrying only \"translations\" revises just those languages and "
               "leaves the English untouched -- use that to translate argument that already exists. "
               "Translations you author arrive flagged for human translation review."}}}}}}}};
}

nlohmann::json DraftGroupIdSchema() {
    return nlohmann::json{
        {"group_id",
         {{"type", "string"}, {"description", "Defaults to the newest open group created by this MCP session."}}},
        {"change_set_id",
         {{"type", "string"}, {"description", "Compatibility alias for group_id during the migration release."}}},
    };
}

nlohmann::json ExpectedRevisionSchema() {
    return nlohmann::json{
        {"type", "integer"},
        {"minimum", 0},
        {"description", "The working_revision returned by the draft or case-content read this change is based on."}};
}

std::vector<ToolDefinition> BuildTools() {
    std::vector<ToolDefinition> tools;

    tools.push_back(ToolDefinition{
        "get_connection_status",
        "Report whether this session is talking to a running Assurance Forge (live working "
        "draft, draft tools available) or serving read-only from a copy on disk, and whether "
        "MCP sharing consent is enabled. Returns no case content. Call it when another tool "
        "reports a connection or consent problem.",
        nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}},
        // Mode, application version and the consent flag are about this
        // process, not the argument, so the consent gate does not apply --
        // which is the point: it must work while consent is still off, so the
        // model can tell the user which switch to flip.
        false,
        &GetConnectionStatus,
    });

    tools.push_back(ToolDefinition{
        "request_project_access",
        "Ask the user, inside the running Assurance Forge, to grant this session access to the "
        "project they have open (ADR 0014). Returns the request's state -- pending, granted, or "
        "denied -- and never any case content. Access is per session and lasts while the project "
        "stays open; a denial is also an answer. Call this when another tool reports "
        "project_access_pending, then retry after the user has had a moment to respond.",
        nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}},
        // The reply carries no case content, but the flag is what enforces the
        // master gate (ADR 0007), and raising an approval prompt inside the
        // application is exactly what a closed gate must prevent: with MCP
        // disabled, no request reaches the user at all.
        true,
        &RequestProjectAccess,
    });

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
        "relationships, its GSN role and immediate neighbours, and any assurance claim "
        "points on it or on the relationships that touch it.",
        nlohmann::json{{"type", "object"},
                       {"properties", {{"id", {{"type", "string"}, {"description", "Element id."}}}}},
                       {"required", nlohmann::json::array({"id"})}},
        true,
        &GetElement,
    });

    tools.push_back(ToolDefinition{
        "list_assurance_claim_points",
        "List the case's assurance claim points (GSN ACPs): what each one annotates -- an "
        "element such as a Solution, or a SupportedBy/InContextOf relationship -- and how it "
        "is resolved: inline text, or a link to a confidence argument (claim, package and top "
        "goal ids you can follow with get_element). An ACP marks where the argument's own "
        "assurance is questioned, so read them before proposing changes near one. Read-only: "
        "ACPs are created and edited in Assurance Forge, not over MCP.",
        nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}},
        true,
        &ListAssuranceClaimPoints,
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
    // Integrated draft workspace
    // ---------------------------------------------------------------------

    tools.push_back(ToolDefinition{
        "get_draft_status",
        "Report the integrated working draft, its revision, and every active change group. Read this before "
        "modifying the draft; every modifying call must carry the returned working_revision.",
        nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}},
        true,
        &GetDraftStatus,
    });

    tools.push_back(ToolDefinition{
        "begin_change_group",
        "Start one coherent contribution to the shared working draft. Human edits, SCCG review, and other MCP "
        "groups may already be present. Nothing is accepted until a human promotes it in Assurance Forge.",
        nlohmann::json{
            {"type", "object"},
            {"properties",
             {{"title", {{"type", "string"}, {"description", "Short title shown to the reviewer."}}},
              {"summary", {{"type", "string"}, {"description", "What this group changes."}}},
              {"rationale", {{"type", "string"}, {"description", "Why this safety-argument change is needed."}}},
              {"expected_working_revision", ExpectedRevisionSchema()}}},
            {"required", nlohmann::json::array({"title", "expected_working_revision"})}},
        true,
        &BeginChangeGroup,
    });

    tools.push_back(ToolDefinition{
        "replace_change_group",
        "Replace every operation in one of this MCP session's groups. Use this when feedback changes the shape of "
        "the proposal; the replacement is rehearsed against the complete integrated draft before it is stored.",
        nlohmann::json{{"type", "object"},
                       {"properties",
                        [&] {
                            nlohmann::json properties = DraftGroupIdSchema();
                            properties["operations"] = OperationsSchema();
                            properties["expected_working_revision"] = ExpectedRevisionSchema();
                            return properties;
                        }()},
                       {"required", nlohmann::json::array({"operations", "expected_working_revision"})}},
        true,
        &ReplaceChangeGroup,
    });

    tools.push_back(ToolDefinition{
        "remove_change_group",
        "Reject one of this MCP session's groups and remove its contribution from the working model. The record is "
        "retained for provenance; accepted SACM is untouched.",
        nlohmann::json{{"type", "object"},
                       {"properties",
                        [&] {
                            nlohmann::json properties = DraftGroupIdSchema();
                            properties["expected_working_revision"] = ExpectedRevisionSchema();
                            return properties;
                        }()},
                       {"required", nlohmann::json::array({"expected_working_revision"})}},
        true,
        &RemoveChangeGroup,
    });

    tools.push_back(ToolDefinition{
        "describe_change_group",
        "Describe one change group's provenance, operations, generated element ids, dependencies, and findings in "
        "the combined working draft.",
        nlohmann::json{{"type", "object"}, {"properties", DraftGroupIdSchema()}},
        true,
        &DescribeChangeGroup,
    });

    tools.push_back(ToolDefinition{
        "submit_change_group",
        "Mark one of this MCP session's groups ready for human review. This does not accept or apply it; only the "
        "user can promote draft work in Assurance Forge.",
        nlohmann::json{{"type", "object"},
                       {"properties",
                        [&] {
                            nlohmann::json properties = DraftGroupIdSchema();
                            properties["expected_working_revision"] = ExpectedRevisionSchema();
                            return properties;
                        }()},
                       {"required", nlohmann::json::array({"expected_working_revision"})}},
        true,
        &SubmitChangeGroup,
    });

    tools.push_back(ToolDefinition{
        "describe_working_draft",
        "Summarize whether the complete integrated draft materializes and report its combined SCCG and structural "
        "findings. Use the ordinary read tools to inspect its elements and tree.",
        nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}},
        true,
        &DescribeWorkingDraft,
    });

    tools.push_back(ToolDefinition{
        "get_draft_events",
        "Return draft events after a revision, so a client can learn what humans, SCCG review, or other MCP clients "
        "changed without guessing from stale local state.",
        nlohmann::json{{"type", "object"},
                       {"properties",
                        {{"after_revision",
                          {{"type", "integer"},
                           {"minimum", 0},
                           {"description", "Return events newer than this revision (default 0)."}}}}}},
        true,
        &GetDraftEvents,
    });

    tools.push_back(ToolDefinition{
        "close_change_group",
        "Abandon one of this MCP session's groups when the conversation ends without submitting it. This removes "
        "its contribution from the working model and retains the rejected record.",
        nlohmann::json{{"type", "object"},
                       {"properties",
                        [&] {
                            nlohmann::json properties = DraftGroupIdSchema();
                            properties["expected_working_revision"] = ExpectedRevisionSchema();
                            return properties;
                        }()},
                       {"required", nlohmann::json::array({"expected_working_revision"})}},
        true,
        &CloseChangeGroup,
    });

    // ---------------------------------------------------------------------
    // Change-set compatibility aliases
    //
    // The descriptions carry one message repeatedly and deliberately: staging
    // does not change the safety case. An agent that believes it has edited the
    // argument will say so, and the user will believe it.
    // ---------------------------------------------------------------------

    tools.push_back(ToolDefinition{
        "begin_change_set",
        "Compatibility alias for begin_change_group. Starts a group in the shared integrated draft and returns both "
        "group_id and change_set_id. Migrate to begin_change_group.",
        nlohmann::json{
            {"type", "object"},
            {"properties",
             {{"title", {{"type", "string"}, {"description", "Short title the user sees while you build this."}}},
              {"summary", {{"type", "string"}, {"description", "What the change does."}}},
              {"intent",
               {{"type", "string"},
                {"description",
                 "Why you are making it. The reviewer is being asked to accept a "
                 "change to a safety argument and needs the reasoning."}}},
              {"expected_working_revision", ExpectedRevisionSchema()}}},
            {"required", nlohmann::json::array({"title", "expected_working_revision"})}},
        true,
        &BeginChangeSet,
    });

    tools.push_back(ToolDefinition{
        "stage_operations",
        "Append operations to a draft change group. They are checked against the complete integrated working draft "
        "and refused together if they would not materialize. Returns stable ids for created elements. This does not "
        "change accepted SACM.",
        nlohmann::json{{"type", "object"},
                       {"properties",
                        [&] {
                            nlohmann::json properties = DraftGroupIdSchema();
                            properties["operations"] = OperationsSchema();
                            properties["expected_working_revision"] = ExpectedRevisionSchema();
                            return properties;
                        }()},
                       {"required", nlohmann::json::array({"operations", "expected_working_revision"})}},
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
                            nlohmann::json properties = DraftGroupIdSchema();
                            properties["count"] = nlohmann::json{
                                {"type", "integer"}, {"description", "How many to drop from the end (default 1)."}};
                            properties["expected_working_revision"] = ExpectedRevisionSchema();
                            return properties;
                        }()},
                       {"required", nlohmann::json::array({"expected_working_revision"})}},
        true,
        &UnstageOperations,
    });

    tools.push_back(ToolDefinition{
        "describe_change_set",
        "Report what a change set would do to the case: elements added, modified and removed, "
        "with their text. Also reports whether it still applies -- the user may have edited the "
        "argument while you were working.",
        nlohmann::json{{"type", "object"}, {"properties", DraftGroupIdSchema()}},
        true,
        &DescribeChangeSet,
    });

    tools.push_back(ToolDefinition{
        "submit_change_set",
        "Hand the change set to the user for a decision. After this, tell them it is waiting for "
        "review in Assurance Forge. Do not tell them the safety case has been changed: it has "
        "not, and only they can change it.",
        nlohmann::json{{"type", "object"},
                       {"properties",
                        [&] {
                            nlohmann::json properties = DraftGroupIdSchema();
                            properties["expected_working_revision"] = ExpectedRevisionSchema();
                            return properties;
                        }()},
                       {"required", nlohmann::json::array({"expected_working_revision"})}},
        true,
        &SubmitChangeSet,
    });

    tools.push_back(ToolDefinition{
        "discard_change_set",
        "Abandon a change set and remove it from the user's canvas.",
        nlohmann::json{{"type", "object"},
                       {"properties",
                        [&] {
                            nlohmann::json properties = DraftGroupIdSchema();
                            properties["expected_working_revision"] = ExpectedRevisionSchema();
                            return properties;
                        }()},
                       {"required", nlohmann::json::array({"expected_working_revision"})}},
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
