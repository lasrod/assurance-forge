#pragma once

#include "core/commands/command_bus.h"

#include <string>

// Audited commands that mutate GSN argument patterns (ADR-0006). Each command
// captures its inputs and, on first execution, the generated identities, so a
// replayer can reproduce the same SACM state by re-running the recorded
// payload.
namespace core::commands {

// Create a new abstract ArgumentPackage classified as a GSN pattern. The
// package id/gid are assigned by `core::CreatePatternPackage` on first apply and
// captured into the audit payload; on replay they are forced via
// `core::CreatePatternPackageWithIds`.
class CreatePatternCommand final : public ICommand {
public:
    CreatePatternCommand(std::string name, std::string identifier, std::string description)
        : name_(std::move(name)), identifier_(std::move(identifier)), description_(std::move(description)) {}

    std::string Name() const override { return "CreatePattern"; }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const std::string& GeneratedId() const { return generated_id_; }
    const std::string& GeneratedGid() const { return generated_gid_; }

private:
    std::string name_;
    std::string identifier_;
    std::string description_;
    std::string generated_id_;
    std::string generated_gid_;
};

} // namespace core::commands
