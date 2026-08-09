#pragma once

#include "core/terminology_package_service.h"
#include "core/sacm_model.h"
#include "legacy_sacm/sacm_model.h"
#include "ui/panels/confidence_panel.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ui::panels {

struct ElementTerminologyAssistCallbacks {
    std::function<void(const std::string& element_id, const std::string& term)> define_term;
    std::function<void(const std::string& element_id, const std::string& term)> link_existing_term;
    std::function<void(const std::string& element_id,
                       const core::TerminologyPackageRef& package_ref,
                       const core::TerminologyTermRef& term_ref)>
        use_term_for_element;
    std::function<void(const std::string& element_id, const std::string& term)> ignore_term;
    std::function<bool(const std::string& element_id, const std::string& term)> is_ignored;
    std::function<void()> focus_review_tab;
};

// Secondary-language translation review. When the user edits the text of an
// element that carries a translation, the other language may no longer match, so
// the element is flagged until the user reviews both and accepts. The panel
// renders a warning + "Mark reviewed" button driven by these callbacks.
struct ElementTranslationReviewCallbacks {
    std::function<bool(const std::string& element_id)> is_pending;
    std::function<void(const std::string& element_id)> accept;
};

struct ElementConfidenceAssistCallbacks {
    std::function<ConfidencePanelModel(const parser::SacmElement& element)> model_for_element;
    std::function<bool(parser::SacmElement& element, const ui::ElementConfidence& confidence)> save_confidence;
    std::function<bool(parser::SacmElement& element, bool active)> set_confidence_active;
    std::function<bool(parser::SacmElement& element)> mark_reviewed;
    std::function<bool()> backup_invalid_and_reset;
};

// Audited text-edit dispatch. Called once when a text field loses focus
// after the user changed its value (i.e., the end of a typing session, not
// per keystroke). The runtime translates each call into one
// UpdateElementTextCommand transaction. `original_value` is the value the
// field held immediately before the edit began and is forwarded as the
// audit event's `old_value`.
struct ElementTextEditCallbacks {
    std::function<void(const std::string& element_id,
                       const std::string& field_token,
                       const std::string& language,
                       const std::string& original_value,
                       const std::string& new_value)>
        commit_text_edit;
};

// Per-element history summary surfaced in the Element Properties panel.
// The app layer is responsible for producing the summary from the project's
// audit store (transactions + latest baseline). The UI is presentation-only.
struct ElementHistoryModel {
    bool available = false; // false → audit data not loaded; section is hidden
    bool ever_seen = false; // element has appeared in any transaction
    std::uint64_t change_count = 0;
    std::uint64_t last_sequence = 0;
    std::string last_changed_at;
    std::string last_changed_by;
    bool has_baseline = false;
    std::string baseline_label; // e.g. "B2 — v1.2 release"
    bool changed_since_baseline = false;
};

struct ElementHistoryCallbacks {
    // Build the summary for the given element. Called once per frame when an
    // element is selected. Return `available=false` to hide the section.
    std::function<ElementHistoryModel(const std::string& element_id)> model_for_element;
    // Invoked when the user clicks "View Element History" — the app should
    // activate the FeedbackDock "History" tab and apply an element filter.
    std::function<void(const std::string& element_id)> open_element_history;
};

// Accepting or rejecting the working-draft change on the selected element.
//
// Both take the *closure* the inspector displayed, not the raw contributing
// groups: the user agreed to what they were shown, and promoting a different set
// than the one on screen is the failure the closure exists to prevent.
struct ElementDraftCallbacks {
    std::function<void(const std::vector<std::string>& group_ids)> accept_groups;
    std::function<void(const std::vector<std::string>& group_ids)> reject_groups;
};

// Render the element properties panel with editable fields.
// Returns true if any field was modified (caller should rebuild tree).
// When `read_only` is true, all editable widgets (text fields, checkboxes,
// translation/confidence controls) render visually disabled and cannot be
// edited. The History section, terminology suggestion buttons that don't
// mutate the model, and metadata stay interactive.
bool ShowElementPanel(parser::AssuranceCase* ac,
                      sacm::AssuranceCasePackage* sacm_pkg,
                      const ElementTerminologyAssistCallbacks* terminology_callbacks = nullptr,
                      const ElementConfidenceAssistCallbacks* confidence_callbacks = nullptr,
                      const ElementTextEditCallbacks* text_edit_callbacks = nullptr,
                      const ElementHistoryCallbacks* history_callbacks = nullptr,
                      const ElementTranslationReviewCallbacks* translation_review_callbacks = nullptr,
                      const ElementDraftCallbacks* draft_callbacks = nullptr,
                      bool read_only = false);

// Human-readable name for a raw SACM element type.
//
// `SacmElement::type` is the XML local name lowercased ("assertedinference"),
// which is a storage identifier, not something to put in front of a reader
// deciding whether to trust a safety argument. The parser admits a closed set,
// so the mapping is complete; an unrecognised value is returned unchanged
// rather than guessed at, because inventing a label for an element we did not
// expect would misrepresent the file.
std::string ElementTypeDisplayName(const std::string& raw_type);

} // namespace ui::panels
