#include "core/commands/pattern_commands.h"

#include "core/element_factory.h"
#include "core/pattern_model.h"

namespace core::commands {

bool CreatePatternCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    const core::PatternCreateResult result =
        core::CreatePatternPackage(ctx.package, name_, identifier_, description_);
    if (!result.success) {
        out_error = result.error;
        return false;
    }
    generated_id_ = result.package_id;
    generated_gid_ = result.package_gid;

    out_event.event_type = "CreatePattern";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["name"] = name_;
    out_event.payload["identifier"] = identifier_;
    out_event.payload["description"] = description_;
    out_event.payload["generated_id"] = generated_id_;
    out_event.payload["generated_gid"] = generated_gid_;
    return true;
}

bool SetUninstantiatedCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (!core::SetElementUninstantiated(ctx.model, &ctx.package, element_id_, value_, out_error))
        return false;

    out_event.event_type = "SetUninstantiated";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["element_id"] = element_id_;
    out_event.payload["value"] = value_;
    return true;
}

bool SetUndevelopedCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (!core::SetElementUndeveloped(ctx.model, &ctx.package, element_id_, value_, out_error))
        return false;

    out_event.event_type = "SetUndeveloped";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["element_id"] = element_id_;
    out_event.payload["value"] = value_;
    return true;
}

std::string RelationOperatorToToken(core::PatternRelationOperator op) {
    switch (op) {
    case core::PatternRelationOperator::Optional:
        return "optional";
    case core::PatternRelationOperator::Multiplicity:
        return "multiplicity";
    case core::PatternRelationOperator::None:
        break;
    }
    return "none";
}

bool RelationOperatorFromToken(const std::string& token, core::PatternRelationOperator& out) {
    if (token == "none") {
        out = core::PatternRelationOperator::None;
        return true;
    }
    if (token == "optional") {
        out = core::PatternRelationOperator::Optional;
        return true;
    }
    if (token == "multiplicity") {
        out = core::PatternRelationOperator::Multiplicity;
        return true;
    }
    return false;
}

bool SetRelationshipPatternCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (!core::SetRelationshipPatternData(ctx.package, relationship_id_, data_, out_error))
        return false;

    out_event.event_type = "SetRelationshipPattern";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["relationship_id"] = relationship_id_;
    out_event.payload["operator"] = RelationOperatorToToken(data_.relationOperator);
    if (data_.relationOperator == core::PatternRelationOperator::Multiplicity && data_.multiplicity.has_value()) {
        out_event.payload["cardinality_min"] = core::PatternBoundToToken(data_.multiplicity->minimum);
        out_event.payload["cardinality_max"] = core::PatternBoundToToken(data_.multiplicity->maximum);
        out_event.payload["cardinality_display"] = data_.multiplicity->displayExpression;
    }
    return true;
}

namespace {

void WriteCardinalityPayload(nlohmann::ordered_json& payload, const core::PatternCardinality& cardinality) {
    payload["cardinality_min"] = core::PatternBoundToToken(cardinality.minimum);
    payload["cardinality_max"] = core::PatternBoundToToken(cardinality.maximum);
    payload["cardinality_display"] = cardinality.displayExpression;
}

} // namespace

bool CreateChoiceGroupCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    std::string relationship_type;
    const std::vector<std::string> members =
        core::GatherChoiceCandidateRelationships(ctx.package, source_element_id_, relationship_type);
    if (members.size() < 2) {
        out_error = "Need at least two same-type SupportedBy alternatives under this element to form a choice.";
        return false;
    }
    // Default cardinality is "1 of n" (1..<count>) when the caller supplied none.
    core::PatternCardinality cardinality = cardinality_;
    if (cardinality.displayExpression.empty())
        cardinality = core::ParseCardinalityExpression("1.." + std::to_string(members.size()));

    const core::ChoiceGroupCreateResult result =
        core::CreateChoiceGroupFromRelationships(ctx.package, members, cardinality);
    if (!result.success) {
        out_error = result.error;
        return false;
    }
    generated_group_id_ = result.group_id;

    out_event.event_type = "CreateChoiceGroup";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["source_element_id"] = source_element_id_;
    out_event.payload["group_id"] = generated_group_id_;
    out_event.payload["member_ids"] = members;
    WriteCardinalityPayload(out_event.payload, cardinality);
    return true;
}

bool RemoveChoiceGroupCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (!core::RemoveChoiceGroup(ctx.package, group_id_, out_error))
        return false;

    out_event.event_type = "RemoveChoiceGroup";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["group_id"] = group_id_;
    return true;
}

bool SetChoiceCardinalityCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (!core::SetChoiceCardinality(ctx.package, group_id_, cardinality_, out_error))
        return false;

    out_event.event_type = "SetChoiceCardinality";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["group_id"] = group_id_;
    WriteCardinalityPayload(out_event.payload, cardinality_);
    return true;
}

} // namespace core::commands
