#pragma once

#include "core/audit/audit_event.h"
#include "core/audit/audit_manifest.h"
#include "core/audit/event_store.h"
#include "core/project_model.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

// Audited command bus (design §15). Every model mutation that should appear
// in the audit timeline flows through `CommandBus::Execute`. The bus owns the
// on-disk audit store (manifest + event log) for the currently loaded project
// and is responsible for:
//   - delegating the mutation to an `ICommand`,
//   - auto-saving the resulting SACM XML to the project's working file,
//   - hashing the post-state and appending a hash-chained transaction.
//
// The bus does NOT own the `parser::AssuranceCase` / `sacm::AssuranceCasePackage`
// — those live in `core::AppState` and are passed in by reference at execute
// time. This avoids fragile pointer lifetimes across project reloads.
//
// Phase 2 scope: per-command transactions (no batching). Writes are atomic
// (temp file + fsync + rename) and the audit log is appended *before* the
// SACM file is overwritten, so the log is the canonical record of intent.
// A tamper / consistency check is performed when the event store is
// reopened.
namespace sacm_adapter {
class LibraryDocument;
}

namespace core::commands {

class ICommand;

struct CommandContext {
    parser::AssuranceCase& model;
    sacm::AssuranceCasePackage& package;

    // Phase 9 Stage 5: the library-owned document (null when the file was
    // loaded through the legacy-parser fallback). A command that routes its
    // edit through a library operation mutates this and sets `library_synced`;
    // the bus re-derives it from the authoritative package for any command
    // that did not, so it never drifts. See src/sacm_adapter/document_edit.h.
    sacm_adapter::LibraryDocument* library_document = nullptr;
    bool library_synced = false;

    // Phase 2 slice 2b-1: the LIVE edit flip, inverted per command. A command
    // sets this when it mutated the LIBRARY natively (rather than the legacy
    // models), which makes the library the source of truth for that edit: the
    // bus then REBUILDS `model` + `package` from it via
    // `core::RebuildDerivedViewsFromLibrary` before serializing, hashing and
    // appending, so the legacy views are derived rather than independently
    // mutated.
    //
    // Deliberately separate from `library_synced` because the flip is
    // incremental: a command that has not been flipped (or that had no library
    // document to mutate) leaves this false and keeps the Stage 5 net, which
    // re-derives the library FROM the authoritative package. Exactly one of the
    // two directions runs per command.
    bool library_primary = false;

    // Kill switch for the library-primary live edit flip. When false, flipped
    // commands take the legacy-mutator path regardless of `library_document`, so
    // the derived views are mutated in place rather than rebuilt wholesale.
    //
    // The flip is LIVE in the interactive app. This used to say the app set it
    // false to dodge a GUI re-entrancy (a wholesale mid-frame rebuild of
    // `model`/`package` invalidating a panel iterating them); that re-entrancy
    // was fixed and the flip re-enabled, but the comment stayed, which made the
    // whole live edit path read as legacy-only to anyone reasoning from here.
    // Nothing under `src/app/` assigns this now -- see the note in
    // `app::commands::DispatchAuditedCommand`. It remains a kill switch: set it
    // false and flipped commands take the legacy-mutator path regardless of
    // `library_document`.
    bool allow_library_primary = true;
};

struct CommandResult {
    bool success = false;
    std::string error;
    std::string transaction_id;
    std::uint64_t transaction_sequence = 0;
    std::string raw_file_hash_after;
    std::string canonical_model_hash_after;
};

class CommandBus {
public:
    // Open the bus for an existing project. The manifest and event log must
    // already have been initialized by `audit::EnsureAuditStore` (called on
    // project open). `sacm_absolute_path` is the file the bus will auto-save
    // to after each successful command — it should match
    // `<project.rootPath>/<manifest.current_sacm>`.
    static std::unique_ptr<CommandBus>
    Open(AssuranceProject project, std::filesystem::path sacm_absolute_path, std::string& error);

    // Execute one command. On `Apply` failure or audit-log append failure
    // returns `{success=false, error}` with no audit-log mutation. Once the
    // transaction has been appended to the log it is considered committed:
    // any subsequent failure (atomic SACM write, manifest cache update) is
    // reported as `{success=true, error=<diagnostic>}` so callers do not
    // roll back UI state that has already been mirrored on disk. The
    // `transaction_id`, `transaction_sequence`, and post-state hashes are
    // populated whenever `success` is true, including the partial-failure
    // cases.
    CommandResult Execute(ICommand& command, CommandContext& ctx, const std::string& author);

    const audit::AuditManifest& Manifest() const {
        return manifest_;
    }
    const audit::EventStore& Store() const {
        return *store_;
    }
    const std::filesystem::path& SacmPath() const {
        return sacm_path_;
    }

private:
    CommandBus() = default;

    AssuranceProject project_;
    std::filesystem::path sacm_path_;
    audit::AuditManifest manifest_;
    std::unique_ptr<audit::EventStore> store_;
};

// Base class for every audited command. Implementations describe what
// happened (`Name`, `BuildEvent`) and how to mutate the model (`Apply`).
//
// Replay (Phase 3) re-applies events by reconstructing the corresponding
// `ICommand` from the event payload and calling its `Apply`, so commands
// must not regenerate non-deterministic identifiers on a second invocation
// when the payload already carries them.
class ICommand {
public:
    virtual ~ICommand() = default;

    // Short, stable identifier persisted as `AuditTransaction::command_name`.
    virtual std::string Name() const = 0;

    // Apply the mutation. On success, fills `out_event` with the semantic
    // payload and returns true. On failure, sets `out_error` and must leave
    // the model unchanged.
    virtual bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) = 0;
};

} // namespace core::commands
