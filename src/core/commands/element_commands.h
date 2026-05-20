#pragma once

#include "core/commands/command_bus.h"
#include "core/element_factory.h"

#include <string>

// Concrete commands that mutate elements in the assurance case. Each command
// is a small POD that captures the inputs, plus an `Apply` that calls the
// existing pure mutator helpers in `core/element_factory.h` and fills the
// `AuditEvent` payload with everything needed to replay the operation.
namespace core::commands {

// Helpers shared with replay: convert NewElementKind / RemoveMode to and
// from short stable string tokens used in event payloads.
std::string             NewElementKindToToken(NewElementKind kind);
bool                    NewElementKindFromToken(const std::string& token, NewElementKind& out);
std::string             RemoveModeToToken(RemoveMode mode);
bool                    RemoveModeFromToken(const std::string& token, RemoveMode& out);

// Add a new top-level Goal (root claim). The id is assigned by the underlying
// `core::AddTopGoal` mutator and captured into the event payload on success.
class CreateTopGoalCommand final : public ICommand {
public:
    std::string Name() const override { return "CreateTopGoal"; }
    bool        Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const std::string& GeneratedId() const { return generated_id_; }

private:
    std::string generated_id_;
};

// Add a new element of the given kind as a child of `parent_id`.
class CreateChildElementCommand final : public ICommand {
public:
    CreateChildElementCommand(std::string parent_id, NewElementKind kind)
        : parent_id_(std::move(parent_id)), kind_(kind) {}

    std::string Name() const override { return "CreateChildElement"; }
    bool        Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const std::string& GeneratedId() const { return generated_id_; }
    const std::string& GeneratedRelationshipId() const { return generated_relationship_id_; }

private:
    std::string    parent_id_;
    NewElementKind kind_;
    std::string    generated_id_;
    std::string    generated_relationship_id_;
};

// Remove an element (and optionally its descendants) from the case.
class RemoveElementCommand final : public ICommand {
public:
    RemoveElementCommand(std::string element_id, RemoveMode mode)
        : element_id_(std::move(element_id)), mode_(mode) {}

    std::string Name() const override { return "RemoveElement"; }
    bool        Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    std::size_t RemovedCount() const { return removed_count_; }

private:
    std::string element_id_;
    RemoveMode  mode_;
    std::size_t removed_count_ = 0;
};

} // namespace core::commands
