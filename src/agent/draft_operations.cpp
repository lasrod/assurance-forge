#include "agent/operations.h"

#include "core/problems/gsn_wellformedness.h"
#include "core/reviews/review_proposal.h"
#include "core/sccg/staged_checks.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace agent {
namespace {

constexpr const char* kNotAcceptedNote =
    "This is unaccepted work in the integrated draft. Only a human can promote it in Assurance Forge.";

std::string StringArgument(const nlohmann::json& arguments, const char* key) {
    const nlohmann::json::const_iterator found = arguments.find(key);
    if (found == arguments.end() || !found->is_string())
        return {};
    return found->get<std::string>();
}

Result DraftError(std::string message, std::uint64_t current_revision) {
    return Result{nlohmann::json{{"error", std::move(message)}, {"current_working_revision", current_revision}}, true};
}

// The revision says the draft moved; the generation says the ground under the
// whole session moved -- a project switch, a fresh grant, a revocation. A
// mutation computed before such a change must not land after it (ADR 0014),
// even when the workspace revision happens to match again.
bool CheckExpectedContextGeneration(const DraftContext& context, const nlohmann::json& arguments, Result& refusal) {
    if (context.context_generation == 0) {
        // No generation to check against -- a caller outside the connected
        // bridge, e.g. tests driving the handler directly. The revision check
        // still stands.
        return true;
    }
    const nlohmann::json::const_iterator supplied = arguments.find("expected_context_generation");
    if (supplied == arguments.end() || !supplied->is_number_integer() || supplied->get<std::int64_t>() < 0) {
        refusal = DraftError("Argument \"expected_context_generation\" is required. Read get_draft_status or any "
                             "case-content result, then retry with the context_generation it reports.",
                             context.store.revision());
        return false;
    }
    if (supplied->get<std::uint64_t>() != context.context_generation) {
        refusal = DraftError("The session's context changed after you read it -- the project was switched, or "
                             "access was re-granted. Re-read the working draft before deciding whether this "
                             "operation still applies.",
                             context.store.revision());
        return false;
    }
    return true;
}

// One gate for both mutation preconditions, so a new handler cannot take the
// revision check without the generation check.
bool CheckExpectedRevision(const DraftContext& context, const nlohmann::json& arguments, Result& refusal) {
    const nlohmann::json::const_iterator supplied = arguments.find("expected_working_revision");
    if (supplied == arguments.end() || !supplied->is_number_integer() || supplied->get<std::int64_t>() < 0) {
        refusal = DraftError("Argument \"expected_working_revision\" is required. Read get_draft_status or any "
                             "case-content result, then retry with the revision it reports.",
                             context.store.revision());
        return false;
    }

    const std::uint64_t expected = supplied->get<std::uint64_t>();
    const std::uint64_t current = context.store.revision();
    if (expected != current) {
        refusal = DraftError("The integrated draft changed after you read it. Re-read the working draft and decide "
                             "whether your operation still means the same thing before retrying.",
                             current);
        return false;
    }
    return CheckExpectedContextGeneration(context, arguments, refusal);
}

std::string ArgumentFile(const DraftContext& context) {
    if (context.state.current_project.has_value()) {
        std::error_code error;
        const std::filesystem::path relative =
            std::filesystem::relative(context.state.loaded_file_path, context.state.current_project->rootPath, error);
        if (!error && !relative.empty())
            return relative.generic_string();
    }
    return context.state.loaded_file_path.generic_string();
}

std::string SessionId(const DraftContext& context) {
    if (!context.session_id.empty())
        return context.session_id;
    return std::to_string(context.connection_id);
}

const core::drafts::DraftChangeGroup* FindOpenGroupForSession(const DraftContext& context) {
    const core::drafts::DraftWorkspace* workspace = context.store.workspace();
    if (workspace == nullptr)
        return nullptr;

    const core::drafts::DraftChangeGroup* newest = nullptr;
    for (const core::drafts::DraftChangeGroup& group : workspace->groups) {
        if (group.source != core::drafts::DraftSource::Mcp || group.source_session_id != SessionId(context) ||
            !group.open()) {
            continue;
        }
        if (newest == nullptr || group.sequence > newest->sequence)
            newest = &group;
    }
    return newest;
}

std::string GroupIdArgument(const DraftContext& context, const nlohmann::json& arguments) {
    std::string id = StringArgument(arguments, "group_id");
    if (id.empty())
        id = StringArgument(arguments, "change_set_id");
    if (!id.empty())
        return id;
    const core::drafts::DraftChangeGroup* open = FindOpenGroupForSession(context);
    return open != nullptr ? open->id : std::string{};
}

const core::drafts::DraftChangeGroup*
FindOwnedGroup(const DraftContext& context, const std::string& group_id, Result& refusal) {
    const core::drafts::DraftWorkspace* workspace = context.store.workspace();
    const core::drafts::DraftChangeGroup* group = workspace != nullptr ? workspace->FindGroup(group_id) : nullptr;
    if (group == nullptr) {
        refusal = DraftError("No draft change group has the id \"" + group_id + "\".", context.store.revision());
        return nullptr;
    }
    if (group->source != core::drafts::DraftSource::Mcp || group->source_session_id != SessionId(context)) {
        refusal = DraftError("That draft group belongs to another contributor. You may inspect the combined "
                             "working draft, but you may modify only groups created by this MCP session.",
                             context.store.revision());
        return nullptr;
    }
    return group;
}

void AddElementRef(nlohmann::json& object,
                   const char* field,
                   const std::optional<core::reviews::ElementRef>& reference) {
    if (!reference.has_value())
        return;
    if (reference->existing_id.has_value())
        object[field] = nlohmann::json{{"id", reference->existing_id.value()}};
    else if (reference->create_ref.has_value())
        object[field] = nlohmann::json{{"ref", reference->create_ref.value()}};
}

nlohmann::json OperationsJson(const core::drafts::DraftChangeGroup& group) {
    nlohmann::json operations = nlohmann::json::array();
    for (const core::reviews::PatchOperation& operation : group.operations) {
        nlohmann::json object{{"type", core::reviews::PatchOperationTypeToString(operation.type)}};
        if (operation.create_ref.has_value())
            object["create_ref"] = operation.create_ref.value();
        AddElementRef(object, "element", operation.element);
        AddElementRef(object, "source", operation.source);
        AddElementRef(object, "target", operation.target);
        if (!operation.field.empty())
            object["field"] = operation.field;
        if (!operation.old_value.empty())
            object["old_value"] = operation.old_value;
        if (!operation.new_value.empty())
            object["new_value"] = operation.new_value;
        if (!operation.text.empty())
            object["text"] = operation.text;
        if (!operation.translations.empty())
            object["translations"] = operation.translations;
        operations.push_back(std::move(object));
    }
    return operations;
}

nlohmann::json GroupJson(const core::drafts::DraftChangeGroup& group, bool include_operations = false) {
    nlohmann::json payload{{"group_id", group.id},
                           {"change_set_id", group.id},
                           {"title", group.title},
                           {"summary", group.summary},
                           {"rationale", group.rationale},
                           {"source", core::drafts::DraftSourceToString(group.source)},
                           {"source_label", group.source_label},
                           {"state", core::drafts::DraftGroupStateToString(group.state)},
                           {"operation_count", static_cast<int>(group.operations.size())},
                           {"created_element_ids", group.generated_ids},
                           {"depends_on_group_ids", group.depends_on_group_ids},
                           {"guideline_ids", group.guideline_ids},
                           {"review_item_ids", group.review_item_ids}};
    // Only when something was acknowledged: an absent key reads as the normal
    // case, and the normal case is a clean submission.
    if (!group.acknowledged_findings.empty())
        payload["acknowledged_findings"] = group.acknowledged_findings;
    if (include_operations)
        payload["operations"] = OperationsJson(group);
    return payload;
}

void AddWorkspaceEnvelope(const DraftContext& context, nlohmann::json& payload) {
    const core::drafts::DraftWorkspace* workspace = context.store.workspace();
    payload["argument_file"] = ArgumentFile(context);
    payload["working_revision"] = context.store.revision();
    if (context.context_generation > 0)
        payload["context_generation"] = context.context_generation;
    payload["view"] = workspace != nullptr && workspace->has_active_groups() ? "working_draft" : "accepted";
    if (workspace != nullptr) {
        payload["workspace_id"] = workspace->id;
        payload["workspace_state"] = core::drafts::DraftWorkspaceStateToString(workspace->state);
    }
    payload["note"] = kNotAcceptedNote;
}

nlohmann::json FindingsJson(const core::drafts::DraftMaterializationResult& materialized) {
    nlohmann::json findings = nlohmann::json::array();
    for (const core::sccg::StagedFinding& finding : materialized.sccg_findings) {
        findings.push_back(nlohmann::json{{"kind", "sccg"},
                                          {"guideline_id", finding.guideline_id},
                                          // The catalog's stable name for the rule, so an agent can
                                          // deduplicate findings across staging calls instead of
                                          // re-reading the sentence each time.
                                          {"check_id", finding.check_id},
                                          {"element_id", finding.element_id},
                                          {"message", finding.detail},
                                          {"severity", core::sccg::FindingSeverityToString(finding.severity)}});
    }
    for (const core::GsnFinding& finding : materialized.gsn_findings) {
        findings.push_back(nlohmann::json{{"kind", "gsn"},
                                          {"requirement_id", core::GsnRequirementId(finding.rule)},
                                          {"rule", core::GsnRuleName(finding.rule)},
                                          {"element_id", finding.element_id},
                                          {"relationship_id", finding.relationship_id},
                                          {"detail", finding.detail},
                                          {"severity", "problem"}});
    }
    for (const core::ArgumentCycle& cycle : materialized.cycles) {
        findings.push_back(
            nlohmann::json{{"kind", "support_cycle"}, {"element_ids", cycle.element_ids}, {"severity", "problem"}});
    }
    return findings;
}

Result DescribeGroupResult(const DraftContext& context, const core::drafts::DraftChangeGroup& group) {
    nlohmann::json payload = GroupJson(group, true);
    AddWorkspaceEnvelope(context, payload);
    if (context.state.loaded_case.has_value()) {
        const core::drafts::DraftMaterializationResult& materialized =
            context.store.Materialize(context.state.loaded_case.value(), context.state.case_revision);
        payload["materializes"] = materialized.success;
        if (materialized.success)
            payload["findings"] = FindingsJson(materialized);
        else
            payload["problem"] = materialized.error;
    }
    return Result::Ok(std::move(payload));
}

Result MissingGroup(const DraftContext& context) {
    return DraftError("Argument \"group_id\" is required. Begin a change group first, or provide the id of one "
                      "created by this MCP session.",
                      context.store.revision());
}

} // namespace

Result GetDraftStatus(const DraftContext& context) {
    nlohmann::json payload;
    const core::drafts::DraftWorkspace* workspace = context.store.workspace();
    nlohmann::json groups = nlohmann::json::array();
    if (workspace != nullptr) {
        for (const core::drafts::DraftChangeGroup* group : workspace->ActiveGroups())
            groups.push_back(GroupJson(*group));
    }
    payload["groups"] = groups;
    // One migration release keeps the previous response key as well as the new
    // vocabulary. This lets existing clients move from list_change_sets to
    // get_draft_status without treating every persisted group as missing.
    payload["change_sets"] = std::move(groups);
    AddWorkspaceEnvelope(context, payload);
    return Result::Ok(std::move(payload));
}

Result BeginChangeGroup(const DraftContext& context, const nlohmann::json& arguments) {
    if (!context.state.loaded_case.has_value())
        return Result::Error("No assurance case is loaded, so there is nothing to draft against.");
    Result refusal;
    if (!CheckExpectedRevision(context, arguments, refusal))
        return refusal;

    const std::string title = StringArgument(arguments, "title");
    if (title.empty())
        return DraftError("Argument \"title\" is required. It is the change-group title shown to the reviewer.",
                          context.store.revision());

    core::drafts::DraftGroupRequest request;
    request.title = title;
    request.summary = StringArgument(arguments, "summary");
    request.rationale = StringArgument(arguments, "rationale");
    if (request.rationale.empty())
        request.rationale = StringArgument(arguments, "intent");
    request.source = core::drafts::DraftSource::Mcp;
    request.source_label = context.client_label.empty() ? "MCP client" : context.client_label;
    request.source_session_id = SessionId(context);

    std::string error;
    const std::string group_id = context.store.BeginGroup(request, context.state.loaded_case.value(), error);
    if (group_id.empty())
        return DraftError(error.empty() ? "The draft change group could not be created." : error,
                          context.store.revision());
    return DescribeChangeGroup(context, nlohmann::json{{"group_id", group_id}});
}

Result StageDraftOperations(const DraftContext& context, const nlohmann::json& arguments) {
    if (!context.state.loaded_case.has_value())
        return Result::Error("No assurance case is loaded, so there is nothing to draft against.");
    Result refusal;
    if (!CheckExpectedRevision(context, arguments, refusal))
        return refusal;

    const std::string group_id = GroupIdArgument(context, arguments);
    if (group_id.empty())
        return MissingGroup(context);
    if (FindOwnedGroup(context, group_id, refusal) == nullptr)
        return refusal;

    std::vector<core::reviews::PatchOperation> operations;
    std::string error;
    const nlohmann::json::const_iterator supplied = arguments.find("operations");
    if (supplied == arguments.end() || !ParsePatchOperations(*supplied, operations, error))
        return DraftError(error.empty() ? "\"operations\" must be a non-empty array." : error,
                          context.store.revision());
    if (!context.store.StageOperations(group_id, operations, context.state.loaded_case.value(), error))
        return DraftError("These operations would not materialize in the integrated draft: " + error,
                          context.store.revision());

    const core::drafts::DraftMaterializationResult& materialized =
        context.store.Materialize(context.state.loaded_case.value(), context.state.case_revision);
    if (!materialized.success)
        return DraftError(materialized.error, context.store.revision());

    const core::drafts::DraftChangeGroup* group = context.store.workspace()->FindGroup(group_id);
    nlohmann::json payload = GroupJson(*group, true);
    payload["staged"] = static_cast<int>(operations.size());
    payload["findings"] = FindingsJson(materialized);
    AddWorkspaceEnvelope(context, payload);
    return Result::Ok(std::move(payload));
}

Result CheckDraftOperations(const DraftContext& context, const nlohmann::json& arguments) {
    if (!context.state.loaded_case.has_value())
        return Result::Error("No assurance case is loaded, so there is nothing to check against.");

    std::vector<core::reviews::PatchOperation> operations;
    std::string error;
    const nlohmann::json::const_iterator supplied = arguments.find("operations");
    if (supplied == arguments.end() || !ParsePatchOperations(*supplied, operations, error))
        return DraftError(error.empty() ? "\"operations\" must be a non-empty array." : error,
                          context.store.revision());

    // The whole point is that nothing below touches the store: the rehearsal
    // runs on a copy of the workspace, so no revision moves, nothing is saved,
    // and nothing flickers onto the user's canvas while the agent iterates.
    core::drafts::DraftWorkspace rehearsal;
    if (context.store.workspace() != nullptr)
        rehearsal = *context.store.workspace();

    // An explicit group_id -- or the session's open group, exactly the default
    // stage_operations would use -- rehearses "appended to that group".
    // Without one, the operations rehearse as a group of their own.
    const std::string group_id = GroupIdArgument(context, arguments);
    if (!group_id.empty()) {
        Result refusal;
        if (FindOwnedGroup(context, group_id, refusal) == nullptr)
            return refusal;
        core::drafts::DraftChangeGroup* target = rehearsal.FindGroup(group_id);
        target->operations.insert(target->operations.end(), operations.begin(), operations.end());
    } else {
        core::drafts::DraftChangeGroup hypothetical;
        hypothetical.id = "$rehearsal";
        while (rehearsal.FindGroup(hypothetical.id) != nullptr)
            hypothetical.id += "$";
        hypothetical.sequence = rehearsal.next_sequence++;
        hypothetical.title = "Rehearsal";
        hypothetical.source = core::drafts::DraftSource::Mcp;
        hypothetical.source_session_id = SessionId(context);
        hypothetical.operations = operations;
        rehearsal.groups.push_back(std::move(hypothetical));
    }

    core::drafts::DraftMaterializationResult materialized = core::drafts::MaterializeDraft(
        rehearsal, context.state.loaded_case.value(), context.store.authoritative_identities());

    nlohmann::json payload;
    payload["materializes"] = materialized.success;
    payload["checked_operation_count"] = static_cast<int>(operations.size());
    if (materialized.success)
        payload["findings"] = FindingsJson(materialized);
    else
        payload["problem"] = materialized.error;
    // Ids allocated during the rehearsal die with it, and returning them would
    // invite an agent to refer to an element that will get a different id when
    // the operations are really staged.
    payload["rehearsal"] = "Nothing was staged and no element ids were allocated. When the operations are "
                           "clean, stage them with stage_operations.";
    AddWorkspaceEnvelope(context, payload);
    return Result::Ok(std::move(payload));
}

Result ReplaceChangeGroup(const DraftContext& context, const nlohmann::json& arguments) {
    if (!context.state.loaded_case.has_value())
        return Result::Error("No assurance case is loaded, so there is nothing to draft against.");
    Result refusal;
    if (!CheckExpectedRevision(context, arguments, refusal))
        return refusal;

    const std::string group_id = GroupIdArgument(context, arguments);
    if (group_id.empty())
        return MissingGroup(context);
    if (FindOwnedGroup(context, group_id, refusal) == nullptr)
        return refusal;

    std::vector<core::reviews::PatchOperation> operations;
    std::string error;
    const nlohmann::json::const_iterator supplied = arguments.find("operations");
    if (supplied == arguments.end() || !ParsePatchOperations(*supplied, operations, error))
        return DraftError(error.empty() ? "\"operations\" must be a non-empty array." : error,
                          context.store.revision());
    if (!context.store.ReplaceOperations(group_id, operations, context.state.loaded_case.value(), error))
        return DraftError("These replacement operations would not materialize in the integrated draft: " + error,
                          context.store.revision());
    return DescribeChangeGroup(context, nlohmann::json{{"group_id", group_id}});
}

Result RemoveChangeGroup(const DraftContext& context, const nlohmann::json& arguments) {
    Result refusal;
    if (!CheckExpectedRevision(context, arguments, refusal))
        return refusal;
    const std::string group_id = GroupIdArgument(context, arguments);
    if (group_id.empty())
        return MissingGroup(context);
    if (FindOwnedGroup(context, group_id, refusal) == nullptr)
        return refusal;

    std::string error;
    if (!context.store.RejectGroup(group_id, error))
        return DraftError(error, context.store.revision());
    nlohmann::json payload{{"group_id", group_id}, {"change_set_id", group_id}, {"state", "rejected"}};
    AddWorkspaceEnvelope(context, payload);
    return Result::Ok(std::move(payload));
}

Result DescribeChangeGroup(const DraftContext& context, const nlohmann::json& arguments) {
    const std::string group_id = GroupIdArgument(context, arguments);
    if (group_id.empty())
        return MissingGroup(context);
    const core::drafts::DraftWorkspace* workspace = context.store.workspace();
    const core::drafts::DraftChangeGroup* group = workspace != nullptr ? workspace->FindGroup(group_id) : nullptr;
    if (group == nullptr)
        return DraftError("No draft change group has the id \"" + group_id + "\".", context.store.revision());
    return DescribeGroupResult(context, *group);
}

// The Problem-severity findings standing against one group in the current
// working draft, each named the way the group's author saw it in the staging
// results. Advisory findings are deliberately absent: they are the reviewer's
// judgement call, and gating on them would train agents to acknowledge
// reflexively -- which would spend the one gate this surface has.
std::vector<std::string> StandingProblemFindings(const DraftContext& context, const std::string& group_id) {
    std::vector<std::string> problems;
    if (!context.state.loaded_case.has_value())
        return problems;
    const core::drafts::DraftMaterializationResult& materialized =
        context.store.Materialize(context.state.loaded_case.value(), context.state.case_revision);
    if (!materialized.success)
        return problems;

    const auto attributed_to_group = [&](const std::string& element_id) {
        const std::vector<std::string> contributors = materialized.change_index.ContributingGroupIds(element_id);
        return std::find(contributors.begin(), contributors.end(), group_id) != contributors.end();
    };

    for (const core::sccg::StagedFinding& finding : materialized.sccg_findings) {
        if (finding.severity != core::sccg::FindingSeverity::Problem || !attributed_to_group(finding.element_id))
            continue;
        problems.push_back(finding.guideline_id + " " + finding.element_id + ": " + finding.detail);
    }
    // GSN well-formedness findings are always problems -- that is what
    // well-formedness means -- so they gate alongside the SCCG problems.
    for (const core::GsnFinding& finding : materialized.gsn_findings) {
        if (finding.element_id.empty() || !attributed_to_group(finding.element_id))
            continue;
        problems.push_back(std::string(core::GsnRequirementId(finding.rule)) + " " + finding.element_id + ": " +
                           finding.detail);
    }
    return problems;
}

Result SubmitChangeGroup(const DraftContext& context, const nlohmann::json& arguments) {
    Result refusal;
    if (!CheckExpectedRevision(context, arguments, refusal))
        return refusal;
    const std::string group_id = GroupIdArgument(context, arguments);
    if (group_id.empty())
        return MissingGroup(context);
    if (FindOwnedGroup(context, group_id, refusal) == nullptr)
        return refusal;

    // The gate sits here, not at staging: staging is deliberately incremental
    // and every intermediate shape is legitimately unfinished, but submit is
    // the author declaring itself done -- the one moment "a reviewer will
    // certainly reject this shape" is worth refusing over.
    const std::vector<std::string> standing_problems = StandingProblemFindings(context, group_id);
    const bool acknowledged = arguments.contains("acknowledge_findings") &&
                              arguments["acknowledge_findings"].is_boolean() &&
                              arguments["acknowledge_findings"].get<bool>();
    if (!standing_problems.empty() && !acknowledged) {
        nlohmann::json problems = nlohmann::json::array();
        for (const std::string& problem : standing_problems)
            problems.push_back(problem);
        return Result{nlohmann::json{{"error",
                                      "This group still has " + std::to_string(standing_problems.size()) +
                                          " problem-severity finding" + (standing_problems.size() == 1 ? "" : "s") +
                                          " a reviewer will certainly reject, listed in problem_findings. Fix them "
                                          "and submit again -- or repeat this call with \"acknowledge_findings\": "
                                          "true to hand the group over anyway, which records the acknowledgment on "
                                          "the group for the reviewer to see."},
                                     {"problem_findings", std::move(problems)},
                                     {"current_working_revision", context.store.revision()}},
                      true};
    }

    std::string error;
    if (!context.store.MarkGroupReady(group_id, error, acknowledged ? standing_problems : std::vector<std::string>{}))
        return DraftError(error, context.store.revision());
    return DescribeChangeGroup(context, nlohmann::json{{"group_id", group_id}});
}

Result DescribeWorkingDraft(const DraftContext& context) {
    if (!context.state.loaded_case.has_value())
        return Result::Error("No assurance case is loaded.");

    nlohmann::json payload;
    const core::drafts::DraftMaterializationResult& materialized =
        context.store.Materialize(context.state.loaded_case.value(), context.state.case_revision);
    payload["materializes"] = materialized.success;
    payload["element_count"] = static_cast<int>(materialized.working_model.elements.size());
    payload["findings"] = FindingsJson(materialized);
    if (!materialized.success) {
        payload["problem"] = materialized.error;
        payload["failing_group_id"] = materialized.failing_group_id;
    }
    AddWorkspaceEnvelope(context, payload);
    return Result::Ok(std::move(payload));
}

Result GetDraftEvents(const DraftContext& context, const nlohmann::json& arguments) {
    std::uint64_t after_revision = 0;
    const nlohmann::json::const_iterator supplied = arguments.find("after_revision");
    if (supplied != arguments.end() && supplied->is_number_integer() && supplied->get<std::int64_t>() >= 0)
        after_revision = supplied->get<std::uint64_t>();

    nlohmann::json events = nlohmann::json::array();
    const core::drafts::DraftWorkspace* workspace = context.store.workspace();
    if (workspace != nullptr) {
        for (const core::drafts::DraftEvent& event : workspace->events) {
            if (event.revision <= after_revision)
                continue;
            events.push_back(nlohmann::json{{"revision", event.revision},
                                            {"type", event.type},
                                            {"group_id", event.group_id},
                                            {"detail", event.detail},
                                            {"created_utc", event.created_utc}});
        }
    }
    nlohmann::json payload{{"events", std::move(events)}};
    AddWorkspaceEnvelope(context, payload);
    return Result::Ok(std::move(payload));
}

Result CloseChangeGroup(const DraftContext& context, const nlohmann::json& arguments) {
    return RemoveChangeGroup(context, arguments);
}

Result UnstageDraftOperations(const DraftContext& context, const nlohmann::json& arguments) {
    if (!context.state.loaded_case.has_value())
        return Result::Error("No assurance case is loaded, so there is nothing to draft against.");
    Result refusal;
    if (!CheckExpectedRevision(context, arguments, refusal))
        return refusal;
    const std::string group_id = GroupIdArgument(context, arguments);
    if (group_id.empty())
        return MissingGroup(context);
    const core::drafts::DraftChangeGroup* group = FindOwnedGroup(context, group_id, refusal);
    if (group == nullptr)
        return refusal;

    const nlohmann::json::const_iterator supplied = arguments.find("count");
    const std::size_t count = supplied != arguments.end() && supplied->is_number_integer() && supplied->get<int>() > 0
                                  ? static_cast<std::size_t>(supplied->get<int>())
                                  : 1;
    if (count > group->operations.size())
        return DraftError("Cannot remove more operations than the group contains.", context.store.revision());

    std::vector<core::reviews::PatchOperation> remaining = group->operations;
    remaining.resize(remaining.size() - count);
    std::string error;
    if (!context.store.ReplaceOperations(group_id, remaining, context.state.loaded_case.value(), error))
        return DraftError(error, context.store.revision());
    return DescribeChangeGroup(context, nlohmann::json{{"group_id", group_id}});
}

} // namespace agent
