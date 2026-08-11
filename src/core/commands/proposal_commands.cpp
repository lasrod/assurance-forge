#include "core/commands/proposal_commands.h"

#include "core/commands/library_bridge.h"
#include "core/reviews/review_proposal_patch_service.h"
#include "core/reviews/review_proposal_plan.h"
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

    // Library-primary via the scratch-compute pattern (the one slices 3d and 3e
    // use): run the SAME patch service on a scratch projection, diff what it
    // decided, and write that through the seams. The patch service stays the
    // single authority on what a proposal means -- which kind each operation
    // creates, how a `create_ref` resolves, what a removal cascades to -- so the
    // flip cannot re-decide any of it. This is NOT the bridge: the scratch is read
    // and discarded, and the document is never rebuilt from it.
    // Why the seams could not take this proposal, when they could not. A decline
    // used to be silent: the command fell back and nothing recorded what the
    // library would not express, so diagnosing one meant patching a probe in.
    std::string decline_reason;
    bool applied_natively = false;
    if (CanApplyLibraryPrimary(ctx)) {
        // The BEFORE state is projected from the document, not taken from
        // `ctx.model`. They are the same thing on the ordinary command path, but
        // not everywhere: `PreflightProposalAgainstLibrary` deliberately hands in
        // an empty model and lets the applied path derive one, so reading
        // `ctx.model` there diffed against nothing and the patch service could not
        // find the elements the proposal named. The document is the source of
        // truth; projecting it is what makes that true here too.
        const parser::AssuranceCase before = sacm_adapter::project_case(*ctx.library_document);
        parser::AssuranceCase scratch = before;
        reviews::ReviewProposalPatchService patch_service;
        reviews::ApplyProposalResult result =
            predetermined_ids_.empty() ? patch_service.ApplyProposal(proposal_, scratch)
                                       : patch_service.ApplyProposalWithIds(proposal_, scratch, predetermined_ids_);
        if (!result.success) {
            out_error = result.error;
            return false;
        }

        // Where a created element goes. The POD is flat, so the package comes from
        // the document: the one owning the proposal's anchor, falling back to the
        // first. A proposal with no anchor that creates an element in a
        // multi-package case is exactly the ambiguity the planner declines on.
        const std::string package_id =
            sacm_adapter::resolve_argument_package_id(*ctx.library_document, proposal_.anchor_element_id);
        const reviews::ProposalPlan plan = reviews::PlanProposalFromDiff(before, scratch, package_id);

        decline_reason = plan.unrepresentable_reason;
        if (plan.unrepresentable_reason.empty()) {
            generated_ids_ = result.generated_ids;
            // Set BEFORE the first write, for the reason MoveSubtreeCommand records:
            // a plan is several writes, a failure between them leaves the document
            // changed, and the bus only re-derives the live models when this flag
            // says the flip engaged. Safe here because the decision to fall back is
            // already made -- the planner reported every reason to decline above.
            ctx.library_primary = true;
            if (!reviews::ApplyProposalPlanToLibrary(*ctx.library_document, plan, out_error))
                return false;
            applied_natively = true;
        }
    }
    if (!applied_natively && !ApplyLegacyOrRefuse(ctx, mutate, out_error)) {
        if (!decline_reason.empty()) {
            out_error = "The SACM library could not express this proposal (" + decline_reason +
                        "), and the compatibility path failed too: " + out_error;
        }
        return false;
    }

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
