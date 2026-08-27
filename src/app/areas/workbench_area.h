#pragma once

#include "core/element_factory.h"
#include "core/sacm_model.h"
#include "core/terminology_package_service.h"
#include "imgui.h"
#include "legacy_sacm/sacm_model.h"
#include "ui/element_context_menu.h"
#include "ui/panels/terminology_package_panel.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
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
    // The working-draft banner's two decisions. Promotion is the one point where
    // proposed work becomes accepted argument, and there is deliberately no
    // route to it that does not pass through a person clicking here.
    std::function<void()> promote_working_draft;
    std::function<void()> discard_working_draft;
    // The evidence register's actions (see AppRuntime for what each does).
    std::function<void(const std::string& element_id)> locate_element;
    std::function<void(const std::string& evidence_id)> remove_evidence;
    std::function<void(const std::string& evidence_id, const std::string& location)> set_evidence_location;
    std::function<void(const std::string& location)> open_evidence_location;
};

void RenderWorkbenchArea(AppRuntimeState& state,
                         const frame::AppLayoutRegion& region,
                         ImGuiWindowFlags panel_flags,
                         const WorkbenchAreaCallbacks& callbacks);

// What the argument-package canvas draws for one package: the committed
// argument, or the preview of the change set an agent has open against it.
//
// Split out because this is the seam the live canvas got wrong. The Argument
// Navigator was fed the preview and the canvas was not, so one view of a change
// set showed eighty staged elements and the other showed none -- and unit tests
// of both halves passed throughout, because each half worked. Choosing the model
// and choosing the projection are one decision and are tested as one.
// The model the Terminology Package tab renders, built from the WORKING
// package (ADR 0016): the draft's glossary while a draft differs from the
// accepted argument, badged row by row with what the draft changed, and with
// editing locked while a draft document exists. Exposed so the tab's claims
// can be tested without ImGui.
ui::panels::TerminologyPackagePanelModel BuildTerminologyPackagePanelModel(AppRuntimeState& state);

parser::AssuranceCase BuildArgumentPackageCanvasCase(const parser::AssuranceCase& committed,
                                                     const std::optional<parser::AssuranceCase>& agent_preview,
                                                     const std::vector<std::string>& agent_preview_added_ids,
                                                     const sacm::AssuranceCasePackage& package,
                                                     const sacm::ArgumentPackage& argument_package,
                                                     std::string_view fallback_title);

} // namespace app::areas
