#include "core/commands/proposal_commands.h"

#include "core/reviews/review_proposal_patch_service.h"
#include "core/sacm_argument_sync.h"

namespace core::commands {

bool ApplyProposalCommand::Apply(CommandContext&   ctx,
                                 audit::AuditEvent& out_event,
                                 std::string&       out_error) {
    reviews::ReviewProposalPatchService patch_service;
    reviews::ApplyProposalResult        result = patch_service.ApplyProposal(proposal_, ctx.model);
    if (!result.success) {
        out_error = result.error;
        return false;
    }
    generated_ids_ = result.generated_ids;

    // Mirror the mutated parser model into the SACM package so the bus's
    // subsequent auto-serialize writes a SACM file that reflects the
    // proposal's changes (not the pre-apply state).
    core::RebuildSacmArgumentPackageFromParser(ctx.model, ctx.package);

    out_event.event_type = "ApplyProposal";
    out_event.payload    = nlohmann::ordered_json::object();
    out_event.payload["proposal_id"]   = proposal_.id;
    out_event.payload["proposal_json"] = reviews::SerializeReviewProposal(proposal_);
    nlohmann::ordered_json generated   = nlohmann::ordered_json::object();
    for (const auto& [create_ref, id] : generated_ids_)
        generated[create_ref] = id;
    out_event.payload["generated_ids"] = std::move(generated);
    return true;
}

} // namespace core::commands
