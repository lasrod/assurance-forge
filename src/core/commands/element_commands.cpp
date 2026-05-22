#include "core/commands/element_commands.h"

#include <algorithm>

namespace core::commands {

std::string NewElementKindToToken(NewElementKind kind) {
    switch (kind) {
    case NewElementKind::Goal:           return "Goal";
    case NewElementKind::Strategy:       return "Strategy";
    case NewElementKind::Solution:       return "Solution";
    case NewElementKind::Context:        return "Context";
    case NewElementKind::Assumption:     return "Assumption";
    case NewElementKind::Justification:  return "Justification";
    }
    return "Goal";
}

bool NewElementKindFromToken(const std::string& token, NewElementKind& out) {
    if (token == "Goal")          { out = NewElementKind::Goal; return true; }
    if (token == "Strategy")      { out = NewElementKind::Strategy; return true; }
    if (token == "Solution")      { out = NewElementKind::Solution; return true; }
    if (token == "Context")       { out = NewElementKind::Context; return true; }
    if (token == "Assumption")    { out = NewElementKind::Assumption; return true; }
    if (token == "Justification") { out = NewElementKind::Justification; return true; }
    return false;
}

std::string RemoveModeToToken(RemoveMode mode) {
    switch (mode) {
    case RemoveMode::NodeOnly:           return "NodeOnly";
    case RemoveMode::NodeAndDescendants: return "NodeAndDescendants";
    }
    return "NodeOnly";
}

bool RemoveModeFromToken(const std::string& token, RemoveMode& out) {
    if (token == "NodeOnly")           { out = RemoveMode::NodeOnly; return true; }
    if (token == "NodeAndDescendants") { out = RemoveMode::NodeAndDescendants; return true; }
    return false;
}

bool CreateTopGoalCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (!core::AddTopGoal(ctx.model, &ctx.package, generated_id_, out_error))
        return false;

    out_event.event_type = "CreateTopGoal";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["generated_id"] = generated_id_;
    return true;
}

bool CreateChildElementCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (parent_id_.empty()) {
        out_error = "CreateChildElementCommand requires a parent id";
        return false;
    }
    if (!core::AddChildElement(ctx.model, &ctx.package, parent_id_, kind_, generated_id_,
                               generated_relationship_id_, out_error))
        return false;

    out_event.event_type = "CreateChildElement";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["parent_id"] = parent_id_;
    out_event.payload["kind"] = NewElementKindToToken(kind_);
    out_event.payload["generated_id"] = generated_id_;
    out_event.payload["generated_relationship_id"] = generated_relationship_id_;
    return true;
}

bool RemoveElementCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (element_id_.empty()) {
        out_error = "RemoveElementCommand requires an element id";
        return false;
    }

    auto planned = core::PlanRemoval(ctx.model, element_id_, mode_);
    if (planned.empty()) {
        out_error = "Nothing to remove for element " + element_id_;
        return false;
    }

    std::vector<std::string> deleted_ids(planned.begin(), planned.end());
    std::sort(deleted_ids.begin(), deleted_ids.end());

    if (!core::RemoveElement(ctx.model, &ctx.package, element_id_, mode_, out_error))
        return false;

    removed_count_ = deleted_ids.size();

    out_event.event_type = "RemoveElement";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["element_id"] = element_id_;
    out_event.payload["mode"] = RemoveModeToToken(mode_);
    out_event.payload["deleted_ids"] = deleted_ids;
    return true;
}

bool UpdateElementTextCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event,
                                     std::string& out_error) {
    if (element_id_.empty()) {
        out_error = "UpdateElementTextCommand requires an element id";
        return false;
    }
    if (language_.empty()) {
        out_error = "UpdateElementTextCommand requires a language code";
        return false;
    }
    if (!core::SetElementTextField(ctx.model, &ctx.package, element_id_, field_, language_,
                                   new_value_, old_value_, out_error))
        return false;

    was_no_op_ = (old_value_ == new_value_);

    out_event.event_type = "UpdateElementText";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["element_id"] = element_id_;
    out_event.payload["field"] = core::ElementTextFieldToToken(field_);
    out_event.payload["language"] = language_;
    out_event.payload["old_value"] = old_value_;
    out_event.payload["new_value"] = new_value_;
    return true;
}

} // namespace core::commands
