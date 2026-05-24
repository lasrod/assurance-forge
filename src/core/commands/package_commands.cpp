#include "core/commands/package_commands.h"

#include "core/acp/assurance_claim_point.h"
#include "core/terminology_package_service.h"

#include <algorithm>
#include <unordered_set>

namespace core {

namespace {

// Match an SACM element by id (preferred) or gid.
bool MatchesIdentity(const sacm::SacmElement& element,
                     const std::string& id,
                     const std::string& gid) {
    return (!id.empty() && element.id == id) || (!gid.empty() && element.gid == gid);
}

} // namespace

bool DeleteArgumentPackage(sacm::AssuranceCasePackage& package,
                           parser::AssuranceCase& model,
                           const std::string& package_id,
                           const std::string& package_gid,
                           std::string& error) {
    auto& vec = package.argumentPackages;
    const auto it = std::find_if(vec.begin(), vec.end(), [&](const sacm::ArgumentPackage& p) {
        return MatchesIdentity(p, package_id, package_gid);
    });
    if (it == vec.end()) {
        error = "Argument package not found in editable model.";
        return false;
    }

    // Capture identities of every element that lived inside the removed
    // argument package so we can scrub matching parser projections and
    // detect ACPs that referenced this package as their separate
    // confidence argument tree.
    const std::string removed_argument_package_id = it->id;
    std::unordered_set<std::string> removed_element_ids;
    std::unordered_set<std::string> removed_element_gids;
    auto record_identity = [&](const sacm::SacmElement& element) {
        if (!element.id.empty())
            removed_element_ids.insert(element.id);
        if (!element.gid.empty())
            removed_element_gids.insert(element.gid);
    };
    for (const sacm::Claim& claim : it->claims)
        record_identity(claim);
    for (const sacm::ArgumentReasoning& reasoning : it->argumentReasonings)
        record_identity(reasoning);
    for (const sacm::ArtifactReference& artifact_reference : it->artifactReferences)
        record_identity(artifact_reference);
    for (const sacm::AssertedInference& inference : it->assertedInferences)
        record_identity(inference);
    for (const sacm::AssertedContext& context : it->assertedContexts)
        record_identity(context);
    for (const sacm::AssertedEvidence& evidence : it->assertedEvidences)
        record_identity(evidence);
    const bool was_confidence_argument_package = core::acp::IsConfidenceArgumentPackage(*it);

    vec.erase(it);

    // Drop parser projections of every element that lived in the removed
    // argument package so they do not show up as orphans in the GSN tree.
    model.elements.erase(std::remove_if(model.elements.begin(),
                                        model.elements.end(),
                                        [&](const parser::SacmElement& element) {
                                            if (removed_element_ids.count(element.id) > 0)
                                                return true;
                                            if (!element.gid.empty() &&
                                                removed_element_gids.count(element.gid) > 0)
                                                return true;
                                            return false;
                                        }),
                         model.elements.end());

    if (was_confidence_argument_package) {
        for (parser::AcpRecord& acp_record : model.acps) {
            if (acp_record.resolution_kind == "topGoalReference" &&
                acp_record.argument_package_id == removed_argument_package_id) {
                acp_record.argument_package_id.clear();
                acp_record.top_goal_id.clear();
            }
        }
    }
    return true;
}

bool DeleteArtifactPackage(sacm::AssuranceCasePackage& package,
                           const std::string& package_id,
                           const std::string& package_gid,
                           std::string& error) {
    auto& vec = package.artifactPackages;
    const auto it = std::find_if(vec.begin(), vec.end(), [&](const sacm::ArtifactPackage& p) {
        return MatchesIdentity(p, package_id, package_gid);
    });
    if (it == vec.end()) {
        error = "Artifact package not found in editable model.";
        return false;
    }
    vec.erase(it);
    return true;
}

} // namespace core

namespace core::commands {

namespace {

void FillIdentityPayload(audit::AuditEvent& out_event,
                         const std::string& event_type,
                         const std::string& id,
                         const std::string& gid) {
    out_event.event_type = event_type;
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["package_id"] = id;
    out_event.payload["package_gid"] = gid;
}

} // namespace

bool RemoveTerminologyPackageCommand::Apply(CommandContext& ctx,
                                            audit::AuditEvent& out_event,
                                            std::string& out_error) {
    if (id_.empty() && gid_.empty()) {
        out_error = "RemoveTerminologyPackageCommand requires an id or gid.";
        return false;
    }
    if (!core::DeleteTerminologyPackage(ctx.package,
                                        core::TerminologyPackageRef{id_, gid_},
                                        out_error)) {
        return false;
    }
    FillIdentityPayload(out_event, "RemoveTerminologyPackage", id_, gid_);
    return true;
}

bool RemoveArgumentPackageCommand::Apply(CommandContext& ctx,
                                         audit::AuditEvent& out_event,
                                         std::string& out_error) {
    if (id_.empty() && gid_.empty()) {
        out_error = "RemoveArgumentPackageCommand requires an id or gid.";
        return false;
    }
    if (!core::DeleteArgumentPackage(ctx.package, ctx.model, id_, gid_, out_error)) {
        return false;
    }
    FillIdentityPayload(out_event, "RemoveArgumentPackage", id_, gid_);
    return true;
}

bool RemoveArtifactPackageCommand::Apply(CommandContext& ctx,
                                         audit::AuditEvent& out_event,
                                         std::string& out_error) {
    if (id_.empty() && gid_.empty()) {
        out_error = "RemoveArtifactPackageCommand requires an id or gid.";
        return false;
    }
    if (!core::DeleteArtifactPackage(ctx.package, id_, gid_, out_error)) {
        return false;
    }
    FillIdentityPayload(out_event, "RemoveArtifactPackage", id_, gid_);
    return true;
}

} // namespace core::commands
