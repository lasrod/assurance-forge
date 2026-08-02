#pragma once

#include "core/commands/command_bus.h"
#include "core/reviews/review_proposal.h"

#include <map>
#include <string>

// Audited command that applies a review proposal (a structured patch
// authored against the model) to the loaded SACM document. The command
// runs the patch service, mirrors the result into the SACM package, and
// records the full proposal plus the patch-service `generated_ids` map
// into the audit payload so a replayer can re-apply with identical
// identities via `ApplyProposalWithIds`.
namespace core::commands {

class ApplyProposalCommand final : public ICommand {
public:
    explicit ApplyProposalCommand(reviews::ReviewProposal proposal) : proposal_(std::move(proposal)) {}

    // Promotion variant: apply using identities the caller has already allocated.
    //
    // A draft workspace pins its `create_ref` -> element id map so a proposed
    // element keeps its name across every rebuild. Letting the command generate
    // ids afresh at promotion would renumber those elements at the moment they
    // become accepted argument -- the ids a reviewer just read, and that an agent
    // was told, would name something else. Element ids reach reports and
    // conversations, so they are not ours to shuffle silently.
    ApplyProposalCommand(reviews::ReviewProposal proposal, std::map<std::string, std::string> predetermined_ids)
        : proposal_(std::move(proposal)), predetermined_ids_(std::move(predetermined_ids)) {}

    std::string Name() const override {
        return "ApplyProposal";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const std::map<std::string, std::string>& GeneratedIds() const {
        return generated_ids_;
    }

private:
    reviews::ReviewProposal proposal_;
    std::map<std::string, std::string> generated_ids_;
    // Empty for an ordinary proposal, which allocates its own.
    std::map<std::string, std::string> predetermined_ids_;
};

} // namespace core::commands
