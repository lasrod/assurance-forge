#pragma once

namespace app::areas {

// Render the floating "Performance" overlay window (Phase 3 of the GSN perf
// analysis plan). `open` is bound to `UiState::show_perf_overlay` so the
// window can close itself via the title-bar X.
void RenderPerfOverlay(bool& open);

} // namespace app::areas
