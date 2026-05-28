#include "core/audit/undo_boundary.h"

#include <algorithm>

namespace core::audit {

std::uint64_t FindUndoBoundary(const std::vector<SnapshotMetadata>& snapshots,
                               const std::vector<BaselineMetadata>& baselines,
                               std::uint64_t current_sequence) {
    std::uint64_t boundary = 0; // initial snapshot at sequence 0 is implicit
    for (const SnapshotMetadata& s : snapshots) {
        if (s.transaction_sequence <= current_sequence) {
            boundary = std::max(boundary, s.transaction_sequence);
        }
    }
    for (const BaselineMetadata& b : baselines) {
        if (b.transaction_sequence <= current_sequence) {
            boundary = std::max(boundary, b.transaction_sequence);
        }
    }
    return boundary;
}

} // namespace core::audit
