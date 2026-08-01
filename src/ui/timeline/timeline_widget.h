#pragma once

#include "core/audit/timeline_types.h"
#include "ui/timeline/timeline_state.h"

#include "imgui.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace ui::timeline {

// Outcome of a single TimelineWidget render pass. Pure data; the app layer
// decides what to do (mutate state, open modals, dispatch commands).
enum class TimelineActionType {
    None,
    PreviewSequence, // payload: sequence
    ReturnToLatest,
    ChangeViewMode, // payload: view_mode
    OpenActionsMenu,
    CreateBaseline,
    CreateSnapshot,
    OpenHistoryAtSequence, // Phase 2 — emit when user clicks a marker tooltip's "open in history" link.
};

struct TimelineAction {
    TimelineActionType type = TimelineActionType::None;
    std::optional<std::uint64_t> sequence;
    std::optional<core::audit::TimelineViewMode> view_mode;
};

// Computed geometry for one marker on the rail. Extracted so unit tests can
// validate ordering and spacing without an ImGui context.
struct MarkerLayout {
    std::size_t point_index = 0; // Index into TimelineModel::points
    float center_x = 0.0f;       // Screen-space center x (within the rail rect)
    float half_width = 0.0f;     // Half hit-test width
};

// Lays out markers into the rail rect [rect_min.x, rect_max.x]. Spacing is
// proportional to transaction_sequence between earliest and latest_sequence;
// when latest_sequence==0 (no transactions) all markers collapse to the
// right edge.
std::vector<MarkerLayout>
ComputeMarkerLayout(const core::audit::TimelineModel& model, const ImVec2& rect_min, const ImVec2& rect_max);

// Render the timeline widget inside `rect`. Returns the action the user
// performed this frame (or `None`). `tab_id` is appended to ImGui IDs so
// multiple tabs can coexist.
TimelineAction RenderTimelineWidget(TimelineState& state,
                                    const core::audit::TimelineModel& model,
                                    const ImVec2& rect_min,
                                    const ImVec2& rect_max,
                                    const char* tab_id);

} // namespace ui::timeline
