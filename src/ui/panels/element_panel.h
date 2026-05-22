#pragma once

#include "core/terminology_package_service.h"
#include "core/sacm_model.h"
#include "sacm/sacm_model.h"
#include "ui/panels/confidence_panel.h"

#include <functional>
#include <string>

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

// Render the element properties panel with editable fields.
// Returns true if any field was modified (caller should rebuild tree).
bool ShowElementPanel(parser::AssuranceCase* ac,
                      sacm::AssuranceCasePackage* sacm_pkg,
                      const ElementTerminologyAssistCallbacks* terminology_callbacks = nullptr,
                      const ElementConfidenceAssistCallbacks* confidence_callbacks = nullptr,
                      const ElementTextEditCallbacks* text_edit_callbacks = nullptr);

} // namespace ui::panels
