#include "core/audit/history_reconstruction.h"

#include "core/argument_package_projection.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_snapshot.h"
#include "core/audit/event_replayer.h"
#include "core/audit/event_store.h"
#include "core/derived_views.h"
#include "core/library_package_projection.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_parser.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

#include <filesystem>

namespace core::audit {

std::expected<ReconstructedState, std::string> ReconstructAtSequence(const AssuranceProject& project,
                                                                     std::uint64_t target_transaction_sequence,
                                                                     const std::string& argument_package_id,
                                                                     const std::string& argument_package_gid) {
    if (project.rootPath.empty())
        return std::unexpected("Project has no root path");

    const std::filesystem::path manifest_path = ManifestPath(project.rootPath);
    if (!std::filesystem::exists(manifest_path))
        return std::unexpected("Project has no audit store at " + manifest_path.string());

    // Phase 1b flip: reconstruct through the library (`ReplayToLibrary`). History
    // always replays from snapshot 0 -- NOT the trusted replay root -- so it can
    // reproduce INTERMEDIATE states up to `target_transaction_sequence`.
    const std::filesystem::path snapshot_path = SnapshotSacmPath(project.rootPath, kInitialSnapshotId);
    sacm_adapter::LoadOutcome snapshot = sacm_adapter::load_document(snapshot_path);
    if (!snapshot.ok || snapshot.document == nullptr) {
        const std::string diagnostics = sacm_adapter::summarize_load_diagnostics(snapshot.diagnostics);
        return std::unexpected("Failed to load snapshot through the library: " + snapshot_path.string() +
                               (diagnostics.empty() ? "" : " (" + diagnostics + ")"));
    }

    std::string store_error;
    auto store = EventStore::Open(project.rootPath, store_error);
    if (!store)
        return std::unexpected("Failed to open event store: " + store_error);

    auto replayed_document =
        Replayer::ReplayToLibrary(std::move(snapshot.document), store->Transactions(), target_transaction_sequence);
    if (!replayed_document)
        return std::unexpected(std::move(replayed_document.error()));

    // The document is the authority; the views are DERIVED from it, exactly as
    // `AppState::load_file` derives the live ones. They are not a hash input:
    // an undo that cannot take the library-primary path assigns them straight
    // over the live model and package, which the command bus then serializes to
    // the tracked file, and the history canvas renders from them. Projecting
    // through `core::project_library_package` -- the AUDIT projection, whose own
    // contract permits it to collapse packages and which never restores vendor
    // TaggedValues -- meant an undo wrote back a document stripped of every ACP
    // and every bare strategy's `strategyTarget`, with the confidence argument
    // package merged into the main one. The canonical hash cannot see that: it
    // re-projects through the tagless projection at hashing time, so the loss is
    // dropped on both sides by construction. Same defect, same projection, as the
    // one fixed in `core::commands::BridgeLegacyMutationToLibrary`; this site was
    // missed because it is reached through the audit path rather than the edit one.
    ReconstructedState reconstructed;
    reconstructed.document = std::move(*replayed_document);
    core::RebuildDerivedViewsFromLibrary(
        *reconstructed.document, reconstructed.views.model, reconstructed.views.package);

    if (!argument_package_id.empty() || !argument_package_gid.empty()) {
        const sacm::ArgumentPackage* arg_pkg =
            core::FindArgumentPackageByIdentity(reconstructed.views.package, argument_package_id, argument_package_gid);
        if (arg_pkg) {
            reconstructed.views.model =
                core::BuildArgumentPackageProjection(reconstructed.views.model, *arg_pkg, arg_pkg->name);
        } else {
            // Package no longer exists at this sequence (e.g. created later
            // than `target_transaction_sequence`). Return an empty
            // projection so the canvas renders nothing rather than the
            // full SACM.
            parser::AssuranceCase empty;
            empty.id = reconstructed.views.model.id;
            empty.name = argument_package_id.empty() ? argument_package_gid : argument_package_id;
            reconstructed.views.model = std::move(empty);
        }
    }

    return reconstructed;
}

} // namespace core::audit
