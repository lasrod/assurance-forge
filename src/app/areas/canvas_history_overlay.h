#pragma once

#include "app/app_runtime_state.h"
#include "app/areas/workbench_area.h"
#include "core/sacm_model.h"

#include <string>

namespace ui {
struct UiState;
struct ElementContextActions;
} // namespace ui
namespace ui::gsn {
class GsnCanvas;
struct CanvasOverlayButtons;
} // namespace ui::gsn
namespace sacm {
struct ArgumentPackage;
struct AssuranceCasePackage;
} // namespace sacm

namespace app::areas {

// Render the audit-divergence warning banner if the loaded project's audit
// log no longer reproduces the on-disk SACM. No-op when the project has no
// audit store or when verification succeeded. Drawn at the top of every
// ArgumentPackage canvas tab so the warning is always visible regardless of
// which package the user is looking at.
void RenderCanvasDivergenceBanner(::app::AppRuntimeState& state, const WorkbenchAreaCallbacks& callbacks);

// Render the autosave-failure banner if `state.last_autosave_error` is
// non-empty. The banner is sticky (no auto-dismiss) until the user clicks
// Dismiss or a subsequent audited command succeeds and clears the field.
// Indicates the in-memory model + audit log are ahead of the on-disk SACM.
void RenderCanvasAutosaveErrorBanner(::app::AppRuntimeState& state);

// Render the working-draft banner while the argument has unaccepted changes.
//
// **The single most important thing on the canvas when a draft is active.** The
// canvas is drawing an argument that includes claims no human has accepted, and
// a reviewer who mistakes it for the accepted case is the failure ADR 0009 names
// first. So the banner says so in words, names the sources that contributed, and
// carries the switch back to the accepted baseline -- never colour alone, which
// a colour-blind reader or a greyscale screenshot would lose entirely.
//
// No-op when nothing is proposed.
void RenderWorkingDraftBanner(::app::AppRuntimeState& state, const WorkbenchAreaCallbacks& callbacks);

// Render an ArgumentPackage canvas tab with the always-visible Assurance
// Timeline rail in its bottom overlay strip.
//
// Behaviour:
//   - When the project has no audit store, this just renders the live
//     canvas (no timeline rail, no Live pill).
//   - When the project has an audit store, the function:
//       * Loads transactions, baselines, and snapshots scoped to this
//         argument package (per-tab cached).
//       * Renders either the live canvas (`tab.timeline.preview_sequence`
//         unset) or the reconstructed historical canvas (preview set).
//       * Paints the Timeline widget into the bottom overlay strip via
//         `CanvasOverlayButtons::on_render_timeline_strip` and dispatches
//         widget actions back onto `tab.timeline`.
//       * Shows a "Live" pill while a preview is active.
//
// Per-tab reconstruction and filtered-transaction caches are keyed on
// `tab.key` and pruned via `ForgetCanvasHistoryTab`.
void RenderArgumentPackageCanvasWithTimeline(::app::AppRuntimeState& state,
                                             ui::UiState& ui_state,
                                             const WorkbenchAreaCallbacks& callbacks,
                                             ::app::WorkbenchState::ArgumentPackageCanvasTab& tab,
                                             const sacm::ArgumentPackage& argument_package,
                                             const parser::AssuranceCase& live_projection,
                                             ui::gsn::GsnCanvas& live_renderer,
                                             const ui::ElementContextActions& live_actions,
                                             const sacm::AssuranceCasePackage* terminology_package);

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
