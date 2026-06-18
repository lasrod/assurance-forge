#pragma once

#include "core/commands/command_bus.h"
#include "core/pattern_model.h"

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

// Set the GSN pattern "uninstantiated" decorator on an element (ADR-0006). The
// new value is recorded explicitly so replay is deterministic.
class SetUninstantiatedCommand final : public ICommand {
public:
    SetUninstantiatedCommand(std::string element_id, bool value)
        : element_id_(std::move(element_id)), value_(value) {}

    std::string Name() const override { return "SetUninstantiated"; }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

private:
    std::string element_id_;
    bool value_;
};

// Set the "undeveloped" flag on a goal or strategy. Valid in both Argument and
// Pattern editing; in patterns it combines with the uninstantiated decorator.
class SetUndevelopedCommand final : public ICommand {
public:
    SetUndevelopedCommand(std::string element_id, bool value)
        : element_id_(std::move(element_id)), value_(value) {}

    std::string Name() const override { return "SetUndeveloped"; }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

private:
    std::string element_id_;
    bool value_;
};

// Set the pattern abstraction (none / optional / multiplicity + cardinality) on
// a SupportedBy / InContextOf relationship (ADR-0006). The underlying SACM
// relationship type is unchanged; only the namespaced tagged values move.
class SetRelationshipPatternCommand final : public ICommand {
public:
    SetRelationshipPatternCommand(std::string relationship_id, core::PatternRelationshipData data)
        : relationship_id_(std::move(relationship_id)), data_(std::move(data)) {}

    std::string Name() const override { return "SetRelationshipPattern"; }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

private:
    std::string relationship_id_;
    core::PatternRelationshipData data_;
};

// Wire-token mapping for the relationship pattern operator, shared by the
// command payload and the audit replayer.
std::string RelationOperatorToToken(core::PatternRelationOperator op);
bool RelationOperatorFromToken(const std::string& token, core::PatternRelationOperator& out);

} // namespace core::commands
