#pragma once

#include "core/audit/audit_baseline.h"
#include "core/audit/audit_snapshot.h"
#include "core/audit/audit_transaction.h"
#include "core/audit/timeline_types.h"

#include <vector>

namespace core::audit {

// Pure function: assemble a `TimelineModel` from already-loaded audit data.
// The builder owns sorting, label formatting, and view-mode filtering. It
// does not touch the file system, ImGui, or the canvas — callers supply the
// inputs and the caller's scope (e.g. per-argument-package) is already
// applied upstream.
//
// Ordering: points are emitted in ascending `transaction_sequence`, with the
// synthetic `Now` marker appended last. When multiple markers share a
// sequence, baselines precede snapshots.
TimelineModel BuildTimelineModel(const std::vector<AuditTransaction>& transactions,
                                 const std::vector<BaselineMetadata>& baselines,
                                 const std::vector<SnapshotMetadata>& snapshots,
                                 const TimelineQuery& query);

} // namespace core::audit
