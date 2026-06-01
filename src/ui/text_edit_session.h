#pragma once

#include "imgui.h"

#include <string>
#include <vector>

namespace ui {

// A finished (or still-open) localized text edit on one element field,
// carrying everything the audited UpdateElementTextCommand needs to record
// the change. `original_value` is the value the field held when the user
// first focused it; `new_value` is the latest value typed.
struct PendingTextEdit {
    std::string element_id;
    std::string field_token;
    std::string language;
    std::string original_value;
    std::string new_value;
};

// Tracks per-widget "original" snapshots so a sequence of keystrokes on a
// single ImGui::InputText collapses into one audited commit when the user
// finishes editing the field (focus leaves it). The runtime turns each
// commit into a single UpdateElementTextCommand transaction, rather than
// one transaction per keystroke.
//
// Usage at a call site (after the InputText that binds to `text`):
//   const ImGuiID id = ImGui::GetID("##edit");
//   PendingTextEdit commit;
//   if (TextEditSession::Track(id, text, element_id, field_token, lang, commit)) {
//       callbacks.commit_text_edit(commit.element_id, commit.field_token,
//                                  commit.language, commit.original_value,
//                                  commit.new_value);
//   }
//
// Track must be called every frame the widget is shown — that's how it
// observes the activation/deactivation transitions reported by ImGui via
// IsItemActivated / IsItemDeactivatedAfterEdit on the current item, and how
// it keeps each open edit's latest value current.
class TextEditSession {
public:
    // Observe the previous item this frame. If it was activated, captures
    // `current` as the pre-edit original under `id` along with the element /
    // field / language the widget edits. While the edit stays open, keeps the
    // entry's latest value in sync with `current`. If the item was
    // deactivated-after-edit this frame, fills `out_commit` and returns true
    // when the value actually changed.
    static bool Track(ImGuiID id,
                      const std::string& current,
                      const std::string& element_id,
                      const std::string& field_token,
                      const std::string& language,
                      PendingTextEdit& out_commit);

    // Return every still-open edit whose latest value differs from its
    // captured pre-edit original, and advance each entry's original to that
    // latest value so a subsequent ImGui deactivation does not re-commit the
    // same change.
    //
    // This is the imperative escape hatch for the forced-flush points
    // (save / close): ImGui's deactivation transition never fires when a
    // focused field stops being rendered (panel switch, programmatic
    // selection change, app close). Without this, the live keystroke edit —
    // already written into the model — would reach the SACM through an
    // un-audited save path and be flagged later as an audit-log divergence.
    static std::vector<PendingTextEdit> CollectPendingEdits();

    // Drops every captured original. Call when the panel/element selection
    // changes in a way that invalidates any pending audit (e.g., the
    // project is closed). Pending edits are silently abandoned.
    static void ClearAll();

private:
    TextEditSession() = delete;
};

} // namespace ui
