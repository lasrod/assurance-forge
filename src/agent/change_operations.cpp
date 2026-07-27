#include "agent/operations.h"

#include "core/reviews/review_proposal.h"
#include "core/sccg/staged_checks.h"
#include "parser/model_utils.h"

#include <optional>
#include <string>
#include <vector>

namespace agent {
namespace {

std::string StringArgument(const nlohmann::json& arguments, const char* key) {
    const nlohmann::json::const_iterator found = arguments.find(key);
    if (found == arguments.end() || !found->is_string()) {
        return {};
    }
    return found->get<std::string>();
}

bool ParseElementRef(const nlohmann::json& source, const char* field,
                     std::optional<core::reviews::ElementRef>& out, std::string& error) {
    const nlohmann::json::const_iterator found = source.find(field);
    if (found == source.end() || found->is_null()) {
        return true; // Absent is fine; the operation type decides what it needs.
    }
    if (!found->is_object()) {
        error = std::string(field) +
                " must be an object like {\"id\": \"G1\"} or {\"ref\": \"$goal\"}.";
        return false;
    }

    const nlohmann::json::const_iterator id  = found->find("id");
    const nlohmann::json::const_iterator ref = found->find("ref");
    const bool has_id  = id != found->end() && id->is_string() && !id->get<std::string>().empty();
    const bool has_ref = ref != found->end() && ref->is_string() && !ref->get<std::string>().empty();
    if (has_id == has_ref) {
        error = std::string(field) + " must carry exactly one of \"id\" (an existing element) or "
                                     "\"ref\" (one this change set creates).";
        return false;
    }

    core::reviews::ElementRef parsed;
    if (has_id) {
        parsed.existing_id = id->get<std::string>();
    } else {
        parsed.create_ref = ref->get<std::string>();
    }
    out = parsed;
    return true;
}

bool ParseOne(const nlohmann::json& source, core::reviews::PatchOperation& out,
              std::string& error) {
    if (!source.is_object()) {
        error = "Each operation must be an object.";
        return false;
    }
    const std::string type = StringArgument(source, "type");
    if (type.empty()) {
        error = "Each operation needs a \"type\".";
        return false;
    }
    if (!core::reviews::PatchOperationTypeFromString(type, out.type)) {
        error = "Unknown operation type \"" + type + "\".";
        return false;
    }

    if (!ParseElementRef(source, "element", out.element, error) ||
        !ParseElementRef(source, "source", out.source, error) ||
        !ParseElementRef(source, "target", out.target, error)) {
        return false;
    }

    const std::string create_ref = StringArgument(source, "create_ref");
    if (!create_ref.empty()) {
        out.create_ref = create_ref;
    }
    out.field     = StringArgument(source, "field");
    out.old_value = StringArgument(source, "old_value");
    out.new_value = StringArgument(source, "new_value");
    out.text      = StringArgument(source, "text");
    return true;
}

// The reviewer is being asked to approve a change to a safety argument, so the
// description has to say what moves and where, not just how many operations
// there are.
nlohmann::json DiffJson(const core::changesets::ChangeSetDiff& diff,
                        const parser::AssuranceCase&           committed) {
    nlohmann::json added    = nlohmann::json::array();
    nlohmann::json modified = nlohmann::json::array();
    nlohmann::json removed  = nlohmann::json::array();

    for (const std::pair<const std::string, core::changesets::ElementChange>& entry :
         diff.status_by_id) {
        if (entry.second == core::changesets::ElementChange::Unchanged ||
            entry.second == core::changesets::ElementChange::Removed) {
            continue;
        }
        const parser::SacmElement* element =
            parser::FindElementById(diff.preview_model, entry.first);
        if (element == nullptr) {
            continue;
        }
        nlohmann::json described{{"id", element->id}, {"type", element->type}};
        if (!element->name.empty()) {
            described["name"] = element->name;
        }
        if (!element->content.empty()) {
            described["content"] = element->content;
        }
        if (entry.second == core::changesets::ElementChange::Added) {
            added.push_back(std::move(described));
        } else {
            const parser::SacmElement* before = parser::FindElementById(committed, entry.first);
            if (before != nullptr && before->content != element->content) {
                described["content_was"] = before->content;
            }
            if (before != nullptr && before->name != element->name) {
                described["name_was"] = before->name;
            }
            modified.push_back(std::move(described));
        }
    }

    for (const parser::SacmElement& element : diff.removed) {
        nlohmann::json described{{"id", element.id}, {"type", element.type}};
        if (!element.name.empty()) {
            described["name"] = element.name;
        }
        if (!element.content.empty()) {
            described["content"] = element.content;
        }
        removed.push_back(std::move(described));
    }

    return nlohmann::json{
        {"added", std::move(added)},
        {"modified", std::move(modified)},
        {"removed", std::move(removed)},
        {"added_count", diff.added_count},
        {"modified_count", diff.modified_count},
        {"removed_count", diff.removed_count},
        {"created_element_ids", diff.generated_ids},
    };
}

// SCCG findings against what the change set would produce, so an agent can fix
// an obviously wrong shape before a reviewer spends attention on it. Only a
// named, mechanically-decidable subset is checked -- see core/sccg/staged_checks.h
// -- and nothing here blocks acceptance.
nlohmann::json SccgFindingsJson(const core::changesets::ChangeSetDiff& diff) {
    std::vector<std::string> touched;
    for (const std::pair<const std::string, core::changesets::ElementChange>& entry :
         diff.status_by_id) {
        if (entry.second != core::changesets::ElementChange::Unchanged &&
            entry.second != core::changesets::ElementChange::Removed) {
            touched.push_back(entry.first);
        }
    }

    nlohmann::json findings = nlohmann::json::array();
    for (const core::sccg::StagedFinding& finding :
         core::sccg::CheckStagedArgument(diff.preview_model, touched)) {
        findings.push_back(nlohmann::json{
            {"guideline_id", finding.guideline_id},
            {"guideline", finding.statement},
            {"element_id", finding.element_id},
            {"detail", finding.detail},
            {"severity", core::sccg::FindingSeverityToString(finding.severity)},
        });
    }
    return findings;
}

nlohmann::json ChangeSetJson(const core::changesets::ChangeSet& change_set) {
    return nlohmann::json{
        {"change_set_id", change_set.id},
        {"title", change_set.title},
        {"summary", change_set.summary},
        {"intent", change_set.intent},
        {"state", core::changesets::ChangeSetStateToString(change_set.state)},
        {"client", change_set.client_label},
        {"operation_count", static_cast<int>(change_set.proposal.operations.size())},
    };
}

Result NoCase() {
    return Result::Error("No assurance case is loaded, so there is nothing to change.");
}

// Every reply about a change set carries the same reminder. An agent that
// believes it has edited the case will tell the user so, and the user will
// believe it -- which is the failure mode this whole design exists to avoid.
constexpr const char* kNotAppliedNote =
    "Nothing has changed in the safety case. This is a proposal the user reviews on the canvas "
    "and accepts or rejects in Assurance Forge; only they can apply it.";

} // namespace

const std::vector<std::string>& PatchOperationTypeNames() {
    static const std::vector<std::string> names{
        "CreateClaim",      "CreateStrategy",  "CreateSolution",   "CreateContext",
        "CreateAssumption", "CreateJustification", "UpdateElementText", "UpdateElementName",
        "SetUndeveloped",   "ClearUndeveloped", "AddSupportedBy",  "RemoveSupportedBy",
        "AddInContextOf",   "RemoveInContextOf", "RemoveElement"};
    return names;
}

bool ParsePatchOperations(const nlohmann::json&                       source,
                          std::vector<core::reviews::PatchOperation>& out, std::string& error) {
    out.clear();
    if (!source.is_array() || source.empty()) {
        error = "\"operations\" must be a non-empty array.";
        return false;
    }
    for (const nlohmann::json& entry : source) {
        core::reviews::PatchOperation parsed;
        if (!ParseOne(entry, parsed, error)) {
            return false;
        }
        out.push_back(std::move(parsed));
    }
    return true;
}

Result BeginChangeSet(const ChangeContext& context, const nlohmann::json& arguments) {
    if (!context.state.loaded_case.has_value()) {
        return NoCase();
    }
    const std::string title = StringArgument(arguments, "title");
    if (title.empty()) {
        return Result::Error("Argument \"title\" is required. It is what the user sees on the "
                             "canvas while you build this change.");
    }

    const std::string id =
        context.store.Begin(context.connection_id, title, StringArgument(arguments, "summary"),
                            StringArgument(arguments, "intent"), context.client_label);

    const core::changesets::ChangeSet* change_set = context.store.Find(id);
    nlohmann::json                     payload    = ChangeSetJson(*change_set);
    payload["note"] =
        std::string("The user can now see this change set in Assurance Forge and will watch it "
                    "take shape as you stage operations. ") +
        kNotAppliedNote;
    return Result::Ok(std::move(payload));
}

Result StageOperations(const ChangeContext& context, const nlohmann::json& arguments) {
    if (!context.state.loaded_case.has_value()) {
        return NoCase();
    }

    std::string id = StringArgument(arguments, "change_set_id");
    if (id.empty()) {
        // Falling back to this connection's open change set keeps the common
        // case -- one agent, one change -- free of bookkeeping.
        const core::changesets::ChangeSet* open = context.store.OpenFor(context.connection_id);
        if (open == nullptr) {
            return Result::Error("No change set is open for this connection. Call "
                                 "begin_change_set first.");
        }
        id = open->id;
    }

    std::vector<core::reviews::PatchOperation> operations;
    std::string                                error;
    const nlohmann::json::const_iterator       supplied = arguments.find("operations");
    if (supplied == arguments.end() ||
        !ParsePatchOperations(*supplied, operations, error)) {
        return Result::Error(error.empty() ? "\"operations\" must be a non-empty array." : error);
    }

    if (!context.store.Stage(id, operations, context.state.loaded_case.value(), error)) {
        return Result::Error("These operations would not apply: " + error);
    }

    const core::changesets::ChangeSet* change_set = context.store.Find(id);
    const core::changesets::ChangeSetDiff diff =
        core::changesets::ComputeChangeSetDiff(*change_set, context.state.loaded_case.value());

    nlohmann::json payload = ChangeSetJson(*change_set);
    payload["staged"]      = static_cast<int>(operations.size());
    payload["diff"]        = DiffJson(diff, context.state.loaded_case.value());
    payload["sccg_findings"] = SccgFindingsJson(diff);
    payload["note"]          = kNotAppliedNote;
    return Result::Ok(std::move(payload));
}

Result UnstageOperations(const ChangeContext& context, const nlohmann::json& arguments) {
    if (!context.state.loaded_case.has_value()) {
        return NoCase();
    }
    std::string id = StringArgument(arguments, "change_set_id");
    if (id.empty()) {
        const core::changesets::ChangeSet* open = context.store.OpenFor(context.connection_id);
        if (open == nullptr) {
            return Result::Error("No change set is open for this connection.");
        }
        id = open->id;
    }

    const nlohmann::json::const_iterator count = arguments.find("count");
    const std::size_t                    drop =
        count != arguments.end() && count->is_number_integer() && count->get<int>() > 0
                               ? static_cast<std::size_t>(count->get<int>())
                               : 1;

    std::string error;
    if (!context.store.Unstage(id, drop, error)) {
        return Result::Error(error);
    }

    const core::changesets::ChangeSet* change_set = context.store.Find(id);
    const core::changesets::ChangeSetDiff diff =
        core::changesets::ComputeChangeSetDiff(*change_set, context.state.loaded_case.value());

    nlohmann::json payload = ChangeSetJson(*change_set);
    payload["diff"]        = DiffJson(diff, context.state.loaded_case.value());
    payload["note"]        = kNotAppliedNote;
    return Result::Ok(std::move(payload));
}

Result DescribeChangeSet(const ChangeContext& context, const nlohmann::json& arguments) {
    if (!context.state.loaded_case.has_value()) {
        return NoCase();
    }
    std::string id = StringArgument(arguments, "change_set_id");
    if (id.empty()) {
        const core::changesets::ChangeSet* open = context.store.OpenFor(context.connection_id);
        if (open == nullptr) {
            return Result::Error("No change set is open for this connection.");
        }
        id = open->id;
    }

    const core::changesets::ChangeSet* change_set = context.store.Find(id);
    if (change_set == nullptr) {
        return Result::Error("No change set has the id \"" + id + "\".");
    }

    const core::changesets::ChangeSetDiff diff =
        core::changesets::ComputeChangeSetDiff(*change_set, context.state.loaded_case.value());

    nlohmann::json payload = ChangeSetJson(*change_set);
    if (!diff.success) {
        // The case moved under the change set. Reported rather than hidden: the
        // agent needs to re-read and rebuild, not retry.
        payload["applies"] = false;
        payload["problem"] = diff.error;
        return Result::Ok(std::move(payload));
    }
    payload["applies"]       = true;
    payload["diff"]          = DiffJson(diff, context.state.loaded_case.value());
    payload["sccg_findings"] = SccgFindingsJson(diff);
    payload["note"]          = kNotAppliedNote;
    return Result::Ok(std::move(payload));
}

Result SubmitChangeSet(const ChangeContext& context, const nlohmann::json& arguments) {
    if (!context.state.loaded_case.has_value()) {
        return NoCase();
    }
    std::string id = StringArgument(arguments, "change_set_id");
    if (id.empty()) {
        const core::changesets::ChangeSet* open = context.store.OpenFor(context.connection_id);
        if (open == nullptr) {
            return Result::Error("No change set is open for this connection.");
        }
        id = open->id;
    }

    std::string error;
    if (!context.store.MarkReady(id, error)) {
        return Result::Error(error);
    }

    const core::changesets::ChangeSet* change_set = context.store.Find(id);
    nlohmann::json                     payload    = ChangeSetJson(*change_set);
    payload["note"] =
        std::string("Handed to the user for a decision. ") + kNotAppliedNote +
        " Do not tell them the change has been made; ask them to review it in Assurance Forge.";
    return Result::Ok(std::move(payload));
}

Result DiscardChangeSet(const ChangeContext& context, const nlohmann::json& arguments) {
    std::string id = StringArgument(arguments, "change_set_id");
    if (id.empty()) {
        const core::changesets::ChangeSet* open = context.store.OpenFor(context.connection_id);
        if (open == nullptr) {
            return Result::Error("No change set is open for this connection.");
        }
        id = open->id;
    }

    std::string error;
    if (!context.store.Discard(id, error)) {
        return Result::Error(error);
    }
    return Result::Ok(nlohmann::json{{"change_set_id", id}, {"state", "discarded"}});
}

Result ListChangeSets(const ChangeContext& context) {
    nlohmann::json open = nlohmann::json::array();
    for (const core::changesets::ChangeSet* change_set : context.store.Open()) {
        open.push_back(ChangeSetJson(*change_set));
    }
    return Result::Ok(nlohmann::json{{"change_sets", std::move(open)}});
}

} // namespace agent
