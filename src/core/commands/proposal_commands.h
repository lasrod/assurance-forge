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

    std::string Name() const override { return "ApplyProposal"; }
    bool        Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const std::map<std::string, std::string>& GeneratedIds() const { return generated_ids_; }

private:
    reviews::ReviewProposal            proposal_;
    std::map<std::string, std::string> generated_ids_;
};

} // namespace core::commands
