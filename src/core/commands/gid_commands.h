#pragma once

#include "core/commands/command_bus.h"

#include <string>

// Audited command that assigns a SACM `gid` to an element that lacks one.
//
// Element gids are part of the canonical model hash (see
// `canonical_model_hash.cpp`, which appends `gid`), so an un-audited gid write
// makes the replayed model diverge from the live one -- a verify failure and, on
// non-destructive recovery, data loss. The inspector's confidence-save flow used
// to call `core::EnsureElementGid` directly on the loaded case; this routes that
// mutation through the same library-primary bridge chokepoint
// (`ApplyLibraryPrimaryOrLegacy`) every other audited mutation uses, so the gid
// assignment becomes a recorded, replayable transaction.
//
// Determinism: the gid is RANDOM (`GenerateUniqueElementGid`), so it is generated
// ONCE on first apply and captured into the payload; replay forces the recorded
// value via `core::SetElementGid` (same idea as AddAcp, whose deterministic id is
// likewise recorded and forced on replay).
namespace core::commands {

class EnsureElementGidCommand final : public ICommand {
public:
    explicit EnsureElementGidCommand(std::string element_id) : element_id_(std::move(element_id)) {}

    std::string Name() const override {
        return "SetElementGid";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    // The gid this command assigned, valid after a successful apply. The caller
    // (the confidence-save flow) writes it onto the live element so a same-frame
    // read (keying the confidence sidecar) sees it before the frame-boundary
    // re-derive re-projects the same value from the library.
    const std::string& GeneratedGid() const {
        return generated_gid_;
    }

private:
    std::string element_id_;
    std::string generated_gid_;
};

} // namespace core::commands
