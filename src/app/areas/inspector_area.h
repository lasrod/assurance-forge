#pragma once

#include "core/sacm_model.h"
#include "core/terminology_package_service.h"
#include "imgui.h"

#include <functional>
#include <vector>
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
    // The model the panel shows and edits. While a draft is active this is the
    // *working* argument, so an element another draft group created can be
    // selected and inspected at all -- reading the accepted model made those
    // report "Element not found", which is every element the draft adds.
    std::function<parser::AssuranceCase*()> inspector_model;
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
    // Toggle the GSN undeveloped decorator through the audited command. Appended
    // rather than placed next to `commit_element_text_edit` because this struct
    // is aggregate-initialized POSITIONALLY in app_runtime_frame.cpp, and several
    // members share a `std::function` signature -- inserting in the middle would
    // silently rebind its neighbours rather than fail to compile.
    std::function<void(const std::string& element_id, bool undeveloped)> set_element_undeveloped;
};

void RenderInspectorArea(AppRuntimeState& state,
                         const frame::AppLayoutRegion& region,
                         ImGuiWindowFlags panel_flags,
                         const InspectorAreaCallbacks& callbacks);

} // namespace app::areas
