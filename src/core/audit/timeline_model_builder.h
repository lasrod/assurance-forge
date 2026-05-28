#pragma once

#include "core/audit/audit_baseline.h"
#include "core/audit/audit_snapshot.h"
#include "core/audit/audit_transaction.h"
#include "core/audit/timeline_types.h"

#include <vector>

namespace core::audit {

// Pure function: assemble a `TimelineModel` from already-loaded audit data.
// The builder owns sorting and label formatting. It does not touch the
// file system, ImGui, or the canvas — callers supply the inputs and the
// caller's scope (e.g. per-argument-package) is already applied upstream.
//
// Output is unified — every kind is always emitted: baselines + snapshots
// + one change marker per transaction + a synthetic `Now` marker. Points
// are ordered by ascending `transaction_sequence`, with kind priority
// `InitialSnapshot < Baseline < Snapshot < Change` breaking ties at a
// shared sequence and stable id breaking remaining ties. `Now` is appended
// last regardless of its enum value.
//
// `TimelineQuery::initial_snapshot_id` (sourced from
// `AuditManifest::initial_snapshot_id`) flags the matching snapshot as
// `InitialSnapshot` and labels it "S0"; regular snapshots get "S1", "S2",
// ….
TimelineModel BuildTimelineModel(const std::vector<AuditTransaction>& transactions,
                                 const std::vector<BaselineMetadata>& baselines,
                                 const std::vector<SnapshotMetadata>& snapshots,
                                 const TimelineQuery& query);

} // namespace core::audit
