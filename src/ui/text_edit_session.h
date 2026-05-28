#pragma once

#include "imgui.h"

#include <string>

namespace ui {

// Tracks per-widget "original" snapshots so a sequence of keystrokes on a
// single ImGui::InputText collapses into one audited commit when the user
// finishes editing the field (focus leaves it). The runtime turns each
// commit into a single UpdateElementTextCommand transaction, rather than
// one transaction per keystroke.
//
// Usage at a call site (after the InputText that binds to `text`):
//   const ImGuiID id = ImGui::GetID("##edit");
//   std::string original;
//   if (TextEditSession::TryCommit(id, text, original)) {
//       callbacks.commit_text_edit(element_id, field_token, lang,
//                                  original, text);
//   }
//
// TryCommit must be called every frame the widget is shown — that's how it
// observes the activation/deactivation transitions reported by ImGui via
// IsItemActivated / IsItemDeactivatedAfterEdit on the current item.
class TextEditSession {
public:
    // If the previous-item was activated this frame, captures `current` as
    // the pre-edit original under `id`. If the previous-item was
    // deactivated-after-edit this frame and an original was captured, fills
    // `out_original` and returns true when the current value differs from
    // the captured original.
    static bool TryCommit(ImGuiID id, const std::string& current, std::string& out_original);

    // Drops every captured original. Call when the panel/element selection
    // changes in a way that invalidates any pending audit (e.g., the
    // project is closed). Pending edits are silently abandoned.
    static void ClearAll();

private:
    TextEditSession() = delete;
};

} // namespace ui
