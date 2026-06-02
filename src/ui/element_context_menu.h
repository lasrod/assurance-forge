#pragma once

#include "core/element_factory.h"
#include "core/terminology_package_service.h"
#include "core/sacm_model.h"

#include <functional>
#include <string>

namespace ui {

struct ElementContextActions {
    std::function<void(core::NewElementKind)> add_child;
    std::function<void()> add_top_goal;
    std::function<void()> add_acp_to_selected_element;
    std::function<void(const std::string& relationship_id)> add_acp_to_relationship;
    std::function<void(const std::string& acp_id)> remove_acp;
    std::function<void(core::RemoveMode)> remove_selected;
    std::function<void()> render_ai_review_menu;
    std::function<void(const char*)> not_implemented;
    // GSN v3 dialectic challenges. The no-arg variants challenge the selected
    // element; the relationship variants challenge a specific relationship id.
    std::function<void()> add_counter_argument;
    std::function<void()> add_counter_evidence;
    std::function<void(const std::string& relationship_id)> add_counter_argument_to_relationship;
    std::function<void(const std::string& relationship_id)> add_counter_evidence_to_relationship;
    std::function<void(const core::TerminologyPackageRef&, const core::TerminologyTermRef&)> open_terminology_term;
    std::function<void(const core::TerminologyPackageRef&, const core::TerminologyTermRef&)> edit_terminology_term;
    std::function<void(const std::string& element_id, const std::string& term_value)> define_terminology_term;
    std::function<void(
        const std::string& element_id, const core::TerminologyPackageRef&, const core::TerminologyTermRef&)>
        add_terminology_term_as_context;
    std::function<void(
        const std::string& element_id, const core::TerminologyPackageRef&, const core::TerminologyTermRef&)>
        add_visible_terminology_term_context;
    std::function<void(const core::TerminologyPackageRef&, const core::TerminologyTermRef&)> find_terminology_usages;
    std::function<void(const std::string& element_id, const std::string& term_value)> change_terminology_meaning;
    std::function<void(const std::string& message)> set_status;
    // Invoked when the user clicks an element's problem badge. Receives the
    // highest-severity problem id and its element id; the runtime opens the
    // Problems panel and selects the matching row.
    std::function<void(const std::string& problem_id, const std::string& element_id)> focus_problem;
};

void RenderAddElementMenu(const ElementContextActions& actions);
void RenderAiReviewMenu(const ElementContextActions& actions);

void RenderRemoveSubmenu(const parser::AssuranceCase* active_case,
                         const std::string& selected_id,
                         const ElementContextActions& actions);

} // namespace ui
