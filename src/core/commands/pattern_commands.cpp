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

} // namespace core::commands
