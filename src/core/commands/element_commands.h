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
std::string             ChallengeSourceTypeToToken(ChallengeSourceType type);
bool                    ChallengeSourceTypeFromToken(const std::string& token, ChallengeSourceType& out);
std::string             ArgumentTargetKindToToken(ArgumentTarget::Kind kind);
bool                    ArgumentTargetKindFromToken(const std::string& token, ArgumentTarget::Kind& out);

// Add a new top-level Goal (root claim). The id is assigned by the underlying
// `core::AddTopGoal` mutator and captured into the event payload on success.
// `target_package_id` selects which argument package receives the goal (the
// active canvas's package, e.g. a pattern); empty routes to the first package.
class CreateTopGoalCommand final : public ICommand {
public:
    explicit CreateTopGoalCommand(std::string target_package_id = {})
        : target_package_id_(std::move(target_package_id)) {}

    std::string Name() const override { return "CreateTopGoal"; }
    bool        Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const std::string& GeneratedId() const { return generated_id_; }

private:
    std::string target_package_id_;
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

// Create a GSN v3 dialectic challenge (counter argument / counter evidence)
// against an element or relationship target. Creates the counter element plus a
// counter relationship (isCounter=true) via `core::AddChallenge`, capturing the
// generated ids for deterministic replay.
class CreateChallengeCommand final : public ICommand {
public:
    CreateChallengeCommand(ArgumentTarget target, ChallengeSourceType source_type)
        : target_(std::move(target)), source_type_(source_type) {}

    std::string Name() const override { return "CreateChallenge"; }
    bool        Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const std::string& GeneratedId() const { return generated_id_; }
    const std::string& GeneratedRelationshipId() const { return generated_relationship_id_; }

private:
    ArgumentTarget      target_;
    ChallengeSourceType source_type_;
    std::string         generated_id_;
    std::string         generated_relationship_id_;
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

// Update one localized text field (name / description / content) on an
// element. One command per completed edit session — keystroke-level events
// are NOT committed; the UI dispatches this once when focus leaves the
// field or a forced flush hook fires (save / close / snapshot / verify /
// exit). The previous value is captured into the audit event so a replayer
// can both apply and visualize the change without re-reading prior state.
class UpdateElementTextCommand final : public ICommand {
public:
    UpdateElementTextCommand(std::string element_id,
                             ElementTextField field,
                             std::string language,
                             std::string new_value)
        : element_id_(std::move(element_id)),
          field_(field),
          language_(std::move(language)),
          new_value_(std::move(new_value)) {}

    std::string Name() const override { return "UpdateElementText"; }
    bool        Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const std::string& OldValue() const { return old_value_; }
    bool               WasNoOp() const { return was_no_op_; }

private:
    std::string      element_id_;
    ElementTextField field_;
    std::string      language_;
    std::string      new_value_;
    std::string      old_value_;
    bool             was_no_op_ = false;
};

} // namespace core::commands
