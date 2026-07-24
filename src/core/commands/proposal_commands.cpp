#include "core/commands/proposal_commands.h"

#include "core/commands/library_bridge.h"
#include "core/reviews/review_proposal_patch_service.h"
#include "core/sacm_argument_sync.h"

namespace core::commands {

bool ApplyProposalCommand::Apply(CommandContext&   ctx,
                                 audit::AuditEvent& out_event,
                                 std::string&       out_error) {
    // ApplyProposal is a compound patch on the parser model with no library
    // analog, so it flips through the bridge like the audit replay does: the same
    // patch runs on a scratch projection of the library, then the library is
    // reloaded from the mutated package (`RebuildSacmArgumentPackageFromParser`
    // mirrors the parser edits back into the package the bridge serializes). Both
    // sides run the identical legacy patch, so they converge by construction.
    const LibraryBridgeMutator mutate = [&](parser::AssuranceCase&         model,
                                            sacm::AssuranceCasePackage& package, std::string& err) -> bool {
        reviews::ReviewProposalPatchService patch_service;
        reviews::ApplyProposalResult        result = patch_service.ApplyProposal(proposal_, model);
        if (!result.success) {
            err = result.error;
            return false;
        }
        generated_ids_ = result.generated_ids;
        // Mirror the mutated parser model into the SACM package so the serialized
        // bytes reflect the proposal's changes (not the pre-apply state).
        core::RebuildSacmArgumentPackageFromParser(model, package);
        return true;
    };
    if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
        return false;

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
