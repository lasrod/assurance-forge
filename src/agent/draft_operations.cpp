#include "agent/operations.h"

#include "core/drafts/draft_document_diff.h"
#include "core/drafts/draft_operation_apply.h"
#include "core/problems/argument_cycles.h"
#include "core/problems/gsn_wellformedness.h"
#include "core/reviews/review_proposal.h"
#include "core/sccg/staged_checks.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
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
                             context.working_revision());
        return false;
    }
    if (supplied->get<std::uint64_t>() != context.context_generation) {
        refusal = DraftError("The session's context changed after you read it -- the project was switched, or "
                             "access was re-granted. Re-read the working draft before deciding whether this "
                             "operation still applies.",
                             context.working_revision());
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
                             context.working_revision());
        return false;
    }

    const std::uint64_t expected = supplied->get<std::uint64_t>();
    const std::uint64_t current = context.working_revision();
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
        refusal = DraftError("No draft change group has the id \"" + group_id + "\".", context.working_revision());
        return nullptr;
    }
    if (group->source != core::drafts::DraftSource::Mcp || group->source_session_id != SessionId(context)) {
        refusal = DraftError("That draft group belongs to another contributor. You may inspect the combined "
                             "working draft, but you may modify only groups created by this MCP session.",
                             context.working_revision());
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

// The draft document as it now stands, and what it changes about the accepted
// argument (ADR 0016). Both are derived: the projection from the document, the
// difference from the two projections. Nothing is inferred from an operation
// log, so this cannot disagree with what the user is looking at.
struct DraftDocumentSnapshot {
    core::AssuranceCase projection;
    core::drafts::DraftDocumentDiff diff;
    bool available = false;
};

DraftDocumentSnapshot SnapshotDraftDocument(const DraftContext& context) {
    DraftDocumentSnapshot snapshot;
    if (!context.document_backed() || !context.state.loaded_case.has_value())
        return snapshot;
    // An argument nobody has drafted against has no draft document, and its
    // draft view is the accepted argument itself. `document_backed()` is true
    // before the first edit and again after an accept discards the draft, so
    // projecting the absent document here yielded an empty case -- and the
    // comparison below then reported every accepted element as one this draft
    // removes. An agent that opened a group after its previous group was
    // accepted was told its untouched case had been emptied.
    snapshot.projection =
        context.document->active() ? context.document->Projection() : context.state.loaded_case.value();
    snapshot.diff = core::drafts::DiffAcceptedAgainstDraft(context.state.loaded_case.value(), snapshot.projection);
    snapshot.available = true;
    return snapshot;
}

void AddWorkspaceEnvelope(const DraftContext& context, nlohmann::json& payload) {
    const core::drafts::DraftWorkspace* workspace = context.store.workspace();
    payload["argument_file"] = ArgumentFile(context);
    payload["working_revision"] = context.working_revision();
    if (context.context_generation > 0)
        payload["context_generation"] = context.context_generation;
    // Whether the argument text a client reads is accepted or unaccepted, taken
    // from the bytes the application is actually rendering rather than from the
    // existence of a change group. A document-backed draft that has changed
    // nothing is indistinguishable from no draft at all, and an empty group is
    // not an unaccepted change.
    payload["view"] = context.working_draft_active ? "working_draft" : "accepted";
    if (workspace != nullptr) {
        payload["workspace_id"] = workspace->id;
        payload["workspace_state"] = core::drafts::DraftWorkspaceStateToString(workspace->state);
    }
    payload["note"] = kNotAcceptedNote;
}

// One SCCG finding as an agent reads it.
//
// `statement` is the guideline's own wording and `guideline_uri` the resource
// carrying the rest of it. Without them an agent holds the tool's paraphrase of
// a rule with no way to reach the rule itself -- a position no reviewer is
// asked to accept, and an agent proposing changes to a safety argument should
// not be either.
nlohmann::json SccgFindingJson(const core::sccg::StagedFinding& finding) {
    return nlohmann::json{{"kind", "sccg"},
                          {"guideline_id", finding.guideline_id},
                          // The catalog's stable name for the rule, so an agent can
                          // deduplicate findings across staging calls instead of
                          // re-reading the sentence each time.
                          {"check_id", finding.check_id},
                          {"statement", finding.statement},
                          {"guideline_uri", "sccg://guideline/" + finding.guideline_id},
                          {"element_id", finding.element_id},
                          {"message", finding.detail},
                          {"severity", core::sccg::FindingSeverityToString(finding.severity)}};
}

// What the tool actually decided, so an empty `findings` array cannot be read
// as conformance.
//
// The mechanical set is a small fraction of SCCG, and always will be: most of
// the guidance is prose only a reader can judge -- whether a decomposition is
// complete, whether evidence is relevant, whether a claim is sufficiently
// justified. An agent that sees no findings and concludes its argument passes
// SCCG has drawn exactly the wrong conclusion, and nothing in the result told
// it otherwise.
nlohmann::json CheckedJson() {
    nlohmann::json checks = nlohmann::json::array();
    for (const std::string& check_id : core::sccg::ImplementedCheckIds()) {
        checks.push_back(check_id);
    }
    return nlohmann::json{
        {"mechanical_checks", checks},
        {"note",
         "These are the checks this tool can decide mechanically. They are a small part of SCCG; most of it "
         "is prose only a reader can judge. No findings means these checks found nothing -- it does not mean "
         "the argument conforms to SCCG. Read sccg://guidelines, and expect a human review."},
    };
}

nlohmann::json ChangesJson(const core::drafts::DraftDocumentDiff& diff) {
    nlohmann::json changes = nlohmann::json::array();
    for (const core::drafts::DraftDocumentChange& change : diff.changes) {
        nlohmann::json entry{{"element_id", change.element_id},
                             {"change", core::drafts::DraftElementChangeToString(change.change)}};
        if (!change.fields.empty())
            entry["fields"] = change.fields;
        changes.push_back(std::move(entry));
    }
    return changes;
}

bool TouchesAnyChangedElement(const std::vector<std::string>& ids, const std::unordered_set<std::string>& changed) {
    return std::any_of(
        ids.begin(), ids.end(), [&changed](const std::string& id) { return !id.empty() && changed.count(id) > 0; });
}

// The same findings the materializer produced, over the draft document instead.
// Scoped to what the draft touched for the same reason: an agent handed defects
// in parts of the argument it never wrote cannot tell which are its to fix.
nlohmann::json DocumentFindingsJson(const DraftDocumentSnapshot& snapshot) {
    nlohmann::json findings = nlohmann::json::array();
    std::vector<std::string> changed_ids;
    for (const core::drafts::DraftDocumentChange& change : snapshot.diff.changes) {
        if (change.change != core::drafts::DraftElementChange::Unchanged)
            changed_ids.push_back(change.element_id);
    }
    if (changed_ids.empty())
        return findings;
    const std::unordered_set<std::string> changed(changed_ids.begin(), changed_ids.end());

    for (const core::sccg::StagedFinding& finding : core::sccg::CheckStagedArgument(snapshot.projection, changed_ids)) {
        findings.push_back(SccgFindingJson(finding));
    }
    for (const core::GsnFinding& finding : core::CheckGsnWellFormedness(snapshot.projection)) {
        if (!TouchesAnyChangedElement({finding.element_id, finding.related_id, finding.relationship_id}, changed))
            continue;
        findings.push_back(nlohmann::json{{"kind", "gsn"},
                                          {"requirement_id", core::GsnRequirementId(finding.rule)},
                                          {"rule", core::GsnRuleName(finding.rule)},
                                          {"element_id", finding.element_id},
                                          {"relationship_id", finding.relationship_id},
                                          {"detail", finding.detail},
                                          {"severity", "problem"}});
    }
    for (const core::ArgumentCycle& cycle : core::FindSupportCycles(snapshot.projection)) {
        if (!TouchesAnyChangedElement(cycle.element_ids, changed))
            continue;
        findings.push_back(
            nlohmann::json{{"kind", "support_cycle"}, {"element_ids", cycle.element_ids}, {"severity", "problem"}});
    }
    return findings;
}

nlohmann::json FindingsJson(const core::drafts::DraftMaterializationResult& materialized) {
    nlohmann::json findings = nlohmann::json::array();
    for (const core::sccg::StagedFinding& finding : materialized.sccg_findings) {
        findings.push_back(SccgFindingJson(finding));
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
    if (context.document_backed()) {
        // No `materializes` key, and that absence is the point: a document-backed
        // draft cannot fail to materialize, because there is nothing to
        // materialize. What it changes is a comparison against the accepted
        // argument, which is derived from the draft and so cannot disagree with
        // what the user is looking at.
        const DraftDocumentSnapshot snapshot = SnapshotDraftDocument(context);
        payload["changes"] = ChangesJson(snapshot.diff);
        payload["added"] = snapshot.diff.added_count;
        payload["modified"] = snapshot.diff.modified_count;
        payload["removed"] = snapshot.diff.removed_count;
        payload["findings"] = DocumentFindingsJson(snapshot);
        return Result::Ok(std::move(payload));
    }
    if (context.state.loaded_case.has_value()) {
        const core::drafts::DraftMaterializationResult& materialized =
            context.store.Materialize(context.state.loaded_case.value(), context.state.case_revision);
        payload["materializes"] = materialized.success;
        if (materialized.success) {
            payload["findings"] = FindingsJson(materialized);
            payload["checked"] = CheckedJson();
        } else {
            payload["problem"] = materialized.error;
        }
    }
    return Result::Ok(std::move(payload));
}

Result MissingGroup(const DraftContext& context) {
    return DraftError("Argument \"group_id\" is required. Begin a change group first, or provide the id of one "
                      "created by this MCP session.",
                      context.working_revision());
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
                          context.working_revision());

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
                          context.working_revision());
    return DescribeChangeGroup(context, nlohmann::json{{"group_id", group_id}});
}

namespace {

// Which ArgumentPackage newly created elements are filed in.
//
// Taken from the first existing element the batch names, because that is the
// part of the argument the contributor is working on. A flat model recorded no
// package at all, so the choice was deferred to promotion, and guessing there
// put a proposed claim in the wrong package of a multi-package case.
std::string AnchorForOperations(const nlohmann::json& arguments,
                                const std::vector<core::reviews::PatchOperation>& operations) {
    const std::string supplied = StringArgument(arguments, "anchor_element_id");
    if (!supplied.empty())
        return supplied;
    for (const core::reviews::PatchOperation& operation : operations) {
        for (const std::optional<core::reviews::ElementRef>* candidate :
             {&operation.element, &operation.source, &operation.target}) {
            if (candidate->has_value() && candidate->value().existing_id.has_value() &&
                !candidate->value().existing_id.value().empty()) {
                return candidate->value().existing_id.value();
            }
        }
    }
    return {};
}

// Applies a batch to the draft document (ADR 0016).
//
// The document is the only thing that decides here. A refusal comes from the
// model that would have held the change, in the call that made it, which is the
// whole reason this path exists: the operation-staging path rehearsed a flat
// model that accepted fourteen shapes the document later refused -- at accept,
// naming neither the field to correct nor the group to reject.
Result StageOntoDraftDocument(const DraftContext& context,
                              const std::string& group_id,
                              const std::vector<core::reviews::PatchOperation>& operations,
                              const nlohmann::json& arguments) {
    core::drafts::DraftDocumentStore& document = *context.document;
    // The first unaccepted change to this argument is what brings the draft into
    // existence, copied from the argument as it stands right now -- not as it
    // stood when someone opened the file.
    if (!document.active()) {
        std::string create_error;
        if (context.accepted_document == nullptr || !document.EnsureDraft(*context.accepted_document, create_error)) {
            return DraftError(create_error.empty() ? "A working draft could not be created for this argument."
                                                   : create_error,
                              context.working_revision());
        }
    }
    const core::drafts::DraftOperationResult applied = core::drafts::ApplyOperationsToDraftDocument(
        *document.document(), operations, AnchorForOperations(arguments, operations));
    if (!applied.applied) {
        // Positioned, because a client staging ten operations has to be told
        // which one to fix rather than re-send the batch blind. The batch is
        // atomic, so nothing landed and there is nothing to undo first.
        std::string message = applied.error;
        if (applied.failed_operation > 0) {
            message = "Operation " + std::to_string(applied.failed_operation) + " of " +
                      std::to_string(operations.size()) + " was refused: " + applied.error;
        }
        return DraftError(std::move(message), context.working_revision());
    }
    document.MarkChanged();

    // Everything below has already happened to the draft. A failure here is
    // reported as a warning rather than as a refusal, because telling a client
    // its batch was rejected while the draft is holding it invites a retry that
    // would apply the same change twice.
    nlohmann::json warnings = nlohmann::json::array();
    std::string save_error;
    if (!document.Save(save_error))
        warnings.push_back("The draft is updated but could not be written to disk: " + save_error);
    std::string ledger_error;
    if (!context.store.RecordAppliedOperations(group_id, operations, ledger_error))
        warnings.push_back("The draft is updated but the change group's record of it is incomplete: " + ledger_error);

    const DraftDocumentSnapshot snapshot = SnapshotDraftDocument(context);
    const core::drafts::DraftWorkspace* workspace = context.store.workspace();
    const core::drafts::DraftChangeGroup* group = workspace != nullptr ? workspace->FindGroup(group_id) : nullptr;
    nlohmann::json payload = group != nullptr ? GroupJson(*group, true) : nlohmann::json::object();
    payload["staged"] = static_cast<int>(operations.size());
    // The same key the operation-staging path answers with, holding real ids
    // this time: the document allocated them when it created the element, and
    // there is no later materialization that could allocate a different one. A
    // client may address what it just made immediately.
    payload["created_element_ids"] = applied.created_ids;
    payload["changes"] = ChangesJson(snapshot.diff);
    payload["added"] = snapshot.diff.added_count;
    payload["modified"] = snapshot.diff.modified_count;
    payload["removed"] = snapshot.diff.removed_count;
    payload["findings"] = DocumentFindingsJson(snapshot);
    if (!warnings.empty())
        payload["warnings"] = std::move(warnings);
    AddWorkspaceEnvelope(context, payload);
    return Result::Ok(std::move(payload));
}

} // namespace

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
                          context.working_revision());

    if (context.document_backed())
        return StageOntoDraftDocument(context, group_id, operations, arguments);

    // No draft document to edit -- an argument the SACM library could not load.
    // The operation-staging path remains for it, expressibility gap included,
    // until that store is retired.
    if (!context.store.StageOperations(group_id, operations, context.state.loaded_case.value(), error))
        return DraftError("These operations would not materialize in the integrated draft: " + error,
                          context.working_revision());

    const core::drafts::DraftMaterializationResult& materialized =
        context.store.Materialize(context.state.loaded_case.value(), context.state.case_revision);
    if (!materialized.success)
        return DraftError(materialized.error, context.working_revision());

    const core::drafts::DraftChangeGroup* group = context.store.workspace()->FindGroup(group_id);
    nlohmann::json payload = GroupJson(*group, true);
    payload["staged"] = static_cast<int>(operations.size());
    payload["findings"] = FindingsJson(materialized);
    payload["checked"] = CheckedJson();
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
    if (materialized.success) {
        payload["findings"] = FindingsJson(materialized);
        payload["checked"] = CheckedJson();
    } else {
        payload["problem"] = materialized.error;
    }
    // Ids allocated during the rehearsal die with it, and returning them would
    // invite an agent to refer to an element that will get a different id when
    // the operations are really staged.
    payload["rehearsal"] = "Nothing was staged and no element ids were allocated. When the operations are "
                           "clean, stage them with stage_operations.";
    AddWorkspaceEnvelope(context, payload);
    return Result::Ok(std::move(payload));
}

// A gesture that only means something against an operation log.
//
// Refused rather than half-honoured. Withdrawing operations from the ledger
// while the draft document keeps the change would report success over an edit
// that is still there -- the silent-drop shape ADR 0016 exists to remove, merely
// moved to a different call.
namespace {

Result NotAvailableAgainstDraftDocument(const DraftContext& context, const char* gesture) {
    return DraftError(std::string("The working draft is a SACM document rather than a list of operations, so ") +
                          gesture +
                          " no longer means anything. Reverse the change by editing the draft -- set the text back, "
                          "or remove the element you added -- or ask the user to discard the whole draft.",
                      context.working_revision());
}

} // namespace

Result ReplaceChangeGroup(const DraftContext& context, const nlohmann::json& arguments) {
    if (!context.state.loaded_case.has_value())
        return Result::Error("No assurance case is loaded, so there is nothing to draft against.");
    Result refusal;
    if (!CheckExpectedRevision(context, arguments, refusal))
        return refusal;
    if (context.document_backed())
        return NotAvailableAgainstDraftDocument(context, "replacing a change group's operations");

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
                          context.working_revision());
    if (!context.store.ReplaceOperations(group_id, operations, context.state.loaded_case.value(), error))
        return DraftError("These replacement operations would not materialize in the integrated draft: " + error,
                          context.working_revision());
    return DescribeChangeGroup(context, nlohmann::json{{"group_id", group_id}});
}

Result RemoveChangeGroup(const DraftContext& context, const nlohmann::json& arguments) {
    Result refusal;
    if (!CheckExpectedRevision(context, arguments, refusal))
        return refusal;
    if (context.document_backed())
        return NotAvailableAgainstDraftDocument(context, "rejecting a change group");
    const std::string group_id = GroupIdArgument(context, arguments);
    if (group_id.empty())
        return MissingGroup(context);
    if (FindOwnedGroup(context, group_id, refusal) == nullptr)
        return refusal;

    std::string error;
    if (!context.store.RejectGroup(group_id, error))
        return DraftError(error, context.working_revision());
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
        return DraftError("No draft change group has the id \"" + group_id + "\".", context.working_revision());
    return DescribeGroupResult(context, *group);
}

// The Problem-severity findings standing against one group in the current
// working draft, each named the way the group's author saw it in the staging
// results. Advisory findings are deliberately absent: they are the reviewer's
// judgement call, and gating on them would train agents to acknowledge
// reflexively -- which would spend the one gate this surface has.
static std::vector<std::string> StandingProblemFindings(const DraftContext& context, const std::string& group_id) {
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
        return DraftError(error, context.working_revision());
    return DescribeChangeGroup(context, nlohmann::json{{"group_id", group_id}});
}

Result DescribeWorkingDraft(const DraftContext& context) {
    if (!context.state.loaded_case.has_value())
        return Result::Error("No assurance case is loaded.");

    nlohmann::json payload;
    if (context.document_backed()) {
        const DraftDocumentSnapshot snapshot = SnapshotDraftDocument(context);
        payload["element_count"] = static_cast<int>(snapshot.projection.elements.size());
        payload["changes"] = ChangesJson(snapshot.diff);
        payload["added"] = snapshot.diff.added_count;
        payload["modified"] = snapshot.diff.modified_count;
        payload["removed"] = snapshot.diff.removed_count;
        payload["findings"] = DocumentFindingsJson(snapshot);
        AddWorkspaceEnvelope(context, payload);
        return Result::Ok(std::move(payload));
    }
    const core::drafts::DraftMaterializationResult& materialized =
        context.store.Materialize(context.state.loaded_case.value(), context.state.case_revision);
    payload["materializes"] = materialized.success;
    payload["element_count"] = static_cast<int>(materialized.working_model.elements.size());
    payload["findings"] = FindingsJson(materialized);
    if (materialized.success) {
        // Only where the checks actually ran. A coverage statement beside a
        // result that could not be computed would read as "these checks found
        // nothing", when the truth is that nothing was checked at all.
        payload["checked"] = CheckedJson();
    } else {
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
    if (context.document_backed())
        return NotAvailableAgainstDraftDocument(context, "unstaging operations");
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
        return DraftError("Cannot remove more operations than the group contains.", context.working_revision());

    std::vector<core::reviews::PatchOperation> remaining = group->operations;
    remaining.resize(remaining.size() - count);
    std::string error;
    if (!context.store.ReplaceOperations(group_id, remaining, context.state.loaded_case.value(), error))
        return DraftError(error, context.working_revision());
    return DescribeChangeGroup(context, nlohmann::json{{"group_id", group_id}});
}

} // namespace agent
