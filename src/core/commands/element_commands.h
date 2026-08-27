#pragma once

#include "core/commands/command_bus.h"
#include "core/element_factory.h"
#include "core/evidence_attributes.h"

#include <string>

// Concrete commands that mutate elements in the assurance case. Each command
// is a small POD that captures the inputs, plus an `Apply` that calls the
// existing pure mutator helpers in `core/element_factory.h` and fills the
// `AuditEvent` payload with everything needed to replay the operation.
namespace core::commands {

// Helpers shared with replay: convert NewElementKind / RemoveMode to and
// from short stable string tokens used in event payloads.
std::string NewElementKindToToken(NewElementKind kind);
bool NewElementKindFromToken(const std::string& token, NewElementKind& out);
std::string RemoveModeToToken(RemoveMode mode);
bool RemoveModeFromToken(const std::string& token, RemoveMode& out);
std::string ChallengeSourceTypeToToken(ChallengeSourceType type);
bool ChallengeSourceTypeFromToken(const std::string& token, ChallengeSourceType& out);
std::string ArgumentTargetKindToToken(ArgumentTarget::Kind kind);
bool ArgumentTargetKindFromToken(const std::string& token, ArgumentTarget::Kind& out);

// Add a new top-level Goal (root claim). The id is assigned by the underlying
// `core::AddTopGoal` mutator and captured into the event payload on success.
class CreateTopGoalCommand final : public ICommand {
public:
    std::string Name() const override {
        return "CreateTopGoal";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const std::string& GeneratedId() const {
        return generated_id_;
    }

private:
    std::string generated_id_;
};

// Add a new element of the given kind as a child of `parent_id`.
class CreateChildElementCommand final : public ICommand {
public:
    CreateChildElementCommand(std::string parent_id, NewElementKind kind)
        : parent_id_(std::move(parent_id)), kind_(kind) {}

    std::string Name() const override {
        return "CreateChildElement";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const std::string& GeneratedId() const {
        return generated_id_;
    }
    const std::string& GeneratedRelationshipId() const {
        return generated_relationship_id_;
    }

private:
    std::string parent_id_;
    NewElementKind kind_;
    std::string generated_id_;
    std::string generated_relationship_id_;
};

// Create a GSN v3 dialectic challenge (counter argument / counter evidence)
// against an element or relationship target. Creates the counter element plus a
// counter relationship (isCounter=true) via `core::AddChallenge`, capturing the
// generated ids for deterministic replay.
class CreateChallengeCommand final : public ICommand {
public:
    CreateChallengeCommand(ArgumentTarget target, ChallengeSourceType source_type)
        : target_(std::move(target)), source_type_(source_type) {}

    std::string Name() const override {
        return "CreateChallenge";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const std::string& GeneratedId() const {
        return generated_id_;
    }
    const std::string& GeneratedRelationshipId() const {
        return generated_relationship_id_;
    }

private:
    ArgumentTarget target_;
    ChallengeSourceType source_type_;
    std::string generated_id_;
    std::string generated_relationship_id_;
};

// Remove an element (and optionally its descendants) from the case.
class RemoveElementCommand final : public ICommand {
public:
    RemoveElementCommand(std::string element_id, RemoveMode mode) : element_id_(std::move(element_id)), mode_(mode) {}

    std::string Name() const override {
        return "RemoveElement";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    std::size_t RemovedCount() const {
        return removed_count_;
    }

private:
    std::string element_id_;
    RemoveMode mode_;
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

    std::string Name() const override {
        return "UpdateElementText";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const std::string& OldValue() const {
        return old_value_;
    }
    bool WasNoOp() const {
        return was_no_op_;
    }

private:
    std::string element_id_;
    ElementTextField field_;
    std::string language_;
    std::string new_value_;
    std::string old_value_;
    bool was_no_op_ = false;
};

// Change the user-facing GSN notation identifier without renaming the SACM
// element or rewriting graph references. The independent identifier is stored
// in a vendor TaggedValue and the edit is replayable like every other mutation.
class UpdateGsnIdentifierCommand final : public ICommand {
public:
    UpdateGsnIdentifierCommand(std::string element_id, std::string new_identifier)
        : element_id_(std::move(element_id)), new_identifier_(std::move(new_identifier)) {}

    std::string Name() const override {
        return "UpdateGsnIdentifier";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const std::string& OldIdentifier() const {
        return old_identifier_;
    }
    bool WasNoOp() const {
        return was_no_op_;
    }

private:
    std::string element_id_;
    std::string new_identifier_;
    std::string old_identifier_;
    bool was_no_op_ = false;
};

// Record where a piece of evidence is: the location of the Resource an
// ArtifactReference cites, creating the Resource when the reference cites none
// (see `sacm_adapter::apply_set_evidence_location`). Library-only: the legacy
// models have no field for a Resource's location, so without a document the
// command refuses rather than recording an event replay could not reproduce.
// The previous location is captured so the history can show the change.
// One write to one of the evidence register's SACM-backed columns
// (core::EvidenceAttribute), recorded on the Artifact the reference cites.
struct EvidenceAttributeWrite {
    std::string element_id;
    EvidenceAttribute attribute = EvidenceAttribute::Owner;
    std::string value;
};

// Record one register column for one piece of evidence. Library-only, like
// SetEvidenceLocationCommand, and for the same reason. The previous value is
// captured so the history can show the change.
class SetEvidenceAttributeCommand final : public ICommand {
public:
    SetEvidenceAttributeCommand(std::string element_id, EvidenceAttribute attribute, std::string value)
        : element_id_(std::move(element_id)), attribute_(attribute), value_(std::move(value)) {}

    std::string Name() const override {
        return "SetEvidenceAttribute";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const std::string& OldValue() const {
        return old_value_;
    }
    bool WasNoOp() const {
        return was_no_op_;
    }

private:
    std::string element_id_;
    EvidenceAttribute attribute_;
    std::string value_;
    std::string old_value_;
    bool was_no_op_ = false;
};

// Move assessments the register held in the project file into the SACM
// document, as one transaction: every write lands or none is recorded. A
// write naming an element that is not evidence fails the whole import, so a
// stale entry cannot be half-migrated.
class ImportEvidenceAssessmentsCommand final : public ICommand {
public:
    explicit ImportEvidenceAssessmentsCommand(std::vector<EvidenceAttributeWrite> writes)
        : writes_(std::move(writes)) {}

    std::string Name() const override {
        return "ImportEvidenceAssessments";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    std::size_t AppliedCount() const {
        return applied_count_;
    }

private:
    std::vector<EvidenceAttributeWrite> writes_;
    std::size_t applied_count_ = 0;
};

class SetEvidenceLocationCommand final : public ICommand {
public:
    SetEvidenceLocationCommand(std::string element_id, std::string location)
        : element_id_(std::move(element_id)), location_(std::move(location)) {}

    std::string Name() const override {
        return "SetEvidenceLocation";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const std::string& OldLocation() const {
        return old_location_;
    }
    bool WasNoOp() const {
        return was_no_op_;
    }

private:
    std::string element_id_;
    std::string location_;
    std::string old_location_;
    bool was_no_op_ = false;
};

// Remove one relationship, leaving both endpoints in place. Distinct from
// `RemoveElementCommand`, whose plan is node-shaped: it walks the GSN tree,
// excludes relationship ids and reparents structural children, none of which is
// meaningful for an edge.
class RemoveRelationshipCommand final : public ICommand {
public:
    explicit RemoveRelationshipCommand(std::string relationship_id) : relationship_id_(std::move(relationship_id)) {}

    std::string Name() const override {
        return "RemoveRelationship";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

private:
    std::string relationship_id_;
};

// Drop one reference from a relationship -- the repair for an endpoint naming an
// element the case does not contain. Records whether scrubbing left the
// relationship structurally invalid and therefore removed it too, so a replay
// reproduces the same outcome without re-deciding it.
class DropRelationshipReferenceCommand final : public ICommand {
public:
    DropRelationshipReferenceCommand(std::string relationship_id, std::string reference)
        : relationship_id_(std::move(relationship_id)), reference_(std::move(reference)) {}

    std::string Name() const override {
        return "DropRelationshipReference";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    bool RemovedRelationship() const {
        return removed_relationship_;
    }

private:
    std::string relationship_id_;
    std::string reference_;
    bool removed_relationship_ = false;
};

// Re-wire a Strategy from an inference's source list into its reasoning slot.
class MoveStrategyToReasoningCommand final : public ICommand {
public:
    MoveStrategyToReasoningCommand(std::string relationship_id, std::string strategy_id)
        : relationship_id_(std::move(relationship_id)), strategy_id_(std::move(strategy_id)) {}

    std::string Name() const override {
        return "MoveStrategyToReasoning";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

private:
    std::string relationship_id_;
    std::string strategy_id_;
};

// Set or clear the GSN undeveloped decorator. The previous value is recorded so
// the event both applies and describes the change.
class SetElementUndevelopedCommand final : public ICommand {
public:
    SetElementUndevelopedCommand(std::string element_id, bool undeveloped)
        : element_id_(std::move(element_id)), undeveloped_(undeveloped) {}

    std::string Name() const override {
        return "SetElementUndeveloped";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    bool WasNoOp() const {
        return was_no_op_;
    }

private:
    std::string element_id_;
    bool undeveloped_ = false;
    bool old_value_ = false;
    bool was_no_op_ = false;
};

} // namespace core::commands
