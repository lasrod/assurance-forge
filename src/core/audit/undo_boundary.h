#pragma once

#include "core/audit/audit_baseline.h"
#include "core/audit/audit_snapshot.h"

#include <cstdint>
#include <vector>

// Undo-boundary helpers (Plan §5.1 / §5.2).
//
// `Undo` (Ctrl+Z) is bounded by the nearest snapshot or baseline at or
// before the current sequence. Snapshot 0 is always present (created on
// project init), so a fresh project's wall sits at sequence 0 and the
// user can step back through every transaction until that wall — at
// which point further undo is refused with a UI hint pointing at the
// historical-preview + restore workflow.
namespace core::audit {

// Highest snapshot- or baseline-pinned sequence that is `<= current_sequence`.
// Includes the implicit initial snapshot at sequence 0, so the return
// value is always well-defined for any project that has an audit store.
//
// The inputs are accepted as pre-fetched vectors (matching the rest of
// the timeline subsystem) so this helper has no I/O and can be tested
// in isolation.
std::uint64_t FindUndoBoundary(const std::vector<SnapshotMetadata>& snapshots,
                               const std::vector<BaselineMetadata>& baselines,
                               std::uint64_t current_sequence);

// Convenience: a single transaction can be undone iff the current sequence
// is strictly past the boundary.
inline bool CanUndo(std::uint64_t current_sequence, std::uint64_t boundary) {
    return current_sequence > boundary;
}

} // namespace core::audit
