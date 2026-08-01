#pragma once

#include "core/audit/audit_transaction.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// History Timeline panel (design §16). Read-only view of the project's
// append-only audit log: lets the user scrub through transactions with a
// slider, inspect the per-transaction event summary, and see the
// reconstructed canonical model hash for the selected point in history.
//
// The panel is intentionally pure presentation — it does not load files,
// run the replay engine, or build canvas trees. The owning `app` area
// supplies the transaction list and the reconstructed-state summary and
// reacts to selection callbacks.
namespace ui::panels {

struct HistoryTimelinePanelModel {
    // Whether the active project has an initialized audit store. When false
    // the panel renders an informational message and disables interaction.
    bool has_audit_store = false;

    // All transactions in append order, owned by the caller.
    const std::vector<core::audit::AuditTransaction>* transactions = nullptr;

    // Sequence the panel is currently pinned to. std::nullopt means "live".
    std::optional<std::uint64_t> selected_sequence;
};

struct HistoryTimelinePanelCallbacks {
    // Called when the user picks a sequence in [0, latest]. 0 means "show
    // the initial snapshot with no events applied".
    std::function<void(std::uint64_t)> on_select_sequence;
    // Called when the user clicks "Return to live"; clears the selection.
    std::function<void()> on_return_to_live;
};

// Render the compact transaction slider above the canvas.
void ShowHistoryTimelineHeader(const HistoryTimelinePanelModel& model, const HistoryTimelinePanelCallbacks& callbacks);

// Render the transactions table. Pair with `ShowHistoryTimelineHeader`.
void ShowHistoryTimelineTransactions(const HistoryTimelinePanelModel& model,
                                     const HistoryTimelinePanelCallbacks& callbacks);

} // namespace ui::panels
