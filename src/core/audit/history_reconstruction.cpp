#include "core/audit/history_reconstruction.h"

#include "core/argument_package_projection.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_snapshot.h"
#include "core/audit/event_store.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_parser.h"

#include <filesystem>

namespace core::audit {

namespace {

std::expected<ReplayState, std::string> LoadSnapshot(const std::filesystem::path& project_root,
                                                     const std::string& snapshot_id) {
    ReplayState state;
    std::string error;
    if (!LoadSnapshotModels(SnapshotSacmPath(project_root, snapshot_id), state.model, state.package, error))
        return std::unexpected(std::move(error));
    return state;
}

} // namespace

std::expected<ReplayState, std::string> ReconstructAtSequence(const AssuranceProject& project,
                                                              std::uint64_t target_transaction_sequence,
                                                              const std::string& argument_package_id,
                                                              const std::string& argument_package_gid) {
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

    auto replayed = Replayer::ReplayFrom(snapshot->model, snapshot->package, store->Transactions(),
                                         target_transaction_sequence);
    if (!replayed)
        return replayed;

    if (!argument_package_id.empty() || !argument_package_gid.empty()) {
        const sacm::ArgumentPackage* arg_pkg = core::FindArgumentPackageByIdentity(
            replayed->package, argument_package_id, argument_package_gid);
        if (arg_pkg) {
            replayed->model = core::BuildArgumentPackageProjection(replayed->model, *arg_pkg, arg_pkg->name);
        } else {
            // Package no longer exists at this sequence (e.g. created later
            // than `target_transaction_sequence`). Return an empty
            // projection so the canvas renders nothing rather than the
            // full SACM.
            parser::AssuranceCase empty;
            empty.id = replayed->model.id;
            empty.name = argument_package_id.empty() ? argument_package_gid : argument_package_id;
            replayed->model = std::move(empty);
        }
    }

    return replayed;
}

} // namespace core::audit
