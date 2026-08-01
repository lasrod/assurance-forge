// Application status bar.
//
// Two jobs. First, save state: SACM XML is the source of truth, and until now
// nothing on screen said whether what the user is looking at has been written to
// it. Second, it is the sink for `core::AppState::status_message` — around twenty
// call sites set that string ("Project saved: X", "Review item save failed: ...")
// and only one panel, visible in one center view, ever rendered it. Most of the
// application's feedback was being produced and thrown away.
#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace ui::panels {

struct StatusBarModel {
    bool has_project = false;
    std::string project_name;
    std::string document_name;      // File name only; empty when no document is open.
    std::string document_full_path; // Shown as a tooltip — status bars must stay one line.
    bool has_unsaved_changes = false;

    // Most recent transient message. Not persisted; replaced by the next one.
    std::string message;
    bool message_is_error = false;

    std::size_t error_count = 0;
    std::size_t warning_count = 0;

    std::string selected_element_id;
};

struct StatusBarCallbacks {
    // Raises the Problems panel. Wired to the counts so they are a way in, not
    // just a readout.
    std::function<void()> open_problems;
};

// "MySafetyCase — main.sacm", degrading to the project alone when no document is
// open and to a placeholder when nothing is. Pure, so it is testable without a
// UI context.
std::string DocumentLabel(const StatusBarModel& model);

// Explicit both ways rather than treating absence as "saved": in a tool whose
// output is a safety argument, an ambiguous save indicator is a trust problem.
std::string SaveStateLabel(const StatusBarModel& model);

// Height the bar occupies, so the layout can subtract it before placing panels.
// Valid only inside a frame — it derives from the current font metrics.
float StatusBarHeight();

void ShowStatusBar(const StatusBarModel& model, const StatusBarCallbacks& callbacks);

} // namespace ui::panels
