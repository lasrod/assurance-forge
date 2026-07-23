#include "core/audit/strategy_migration.h"

#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_snapshot.h"
#include "core/library_package_projection.h"
#include "core/project_file_io.h"
#include "core/sha256.h"
#include "core/time_utils.h"
#include "sacm/sacm_serializer.h"
#include "sacm_adapter/gsn_role_tag.h"
#include "sacm_adapter/library_load.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace core::audit {

namespace {

bool ReasoningHasStrategyTargetTag(const sacm::ArgumentReasoning& reasoning) {
    for (const sacm::TaggedValue& tag : reasoning.taggedValues) {
        if (tag.key == sacm_adapter::kGsnStrategyTargetTagKey)
            return true;
    }
    return false;
}

// True when the strategy participates in a legacy bare inference
// `{reasoning=strategy, no source}`.
bool HasBareInference(const sacm::ArgumentPackage& ap, const std::string& strategy_id) {
    for (const sacm::AssertedInference& inf : ap.assertedInferences) {
        if (inf.reasoning == strategy_id && inf.sources.empty())
            return true;
    }
    return false;
}

// Write a trusted-baseline snapshot at `transaction_sequence` from explicit bytes
// under a migration-specific id (so it never collides with the initial or a user
// snapshot). Idempotent: a retried migration overwrites its own snapshot.
bool WriteMigrationBaselineSnapshot(const std::filesystem::path& project_root,
                                    const std::string& sacm_bytes,
                                    std::uint64_t transaction_sequence,
                                    std::uint64_t event_sequence,
                                    const std::string& raw_hash,
                                    const std::string& canonical_hash,
                                    std::string& out_snapshot_id,
                                    std::string& error) {
    char id_buf[48];
    std::snprintf(id_buf, sizeof(id_buf), "snapshot_migration_%06llu",
                  static_cast<unsigned long long>(transaction_sequence));
    const std::string snapshot_id = id_buf;

    const std::filesystem::path snapshot_dir = SnapshotDir(project_root, snapshot_id);
    std::error_code ec;
    if (!std::filesystem::create_directories(snapshot_dir, ec) && ec) {
        error = "Could not create migration snapshot directory: " + ec.message();
        return false;
    }
    if (auto write = WriteTextFile(SnapshotSacmPath(project_root, snapshot_id), sacm_bytes); !write) {
        error = std::move(write.error());
        return false;
    }

    SnapshotMetadata metadata;
    metadata.snapshot_schema_version = kSnapshotSchemaVersion;
    metadata.snapshot_id = snapshot_id;
    metadata.created_at = NowUtcString();
    metadata.created_by = "system";
    metadata.reason = "strategy-encoding migration (single-inference)";
    metadata.transaction_sequence = transaction_sequence;
    metadata.event_sequence = event_sequence;
    metadata.raw_file_hash = raw_hash;
    metadata.canonical_model_hash = canonical_hash;
    if (!WriteSnapshotMetadata(project_root, metadata, error))
        return false;

    out_snapshot_id = snapshot_id;
    return true;
}

} // namespace

bool PackageHasLegacyStrategyEncoding(const sacm::AssuranceCasePackage& package) {
    for (const sacm::ArgumentPackage& ap : package.argumentPackages) {
        for (const sacm::ArgumentReasoning& strategy : ap.argumentReasonings) {
            if (ReasoningHasStrategyTargetTag(strategy))
                continue;  // already single-inference encoded
            if (HasBareInference(ap, strategy.id))
                return true;
        }
    }
    return false;
}

bool NormalizeStrategyEncoding(sacm::AssuranceCasePackage& package) {
    bool changed = false;
    for (sacm::ArgumentPackage& ap : package.argumentPackages) {
        for (sacm::ArgumentReasoning& strategy : ap.argumentReasonings) {
            if (ReasoningHasStrategyTargetTag(strategy))
                continue;

            // The legacy bare inference {reasoning=strategy, no source}.
            const sacm::AssertedInference* bare = nullptr;
            for (const sacm::AssertedInference& inf : ap.assertedInferences) {
                if (inf.reasoning == strategy.id && inf.sources.empty()) {
                    bare = &inf;
                    break;
                }
            }
            if (bare == nullptr)
                continue;  // not the legacy encoding

            const std::string bare_id = bare->id;
            const std::string target_goal = bare->targets.empty() ? std::string() : bare->targets.front();

            // Collect the legacy per-sub-goal inferences {target=strategy, no
            // reasoning} and their sources; mark them and the bare inference for
            // removal.
            std::vector<std::string> subgoals;
            std::vector<std::string> remove_ids;
            remove_ids.push_back(bare_id);
            for (const sacm::AssertedInference& inf : ap.assertedInferences) {
                if (!inf.reasoning.empty())
                    continue;
                if (std::find(inf.targets.begin(), inf.targets.end(), strategy.id) == inf.targets.end())
                    continue;
                for (const std::string& source : inf.sources)
                    subgoals.push_back(source);
                remove_ids.push_back(inf.id);
            }

            std::erase_if(ap.assertedInferences, [&](const sacm::AssertedInference& inf) {
                return std::find(remove_ids.begin(), remove_ids.end(), inf.id) != remove_ids.end();
            });

            if (!target_goal.empty()) {
                strategy.taggedValues.push_back(sacm::TaggedValue{.id = strategy.id + "__strategyTarget",
                                                                  .key = sacm_adapter::kGsnStrategyTargetTagKey,
                                                                  .value = target_goal});
            }

            // Materialize the single inference when the strategy has sub-goals; a
            // bare strategy (no sub-goals) keeps only the tag -- its placement is
            // a render-only placeholder synthesized on load.
            if (!subgoals.empty() && !target_goal.empty()) {
                sacm::AssertedInference single;
                single.id = bare_id;  // reuse the strategy's original inference id
                single.reasoning = strategy.id;
                single.targets.push_back(target_goal);
                single.sources = std::move(subgoals);
                ap.assertedInferences.push_back(std::move(single));
            }
            changed = true;
        }
    }
    return changed;
}

bool MigrateStrategyEncodingIfNeeded(const AssuranceProject& project,
                                     const std::filesystem::path& sacm_relative_path,
                                     StrategyMigrationResult& out_result,
                                     std::string& error) {
    out_result = StrategyMigrationResult{};

    if (project.rootPath.empty() || sacm_relative_path.empty())
        return true;  // nothing to migrate

    // Migration only matters for an audited project (verify replays against the
    // manifest); a bare file has no divergence to prevent.
    if (!std::filesystem::exists(ManifestPath(project.rootPath)))
        return true;

    AuditManifest manifest;
    if (!ReadAuditManifest(project.rootPath, manifest, error))
        return false;

    // Only the manifest's tracked current SACM is audited: verify and the trusted
    // baseline are keyed to it. Skip unless the file the caller opened is that one,
    // so we never rewrite (or take a baseline of) a SACM this audit store does not
    // cover -- e.g. a second argument file or a stale manifest.
    if (manifest.current_sacm.empty() || sacm_relative_path.generic_string() != manifest.current_sacm)
        return true;

    // With no transactions, replay reproduces snapshot 0 verbatim (nothing is
    // re-encoded through the current factory), so the legacy bytes still match --
    // there is no divergence to heal and nothing to migrate yet.
    if (manifest.latest_transaction_sequence == 0)
        return true;

    const std::filesystem::path sacm_abs = project.rootPath / sacm_relative_path;
    if (!std::filesystem::exists(sacm_abs))
        return true;

    // Read the on-disk SACM through the library (it is library XMI since Stage 6,
    // which the legacy parser reads near-empty), preserving vendor tags.
    sacm_adapter::LoadOutcome outcome = sacm_adapter::load_document(sacm_abs);
    if (!outcome.ok || outcome.document == nullptr)
        return true;  // unreadable by the library: leave it to the load-time fallback
    sacm::AssuranceCasePackage package = core::project_library_package_with_tags(*outcome.document);

    if (!PackageHasLegacyStrategyEncoding(package))
        return true;  // already single-inference: nothing to migrate

    NormalizeStrategyEncoding(package);

    std::optional<std::string> migrated_xml = core::library_xmi_from_package(package);
    if (!migrated_xml) {
        error = "Strategy migration could not serialize the normalized package to library XMI.";
        return false;
    }

    // Rewrite the on-disk SACM with the migrated bytes.
    if (auto write = core::WriteTextFileAtomic(sacm_abs, *migrated_xml); !write) {
        error = "Strategy migration could not write the migrated SACM: " + write.error();
        return false;
    }

    const std::string raw_hash = Sha256::HexDigest(*migrated_xml);
    std::string canonical_hash;
    if (auto canonical = core::library_canonical_hash_from_xml(*migrated_xml))
        canonical_hash = *canonical;

    // Promote a trusted replay baseline at HEAD from the migrated state, so
    // open-verify replays forward from it (no pre-migration events re-run) and
    // matches the on-disk bytes by construction. The baseline is written as
    // *legacy* XML because snapshots are loaded by the legacy parser
    // (`LoadSnapshot` uses `sacm::parse_sacm`, which reads library XMI near-empty
    // -- a latent gap Phase 1b closes); its library-canonical hash still matches
    // the library-XMI on-disk file (the same convergence the initial snapshot
    // relies on today).
    const std::string baseline_xml = sacm::serialize_sacm(package);
    const std::string baseline_raw_hash = Sha256::HexDigest(baseline_xml);
    std::string baseline_snapshot_id;
    if (!WriteMigrationBaselineSnapshot(project.rootPath, baseline_xml,
                                        manifest.latest_transaction_sequence,
                                        manifest.latest_event_sequence, baseline_raw_hash, canonical_hash,
                                        baseline_snapshot_id, error)) {
        error = "Strategy migration wrote the migrated SACM but could not record the trusted "
                "baseline: " + error;
        return false;
    }

    manifest.replay_root_snapshot_id = baseline_snapshot_id;
    manifest.last_known_raw_file_hash = raw_hash;
    if (!canonical_hash.empty())
        manifest.last_known_canonical_model_hash = canonical_hash;
    if (!WriteAuditManifest(project.rootPath, manifest, error)) {
        error = "Strategy migration wrote the migrated SACM and baseline but could not update the "
                "manifest: " + error;
        return false;
    }

    out_result.migrated = true;
    out_result.baseline_snapshot_id = baseline_snapshot_id;
    out_result.note = "Migrated legacy GSN strategy encoding to the standard single-inference form "
                      "and recorded a trusted audit baseline.";
    return true;
}

} // namespace core::audit
