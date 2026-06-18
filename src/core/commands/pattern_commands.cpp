#include "core/commands/pattern_commands.h"

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

} // namespace core::commands
