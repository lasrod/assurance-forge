#pragma once

#include "core/terminology_package_service.h"
#include "parser/xml_parser.h"
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
    std::function<bool(parser::SacmElement& element)> clear_confidence;
    std::function<bool(parser::SacmElement& element)> mark_reviewed;
    std::function<bool()> backup_invalid_and_reset;
};

// Render the element properties panel with editable fields.
// Returns true if any field was modified (caller should rebuild tree).
bool ShowElementPanel(parser::AssuranceCase* ac,
                      sacm::AssuranceCasePackage* sacm_pkg,
                      const ElementTerminologyAssistCallbacks* terminology_callbacks = nullptr,
                      const ElementConfidenceAssistCallbacks* confidence_callbacks = nullptr);

} // namespace ui::panels
