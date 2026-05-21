#pragma once

#include "app/app_runtime_state.h"
#include "app/areas/workbench_area.h"
#include "core/sacm_model.h"

#include <string>

namespace ui { class UiState; }
namespace ui::gsn { class GsnCanvas; struct CanvasOverlayButtons; }
namespace sacm { struct ArgumentPackage; }

namespace app::areas {

// Render the audit-divergence warning banner if the loaded project's audit
// log no longer reproduces the on-disk SACM. No-op when the project has no
// audit store or when verification succeeded. Drawn at the top of every
// ArgumentPackage canvas tab so the warning is always visible regardless of
// which package the user is looking at.
void RenderCanvasDivergenceBanner(::app::AppRuntimeState& state,
                                  const WorkbenchAreaCallbacks& callbacks);

// Render the in-canvas history overlay for an ArgumentPackage canvas tab.
//
// Layout (vertical, top-to-bottom):
//   1. Header strip   — LIVE/HISTORICAL banner, transaction-sequence slider,
//                       reconstructed-state summary, "Return to live" button.
//   2. Canvas region  — either the live per-tab renderer (when the slider is
//                       at the latest sequence) or a per-tab historical
//                       renderer seeded with the reconstructed model.
//   3. Transactions   — table of transactions filtered to this argument
//                       package's running scope.
//
// The overlay owns one historical `ui::gsn::GsnCanvas` per `tab.key`; the
// live canvas is the caller's existing per-tab renderer so pan/zoom state
// survives toggling history on/off. Per-tab reconstruction and filtered-
// transaction caches are keyed on `tab.key` and pruned via
// `ForgetCanvasHistoryTab`.
void RenderCanvasHistoryOverlay(::app::AppRuntimeState& state,
                                ui::UiState& ui_state,
                                const WorkbenchAreaCallbacks& callbacks,
                                ::app::WorkbenchState::ArgumentPackageCanvasTab& tab,
                                const sacm::ArgumentPackage& argument_package,
                                const parser::AssuranceCase& live_projection,
                                ui::gsn::GsnCanvas& live_renderer,
                                const ui::gsn::CanvasOverlayButtons* overlay_buttons);

// Drop overlay state for a closed tab.
void ForgetCanvasHistoryTab(const std::string& tab_key);

// True when the active project has an initialized audit store on disk. Used
// by the workbench to decide whether the "History" toggle button should be
// enabled.
bool ProjectHasAuditStore(const ::app::AppRuntimeState& state);

// True when the active project has an audit store *and* at least one
// committed transaction. Distinguishes "audit not initialized" from
// "audit initialized but empty" — used by the H-toggle to decide whether
// switching into history view would land on a useful state.
bool ProjectAuditLogHasTransactions(const ::app::AppRuntimeState& state);

} // namespace app::areas
