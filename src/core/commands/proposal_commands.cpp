#include "core/commands/proposal_commands.h"

#include "core/commands/library_bridge.h"
#include "core/reviews/review_proposal_patch_service.h"
#include "core/sacm_argument_sync.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

namespace core::commands {

bool PreflightProposalAgainstLibrary(const sacm_adapter::LibraryDocument& document,
                                     const reviews::ReviewProposal& proposal,
                                     const std::map<std::string, std::string>& predetermined_ids,
                                     parser::AssuranceCase& out_model,
                                     std::string& error) {
    error.clear();
    const sacm_adapter::SaveOutcome serialized = sacm_adapter::save_document(document);
    if (!serialized.ok) {
        error = "Could not clone the authoritative SACM document for promotion preflight: " +
                sacm_adapter::summarize_load_diagnostics(serialized.diagnostics);
        return false;
    }

    sacm_adapter::LibraryDocument rehearsal;
    if (!sacm_adapter::reload_document(rehearsal, serialized.xml)) {
        error = "Could not reload the authoritative SACM document for promotion preflight.";
        return false;
    }

    parser::AssuranceCase scratch_model;
    sacm::AssuranceCasePackage scratch_package;
    CommandContext context{scratch_model, scratch_package, &rehearsal};
    ApplyProposalCommand command(proposal, predetermined_ids);
    audit::AuditEvent unused_event;
    if (!command.Apply(context, unused_event, error))
        return false;

    out_model = sacm_adapter::project_case(rehearsal);
    return true;
}

bool ApplyProposalCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    // ApplyProposal is a compound patch on the parser model with no library
    // analog, so it flips through the bridge like the audit replay does: the same
    // patch runs on a scratch projection of the library, then the library is
    // reloaded from the mutated package (`RebuildSacmArgumentPackageFromParser`
    // mirrors the parser edits back into the package the bridge serializes). Both
    // sides run the identical legacy patch, so they converge by construction.
    const LibraryBridgeMutator mutate =
        [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
        reviews::ReviewProposalPatchService patch_service;
        // With predetermined ids this is the same replay path the audit log uses,
        // so a promoted draft element keeps the id it was shown under. Without
        // them it allocates, which is what an ordinary proposal has always done.
        reviews::ApplyProposalResult result =
            predetermined_ids_.empty() ? patch_service.ApplyProposal(proposal_, model)
                                       : patch_service.ApplyProposalWithIds(proposal_, model, predetermined_ids_);
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
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["proposal_id"] = proposal_.id;
    out_event.payload["proposal_json"] = reviews::SerializeReviewProposal(proposal_);
    nlohmann::ordered_json generated = nlohmann::ordered_json::object();
    for (const auto& [create_ref, id] : generated_ids_)
        generated[create_ref] = id;
    out_event.payload["generated_ids"] = std::move(generated);
    return true;
}

} // namespace core::commands
