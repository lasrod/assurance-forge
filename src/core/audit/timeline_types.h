#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Timeline data primitives shared by the audit data layer, the model builder,
// and the UI widget. These types are deliberately UI-free so that
// `TimelineModelBuilder` and its tests do not need ImGui.
//
// Phase 2 scope (per Assurance Timeline Implementation Plan):
//   - Unified rail: always emits InitialSnapshot, Baselines, Snapshots,
//     Changes (one per transaction), and a synthetic Now marker.
//   - Scopes: WholeCase, CurrentPackage
// `TimelineViewMode` is retained for compatibility with the existing widget
// dropdown but is no longer consulted by `BuildTimelineModel`. The remaining
// scope enumerators and point types (Compare / SelectedElement /
// ChangeGroup / PreviewRevision / CompareStart / CompareEnd) are reserved
// for later phases and intentionally compile today so callers can
// pattern-match exhaustively.
namespace core::audit {

enum class TimelineViewMode {
    Baselines,
    Snapshots,
    // Reserved for later phases.
    Changes,
    Compare,
    SelectedElement,
};

enum class TimelineScope {
    WholeCase,
    CurrentPackage,
    // Reserved for later phases.
    CurrentBranch,
    SelectedElement,
    CurrentBaseline,
};

// Order matters: the builder's stable-sort comparator uses the underlying
// integer value as the secondary key, so the kind priority at a shared
// transaction sequence is InitialSnapshot < Baseline < Snapshot < Change.
// `Now` is appended after sorting so its position relative to the others
// is unaffected by its enum value.
enum class TimelinePointType {
    InitialSnapshot,
    Baseline,
    Snapshot,
    Change,
    Now,
    // Reserved for later phases.
    ChangeGroup,
    PreviewRevision,
    CompareStart,
    CompareEnd,
};

struct TimelinePoint {
    std::uint64_t     transaction_sequence = 0;
    TimelinePointType type = TimelinePointType::Now;
    // Stable identifier for hit-testing / persistence. Empty for the
    // synthetic Now marker.
    std::string       id;
    // Short label rendered on or near the marker (e.g. "B0", "S3", "NOW").
    std::string       label;
    // Multi-line hover tooltip rendered by the widget.
    std::string       tooltip;
    // True for major markers (baselines + Now) that the rail draws larger.
    bool              is_major = false;
};

struct TimelineModel {
    std::vector<TimelinePoint> points;
    std::uint64_t              latest_sequence = 0;
    bool                       has_audit_store = false;
};

struct TimelineQuery {
    TimelineViewMode             view_mode = TimelineViewMode::Baselines;
    TimelineScope                scope = TimelineScope::WholeCase;
    // Optional package identity used when `scope == CurrentPackage`. The
    // builder does not consult these for Phase 1 (filtering happens upstream
    // when the caller passes pre-filtered transactions) but keeps them in
    // the query so future scopes can use them without an API churn.
    std::optional<std::string>   package_id;
    std::optional<std::string>   package_gid;
    // When non-empty, the builder tags the snapshot whose `snapshot_id`
    // equals this value as `TimelinePointType::InitialSnapshot` (sorted
    // first at its sequence) instead of `Snapshot`. Sourced from
    // `AuditManifest::initial_snapshot_id`.
    std::string                  initial_snapshot_id;
};

} // namespace core::audit
