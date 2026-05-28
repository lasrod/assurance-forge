#pragma once

#include "core/audit/timeline_types.h"

#include <cstdint>
#include <optional>

namespace ui::timeline {

// Transient UI state for the always-visible timeline rail in the GSN canvas
// overlay strip. One instance lives per ArgumentPackageCanvasTab.
struct TimelineState {
    core::audit::TimelineViewMode view_mode = core::audit::TimelineViewMode::Baselines;
    core::audit::TimelineScope    scope = core::audit::TimelineScope::CurrentPackage;
    // When set, the canvas renders the reconstructed model at this sequence
    // and the rail draws a "preview" indicator. When unset, the canvas
    // shows the live model.
    std::optional<std::uint64_t>  preview_sequence;
    // Open/close state for the "⋯" actions menu popup.
    bool                          show_actions_menu = false;
};

} // namespace ui::timeline
