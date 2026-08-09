#pragma once

#include "core/commands/command_bus.h"
#include "core/terminology_package_service.h"

#include <string>
#include <vector>

// Audited commands that mutate terminology packages, categories, and
// terms. Each command captures the inputs and (on first execution) the
// generated identities, so a replayer can reproduce the same SACM state
// by re-running the recorded payload.
namespace core::commands {

// Create a new TerminologyPackage. The id/gid are assigned by the
// underlying mutator on first apply and captured into the audit payload;
// on replay they are forced via `CreateTerminologyPackageWithIds`.
class CreateTerminologyPackageCommand final : public ICommand {
public:
    CreateTerminologyPackageCommand(std::string name, std::string description)
        : name_(std::move(name)), description_(std::move(description)) {}

    std::string Name() const override {
        return "CreateTerminologyPackage";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const core::TerminologyPackageRef& GeneratedRef() const {
        return generated_ref_;
    }

private:
    std::string name_;
    std::string description_;
    core::TerminologyPackageRef generated_ref_;
};

// Update an existing TerminologyPackage's name and description.
class UpdateTerminologyPackageCommand final : public ICommand {
public:
    UpdateTerminologyPackageCommand(core::TerminologyPackageRef package_ref, std::string name, std::string description)
        : package_ref_(std::move(package_ref)), name_(std::move(name)), description_(std::move(description)) {}

    std::string Name() const override {
        return "UpdateTerminologyPackage";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

private:
    core::TerminologyPackageRef package_ref_;
    std::string name_;
    std::string description_;
};

// Create a new terminology category in a given TerminologyPackage. The
// category id/gid are assigned by the underlying mutator on first apply
// and captured into the audit payload; on replay they are forced via
// `CreateTerminologyCategoryWithIds`.
class CreateTerminologyCategoryCommand final : public ICommand {
public:
    CreateTerminologyCategoryCommand(core::TerminologyPackageRef package_ref, core::TerminologyCategoryDraft draft)
        : package_ref_(std::move(package_ref)), draft_(std::move(draft)) {}

    std::string Name() const override {
        return "CreateTerminologyCategory";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const core::TerminologyCategoryRef& GeneratedRef() const {
        return generated_ref_;
    }

private:
    core::TerminologyPackageRef package_ref_;
    core::TerminologyCategoryDraft draft_;
    core::TerminologyCategoryRef generated_ref_;
};

// Update an existing terminology category's draft fields.
class UpdateTerminologyCategoryCommand final : public ICommand {
public:
    UpdateTerminologyCategoryCommand(core::TerminologyPackageRef package_ref,
                                     core::TerminologyCategoryRef category_ref,
                                     core::TerminologyCategoryDraft draft)
        : package_ref_(std::move(package_ref)), category_ref_(std::move(category_ref)), draft_(std::move(draft)) {}

    std::string Name() const override {
        return "UpdateTerminologyCategory";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

private:
    core::TerminologyPackageRef package_ref_;
    core::TerminologyCategoryRef category_ref_;
    core::TerminologyCategoryDraft draft_;
};

// Delete an existing terminology category.
class DeleteTerminologyCategoryCommand final : public ICommand {
public:
    DeleteTerminologyCategoryCommand(core::TerminologyPackageRef package_ref, core::TerminologyCategoryRef category_ref)
        : package_ref_(std::move(package_ref)), category_ref_(std::move(category_ref)) {}

    std::string Name() const override {
        return "DeleteTerminologyCategory";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

private:
    core::TerminologyPackageRef package_ref_;
    core::TerminologyCategoryRef category_ref_;
};

// Create a new terminology term in a given TerminologyPackage. The
// term id/gid are assigned by the underlying mutator on first apply and
// captured into the audit payload; on replay they are forced via
// `CreateTerminologyTermWithIds`.
class CreateTerminologyTermCommand final : public ICommand {
public:
    CreateTerminologyTermCommand(core::TerminologyPackageRef package_ref, core::TerminologyTermDraft draft)
        : package_ref_(std::move(package_ref)), draft_(std::move(draft)) {}

    std::string Name() const override {
        return "CreateTerminologyTerm";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const core::TerminologyTermRef& GeneratedRef() const {
        return generated_ref_;
    }

private:
    core::TerminologyPackageRef package_ref_;
    core::TerminologyTermDraft draft_;
    core::TerminologyTermRef generated_ref_;
};

// Update an existing terminology term's draft fields.
class UpdateTerminologyTermCommand final : public ICommand {
public:
    UpdateTerminologyTermCommand(core::TerminologyPackageRef package_ref,
                                 core::TerminologyTermRef term_ref,
                                 core::TerminologyTermDraft draft)
        : package_ref_(std::move(package_ref)), term_ref_(std::move(term_ref)), draft_(std::move(draft)) {}

    std::string Name() const override {
        return "UpdateTerminologyTerm";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

private:
    core::TerminologyPackageRef package_ref_;
    core::TerminologyTermRef term_ref_;
    core::TerminologyTermDraft draft_;
};

// Delete an existing terminology term.
//
// `cascade_references` carries the user's answer to the confirmation "deleting
// this term also removes N elements that reference it -- go ahead?". A term used
// as a visible context is referenced from an ArgumentPackage, and the SACM
// library refuses to delete across a package boundary unless the caller opts in
// (SACM-CMD-007), so without this the delete is refused rather than silently
// taking the reference with it.
//
// It is a COMMAND INPUT, recorded in the audit payload, and not something the
// apply re-derives. A replay has no user to ask and must reproduce the decision
// that was actually made; deriving it from the model at replay time would let a
// later document state answer a question the user answered differently. Events
// recorded before this field existed replay as `false`, which is the behaviour
// they were written under.
class DeleteTerminologyTermCommand final : public ICommand {
public:
    DeleteTerminologyTermCommand(core::TerminologyPackageRef package_ref,
                                 core::TerminologyTermRef term_ref,
                                 bool cascade_references = false)
        : package_ref_(std::move(package_ref)),
          term_ref_(std::move(term_ref)),
          cascade_references_(cascade_references) {}

    std::string Name() const override {
        return "DeleteTerminologyTerm";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    // Every element the delete removed, the term included. Empty until applied.
    const std::vector<std::string>& RemovedIds() const {
        return removed_ids_;
    }

private:
    core::TerminologyPackageRef package_ref_;
    core::TerminologyTermRef term_ref_;
    bool cascade_references_ = false;
    std::vector<std::string> removed_ids_;
};

// Associate an existing terminology term with a claim/strategy/solution
// element by creating an ArtifactReference (if needed) and an
// AssertedContext linking them. On first apply, captures any
// newly-created entity identities into the payload so replay can
// reproduce them via `AssociateTerminologyTermWithElementWithIds`.
class AssociateTerminologyTermWithElementCommand final : public ICommand {
public:
    AssociateTerminologyTermWithElementCommand(std::string element_id,
                                               core::TerminologyPackageRef package_ref,
                                               core::TerminologyTermRef term_ref)
        : element_id_(std::move(element_id)), package_ref_(std::move(package_ref)), term_ref_(std::move(term_ref)) {}

    std::string Name() const override {
        return "AssociateTerminologyTermWithElement";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const core::TerminologyContextAssociationResult& Result() const {
        return result_;
    }

private:
    std::string element_id_;
    core::TerminologyPackageRef package_ref_;
    core::TerminologyTermRef term_ref_;
    core::TerminologyContextAssociationResult result_;
};

// Add an existing terminology term as a visible context for an element.
// Like the Associate command but produces a context flagged with the
// visible-terminology marker and also synchronises the parser model so
// replayed history sees the same projection.
class AddTerminologyTermAsVisibleContextCommand final : public ICommand {
public:
    AddTerminologyTermAsVisibleContextCommand(std::string element_id,
                                              core::TerminologyPackageRef package_ref,
                                              core::TerminologyTermRef term_ref)
        : element_id_(std::move(element_id)), package_ref_(std::move(package_ref)), term_ref_(std::move(term_ref)) {}

    std::string Name() const override {
        return "AddTerminologyTermAsVisibleContext";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    const core::TerminologyContextAssociationResult& Result() const {
        return result_;
    }

private:
    std::string element_id_;
    core::TerminologyPackageRef package_ref_;
    core::TerminologyTermRef term_ref_;
    core::TerminologyContextAssociationResult result_;
};

} // namespace core::commands
