#pragma once

#include "core/commands/command_bus.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_model.h"

#include <string>

namespace core {

// Remove the argument package identified by (id, gid) from the SACM model
// AND scrub matching parser projections (so claims/reasonings that lived
// in the removed package do not linger as orphans in the GSN tree). If
// the removed package backed a separate confidence argument tree, clears
// the link on any ACP that referenced it so the GSN canvas surfaces an
// "incomplete" badge again.
//
// Returns false with `error` populated if no matching package is found.
bool DeleteArgumentPackage(sacm::AssuranceCasePackage& package,
                           parser::AssuranceCase& model,
                           const std::string& package_id,
                           const std::string& package_gid,
                           std::string& error);

// Remove the artifact package identified by (id, gid). Returns false with
// `error` populated if no matching package is found.
bool DeleteArtifactPackage(sacm::AssuranceCasePackage& package,
                           const std::string& package_id,
                           const std::string& package_gid,
                           std::string& error);

} // namespace core

namespace core::commands {

// Removes a terminology package by (id, gid) via `core::DeleteTerminologyPackage`.
class RemoveTerminologyPackageCommand : public ICommand {
public:
    RemoveTerminologyPackageCommand(std::string id, std::string gid) : id_(std::move(id)), gid_(std::move(gid)) {}

    std::string Name() const override {
        return "RemoveTerminologyPackage";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

private:
    std::string id_;
    std::string gid_;
};

// Removes an argument package by (id, gid) via `core::DeleteArgumentPackage`.
class RemoveArgumentPackageCommand : public ICommand {
public:
    RemoveArgumentPackageCommand(std::string id, std::string gid) : id_(std::move(id)), gid_(std::move(gid)) {}

    std::string Name() const override {
        return "RemoveArgumentPackage";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

private:
    std::string id_;
    std::string gid_;
};

// Removes an artifact package by (id, gid) via `core::DeleteArtifactPackage`.
class RemoveArtifactPackageCommand : public ICommand {
public:
    RemoveArtifactPackageCommand(std::string id, std::string gid) : id_(std::move(id)), gid_(std::move(gid)) {}

    std::string Name() const override {
        return "RemoveArtifactPackage";
    }
    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

private:
    std::string id_;
    std::string gid_;
};

} // namespace core::commands
