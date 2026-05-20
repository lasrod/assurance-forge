#include "core/audit/history_reconstruction.h"

#include "core/audit/audit_paths.h"
#include "core/audit/event_store.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_parser.h"

#include <filesystem>

namespace core::audit {

namespace {

std::expected<ReplayState, std::string> LoadSnapshot(const std::filesystem::path& project_root,
                                                     const std::string& snapshot_id) {
    const std::filesystem::path sacm_path = SnapshotSacmPath(project_root, snapshot_id);
    auto pkg = sacm::parse_sacm(sacm_path.string());
    if (!pkg)
        return std::unexpected("Failed to parse snapshot SACM at " + sacm_path.string() + ": " +
                               pkg.error());
    auto ac = parser::parse_sacm_xml(sacm_path.string());
    if (!ac)
        return std::unexpected("Failed to parse snapshot parser model at " + sacm_path.string() + ": " +
                               ac.error());
    return ReplayState{std::move(*ac), std::move(*pkg)};
}

} // namespace

std::expected<ReplayState, std::string> ReconstructAtSequence(const AssuranceProject& project,
                                                              std::uint64_t target_transaction_sequence) {
    if (project.rootPath.empty())
        return std::unexpected("Project has no root path");

    const std::filesystem::path manifest_path = ManifestPath(project.rootPath);
    if (!std::filesystem::exists(manifest_path))
        return std::unexpected("Project has no audit store at " + manifest_path.string());

    auto snapshot = LoadSnapshot(project.rootPath, kInitialSnapshotId);
    if (!snapshot)
        return std::unexpected(std::move(snapshot.error()));

    std::string store_error;
    auto store = EventStore::Open(project.rootPath, store_error);
    if (!store)
        return std::unexpected("Failed to open event store: " + store_error);

    return Replayer::ReplayFrom(snapshot->model, snapshot->package, store->Transactions(),
                                target_transaction_sequence);
}

} // namespace core::audit
