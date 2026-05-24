#include "core/commands/terminology_commands.h"

#include "core/terminology_context_projection.h"

namespace core::commands {

bool CreateTerminologyPackageCommand::Apply(CommandContext&   ctx,
                                            audit::AuditEvent& out_event,
                                            std::string&       out_error) {
    core::TerminologyPackageCreateResult result =
        core::CreateTerminologyPackage(ctx.package, name_, description_);
    if (!result.success) {
        out_error = result.error;
        return false;
    }
    generated_ref_ = result.package_ref;

    out_event.event_type = "CreateTerminologyPackage";
    out_event.payload    = nlohmann::ordered_json::object();
    out_event.payload["name"]          = name_;
    out_event.payload["description"]   = description_;
    out_event.payload["generated_id"]  = generated_ref_.id;
    out_event.payload["generated_gid"] = generated_ref_.gid;
    return true;
}

bool UpdateTerminologyPackageCommand::Apply(CommandContext&   ctx,
                                            audit::AuditEvent& out_event,
                                            std::string&       out_error) {
    if (!core::UpdateTerminologyPackage(ctx.package, package_ref_, name_, description_, out_error))
        return false;

    out_event.event_type = "UpdateTerminologyPackage";
    out_event.payload    = nlohmann::ordered_json::object();
    out_event.payload["package_id"]  = package_ref_.id;
    out_event.payload["package_gid"] = package_ref_.gid;
    out_event.payload["name"]        = name_;
    out_event.payload["description"] = description_;
    return true;
}

bool CreateTerminologyCategoryCommand::Apply(CommandContext&   ctx,
                                             audit::AuditEvent& out_event,
                                             std::string&       out_error) {
    core::TerminologyCategoryCreateResult result =
        core::CreateTerminologyCategory(ctx.package, package_ref_, draft_);
    if (!result.success) {
        out_error = result.error;
        return false;
    }
    generated_ref_ = result.category_ref;

    out_event.event_type = "CreateTerminologyCategory";
    out_event.payload    = nlohmann::ordered_json::object();
    out_event.payload["package_id"]    = package_ref_.id;
    out_event.payload["package_gid"]   = package_ref_.gid;
    out_event.payload["name"]          = draft_.name;
    out_event.payload["description"]   = draft_.description;
    out_event.payload["generated_id"]  = generated_ref_.id;
    out_event.payload["generated_gid"] = generated_ref_.gid;
    return true;
}

bool UpdateTerminologyCategoryCommand::Apply(CommandContext&   ctx,
                                             audit::AuditEvent& out_event,
                                             std::string&       out_error) {
    if (!core::UpdateTerminologyCategory(ctx.package, package_ref_, category_ref_, draft_, out_error))
        return false;

    out_event.event_type = "UpdateTerminologyCategory";
    out_event.payload    = nlohmann::ordered_json::object();
    out_event.payload["package_id"]   = package_ref_.id;
    out_event.payload["package_gid"]  = package_ref_.gid;
    out_event.payload["category_id"]  = category_ref_.id;
    out_event.payload["category_gid"] = category_ref_.gid;
    out_event.payload["name"]         = draft_.name;
    out_event.payload["description"]  = draft_.description;
    return true;
}

bool DeleteTerminologyCategoryCommand::Apply(CommandContext&   ctx,
                                             audit::AuditEvent& out_event,
                                             std::string&       out_error) {
    if (!core::DeleteTerminologyCategory(ctx.package, package_ref_, category_ref_, out_error))
        return false;

    out_event.event_type = "DeleteTerminologyCategory";
    out_event.payload    = nlohmann::ordered_json::object();
    out_event.payload["package_id"]   = package_ref_.id;
    out_event.payload["package_gid"]  = package_ref_.gid;
    out_event.payload["category_id"]  = category_ref_.id;
    out_event.payload["category_gid"] = category_ref_.gid;
    return true;
}

namespace {

void WriteTermDraftToPayload(nlohmann::ordered_json& payload, const core::TerminologyTermDraft& draft) {
    payload["value"]              = draft.value;
    payload["name"]               = draft.name;
    payload["description"]        = draft.description;
    payload["category_refs"]      = draft.category_refs;
    payload["external_reference"] = draft.externalReference;
    payload["origin"]             = draft.origin;
}

} // namespace

bool CreateTerminologyTermCommand::Apply(CommandContext&   ctx,
                                         audit::AuditEvent& out_event,
                                         std::string&       out_error) {
    core::TerminologyTermCreateResult result =
        core::CreateTerminologyTerm(ctx.package, package_ref_, draft_);
    if (!result.success) {
        out_error = result.error;
        return false;
    }
    generated_ref_ = result.term_ref;

    out_event.event_type = "CreateTerminologyTerm";
    out_event.payload    = nlohmann::ordered_json::object();
    out_event.payload["package_id"]  = package_ref_.id;
    out_event.payload["package_gid"] = package_ref_.gid;
    WriteTermDraftToPayload(out_event.payload, draft_);
    out_event.payload["generated_id"]  = generated_ref_.id;
    out_event.payload["generated_gid"] = generated_ref_.gid;
    return true;
}

bool UpdateTerminologyTermCommand::Apply(CommandContext&   ctx,
                                         audit::AuditEvent& out_event,
                                         std::string&       out_error) {
    if (!core::UpdateTerminologyTerm(ctx.package, package_ref_, term_ref_, draft_, out_error))
        return false;
    core::RefreshVisibleTerminologyContextProjection(ctx.model, ctx.package);

    out_event.event_type = "UpdateTerminologyTerm";
    out_event.payload    = nlohmann::ordered_json::object();
    out_event.payload["package_id"]  = package_ref_.id;
    out_event.payload["package_gid"] = package_ref_.gid;
    out_event.payload["term_id"]     = term_ref_.id;
    out_event.payload["term_gid"]    = term_ref_.gid;
    WriteTermDraftToPayload(out_event.payload, draft_);
    return true;
}

bool DeleteTerminologyTermCommand::Apply(CommandContext&   ctx,
                                         audit::AuditEvent& out_event,
                                         std::string&       out_error) {
    if (!core::DeleteTerminologyTerm(ctx.package, package_ref_, term_ref_, out_error))
        return false;
    core::RefreshVisibleTerminologyContextProjection(ctx.model, ctx.package);

    out_event.event_type = "DeleteTerminologyTerm";
    out_event.payload    = nlohmann::ordered_json::object();
    out_event.payload["package_id"]  = package_ref_.id;
    out_event.payload["package_gid"] = package_ref_.gid;
    out_event.payload["term_id"]     = term_ref_.id;
    out_event.payload["term_gid"]    = term_ref_.gid;
    return true;
}

namespace {

void WriteContextAssociationPayload(nlohmann::ordered_json&                          payload,
                                    const std::string&                               element_id,
                                    const core::TerminologyPackageRef&               package_ref,
                                    const core::TerminologyTermRef&                  term_ref,
                                    const core::TerminologyContextAssociationResult& result) {
    payload["element_id"]                  = element_id;
    payload["package_id"]                  = package_ref.id;
    payload["package_gid"]                 = package_ref.gid;
    payload["term_id"]                     = term_ref.id;
    payload["term_gid"]                    = term_ref.gid;
    payload["already_associated"]          = result.already_associated;
    payload["created_artifact_reference"]  = result.created_artifact_reference;
    payload["created_asserted_context"]    = result.created_asserted_context;
    payload["artifact_reference_id"]       = result.artifact_reference_id;
    payload["artifact_reference_gid"]      = result.artifact_reference_gid;
    payload["asserted_context_id"]         = result.asserted_context_id;
    payload["asserted_context_gid"]        = result.asserted_context_gid;
}

} // namespace

bool AssociateTerminologyTermWithElementCommand::Apply(CommandContext&   ctx,
                                                       audit::AuditEvent& out_event,
                                                       std::string&       out_error) {
    result_ = core::AssociateTerminologyTermWithElement(ctx.package, element_id_, package_ref_, term_ref_);
    if (!result_.success) {
        out_error = result_.error;
        return false;
    }

    out_event.event_type = "AssociateTerminologyTermWithElement";
    out_event.payload    = nlohmann::ordered_json::object();
    WriteContextAssociationPayload(out_event.payload, element_id_, package_ref_, term_ref_, result_);
    return true;
}

bool AddTerminologyTermAsVisibleContextCommand::Apply(CommandContext&   ctx,
                                                      audit::AuditEvent& out_event,
                                                      std::string&       out_error) {
    result_ = core::AddTerminologyTermAsVisibleContext(ctx.package, element_id_, package_ref_, term_ref_);
    if (!result_.success) {
        out_error = result_.error;
        return false;
    }
    core::SyncVisibleTerminologyContextToParser(ctx.model, ctx.package, result_);

    out_event.event_type = "AddTerminologyTermAsVisibleContext";
    out_event.payload    = nlohmann::ordered_json::object();
    WriteContextAssociationPayload(out_event.payload, element_id_, package_ref_, term_ref_, result_);
    return true;
}

} // namespace core::commands
