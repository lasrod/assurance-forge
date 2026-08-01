#pragma once

#include "core/project_model.h"
#include "sacm/sacm_model.h"

#include <filesystem>
#include <string>

// Legacy GSN-strategy encoding migration (Phase 9 Stage 7, slice 3).
//
// Before the single-inference encoding, the element factory recorded a strategy
// as a *bare* AssertedInference `{reasoning=strategy, target=goal, no source}`
// plus a *separate* inference `{target=strategy, source=sub-goal}` per sub-goal.
// The current factory (and library) use the standard single inference
// `{reasoning=strategy, target=goal, sources=[sub-goals]}` + a strategyTarget
// vendor tag. Replaying a legacy project's audit log through the new factory
// therefore reconstructs the single-inference form, which no longer matches the
// legacy bare-inference bytes on disk -- so the verifier would flag a false
// divergence on open.
//
// This migration converts an on-disk project to the single-inference encoding
// and promotes a *trusted replay baseline* at HEAD (a snapshot the verifier
// replays forward from), so the reconstructed state matches the migrated bytes
// by construction. It runs once on open; already-migrated projects are skipped.
namespace core::audit {

// True if `package` still uses the legacy bare-inference strategy encoding: a
// strategy (ArgumentReasoning) that participates in a sourceless AssertedInference
// AND does NOT yet carry a strategyTarget tag. The tag is the discriminator --
// the current factory leaves a sourceless inference behind when a strategy's last
// sub-goal is removed too, but always with the tag, so tagged strategies are
// already new-encoded and must not be re-migrated.
bool PackageHasLegacyStrategyEncoding(const sacm::AssuranceCasePackage& package);

// Rewrite `package`'s strategies from the legacy bare-inference encoding to the
// single-inference encoding in place: collapse each strategy's bare inference and
// its per-sub-goal inferences into one `{reasoning=strategy, target=goal,
// sources=[sub-goals]}` (a bare strategy with no sub-goals keeps only the
// strategyTarget tag and no inference), and add the strategyTarget tag. Idempotent
// -- a package with no legacy encoding is left untouched. Returns true if anything
// changed.
bool NormalizeStrategyEncoding(sacm::AssuranceCasePackage& package);

struct StrategyMigrationResult {
    bool migrated = false; // true when the on-disk file was rewritten
    std::string note;      // user-facing summary (empty when not migrated)
    std::string baseline_snapshot_id;
};

// If the project's on-disk SACM still uses the legacy strategy encoding, migrate
// it to single-inference, rewrite the file, and promote a trusted replay baseline
// at HEAD so open-verify converges. A project with no audit store, or already in
// the single-inference encoding, is a no-op (returns migrated=false). Returns
// false only on an unexpected I/O / serialization failure (with `error` set); a
// clean no-op returns true with migrated=false.
bool MigrateStrategyEncodingIfNeeded(const AssuranceProject& project,
                                     const std::filesystem::path& sacm_relative_path,
                                     StrategyMigrationResult& out_result,
                                     std::string& error);

} // namespace core::audit
