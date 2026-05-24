#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Timeline data primitives shared by the audit data layer, the model builder,
// and the UI widget. These types are deliberately UI-free so that
// `TimelineModelBuilder` and its tests do not need ImGui.
//
// Phase 1 scope (per Assurance Timeline Implementation Plan):
//   - View modes: Baselines, Snapshots
//   - Scopes: WholeCase, CurrentPackage
//   - Point types in use: Now, Baseline, Snapshot
// The remaining enumerators are reserved for later phases (Changes /
// Compare / SelectedElement) and intentionally compile today so callers can
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

enum class TimelinePointType {
    Now,
    Baseline,
    Snapshot,
    // Reserved for later phases.
    Change,
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
};

} // namespace core::audit
