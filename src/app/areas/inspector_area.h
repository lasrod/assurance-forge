#pragma once

#include "core/terminology_package_service.h"
#include "imgui.h"

#include <functional>
#include <string>

namespace app {
struct AppRuntimeState;
namespace frame {
struct AppLayoutRegion;
}
} // namespace app

namespace app::areas {

struct InspectorAreaCallbacks {
    std::function<void()> render_proposal_element_editor;
    // Accept or reject the working-draft change on the selected element. Both
    // receive the dependency closure the inspector displayed, never a wider set.
    std::function<void(const std::vector<std::string>&)> accept_draft_groups;
    std::function<void(const std::vector<std::string>&)> reject_draft_groups;
    std::function<void(const std::string&, const std::string&)> define_terminology_term;
    std::function<void(const std::string&, const std::string&)> link_existing_terminology_term;
    std::function<void(const std::string&, const core::TerminologyPackageRef&, const core::TerminologyTermRef&)>
        use_terminology_term_for_element;
    std::function<void(const std::string&, const std::string&)> ignore_terminology_suggestion;
    std::function<bool(const std::string&, const std::string&)> is_terminology_suggestion_ignored;
    std::function<void()> focus_review_tab;
    std::function<void()> mark_element_modified;
    std::function<void(const std::string& element_id,
                       const std::string& field_token,
                       const std::string& language,
                       const std::string& original_value,
                       const std::string& new_value)>
        commit_element_text_edit;
    std::function<void(const std::string& element_id)> open_element_history;
    std::function<bool(const std::string& element_id)> is_translation_review_pending;
    std::function<void(const std::string& element_id)> accept_translation_review;
};

void RenderInspectorArea(AppRuntimeState& state,
                         const frame::AppLayoutRegion& region,
                         ImGuiWindowFlags panel_flags,
                         const InspectorAreaCallbacks& callbacks);

} // namespace app::areas
