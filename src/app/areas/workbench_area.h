#pragma once

#include "core/element_factory.h"
#include "core/terminology_package_service.h"
#include "imgui.h"
#include "ui/element_context_menu.h"
#include "ui/panels/terminology_package_panel.h"

#include <functional>
#include <string>
#include <vector>

namespace app {
struct AppRuntimeState;
namespace frame {
struct AppLayoutRegion;
}
} // namespace app

namespace app::areas {

struct WorkbenchAreaCallbacks {
    std::function<ui::ElementContextActions()> make_element_context_actions;
    std::function<void(core::NewElementKind)> add_proposal_child;
    std::function<void()> add_proposal_top_goal;
    std::function<void(core::RemoveMode)> remove_proposal_selected;
    std::function<void(const char*)> show_not_implemented;
    std::function<bool(const std::string&)> edit_proposal_by_id;
    std::function<void(bool)> exit_proposal_canvas;
    std::function<void(const core::TerminologyPackageRef&, const core::TerminologyTermRef&)> open_terminology_term;
    std::function<void(const core::TerminologyPackageRef&, const core::TerminologyTermRef&)> edit_terminology_term;
    std::function<void(const std::string&, const std::string&)> define_terminology_term;
    std::function<void(const std::string&, const core::TerminologyPackageRef&, const core::TerminologyTermRef&)>
        add_terminology_term_as_context;
    std::function<void(const std::string&, const core::TerminologyPackageRef&, const core::TerminologyTermRef&)>
        add_visible_terminology_term_context;
    std::function<void(const core::TerminologyPackageRef&, const core::TerminologyTermRef&)> find_terminology_usages;
    std::function<void(const std::string&, const std::string&)> change_terminology_meaning;
    std::function<void()> apply_terminology_package_edits;
    std::function<void()> delete_terminology_package;
    std::function<void()> add_terminology_term;
    std::function<void(const core::TerminologyTermRef&)> select_terminology_term;
    std::function<void(const core::TerminologyTermRef&)> edit_terminology_term_from_package;
    std::function<void(const core::TerminologyTermRef&)> delete_terminology_term;
    std::function<void(const core::TerminologyTermRef&)> find_terminology_term_usages;
    std::function<void(const std::string&)> set_terminology_category_filter;
    std::function<void()> add_terminology_category;
    std::function<void(const core::TerminologyCategoryRef&)> select_terminology_category;
    std::function<void(const core::TerminologyCategoryRef&)> edit_terminology_category;
    std::function<void(const core::TerminologyCategoryRef&)> delete_terminology_category;
    std::function<void()> seed_recommended_terminology_categories;
    // Ignored-terms management for the terminology panel's "Ignored terms" section.
    std::function<std::vector<ui::panels::IgnoredTerminologyEntry>()> list_ignored_terms;
    std::function<void(const std::string& element_id, const std::string& term)> restore_ignored_term;
    // Invoked by the History Timeline area when the user clicks the
    // "Reconcile audit log" button on the divergence warning banner.
    std::function<void()> reconcile_audit_store;
};

void RenderWorkbenchArea(AppRuntimeState& state,
                         const frame::AppLayoutRegion& region,
                         ImGuiWindowFlags panel_flags,
                         const WorkbenchAreaCallbacks& callbacks);

} // namespace app::areas
