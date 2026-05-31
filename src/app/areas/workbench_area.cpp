#include "app/areas/workbench_area.h"

#include "app/app_runtime_state.h"
#include "app/areas/canvas_history_overlay.h"
#include "app/frame/app_layout_regions.h"
#include "app/frame/app_shell.h"
#include "core/argument_package_projection.h"
#include "core/perf/frame_profiler.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/gsn/gsn_canvas_renderer.h"
#include "ui/i18n/localization.h"
#include "ui/panels/package_details_panel.h"
#include "ui/panels/terminology_package_panel.h"
#include "ui/register_views.h"
#include "sacm/sacm_model.h"
#include "ui/theme.h"
#include "ui/ui_state.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace app::areas {
namespace {

std::unordered_map<std::string, ui::gsn::GsnCanvas> g_argument_package_canvas_renderers;

// Per-tab cache of the expensive rebuild outputs (visible_case projection,
// derived AssuranceTree, and the case_revision the renderer was last seeded
// with). When the inputs are unchanged, we reuse the cached projection and
// skip both `BuildArgumentPackageProjection` (~7 ms) and
// `renderer.SetTree` (~3.7 ms, dominated by LayoutEngine::ComputeLayout).
struct ArgumentPackageTabCache {
    std::uint64_t case_revision = ~0ull;
    std::string argument_package_id;
    std::string argument_package_gid;
    std::string tab_title;
    bool show_secondary_language = false;
    std::string secondary_language;
    parser::AssuranceCase visible_case;
    core::AssuranceTree visible_tree;
    bool renderer_seeded = false;
    bool valid = false;
};
std::unordered_map<std::string, ArgumentPackageTabCache> g_argument_package_canvas_caches;

template <typename ElementT>
void AddElementIdentity(const ElementT& element,
                        std::unordered_set<std::string>& ids,
                        std::unordered_set<std::string>& gids) {
    if (!element.id.empty())
        ids.insert(element.id);
    if (!element.gid.empty())
        gids.insert(element.gid);
}

const sacm::ArgumentPackage* FindArgumentPackage(const sacm::AssuranceCasePackage& package,
                                                 const WorkbenchState::ArgumentPackageCanvasTab& tab) {
    return core::FindArgumentPackageByIdentity(package, tab.package_id, tab.package_gid);
}

parser::AssuranceCase BuildArgumentPackageProjection(const parser::AssuranceCase& source_model,
                                                     const sacm::ArgumentPackage& argument_package,
                                                     const std::string& fallback_name) {
    return core::BuildArgumentPackageProjection(source_model, argument_package, fallback_name);
}

ui::ElementContextActions MakeProposalContextActions(const WorkbenchAreaCallbacks& callbacks) {
    return ui::ElementContextActions{
        callbacks.add_proposal_child,
        callbacks.add_proposal_top_goal,
        nullptr,
        nullptr,
        nullptr,
        callbacks.remove_proposal_selected,
        nullptr,
        callbacks.show_not_implemented,
    };
}

ui::ElementContextActions MakeCanvasContextActions(const WorkbenchAreaCallbacks& callbacks) {
    ui::ElementContextActions actions =
        callbacks.make_element_context_actions ? callbacks.make_element_context_actions() : ui::ElementContextActions{};
    actions.open_terminology_term = callbacks.open_terminology_term;
    actions.edit_terminology_term = callbacks.edit_terminology_term;
    actions.define_terminology_term = callbacks.define_terminology_term;
    actions.add_terminology_term_as_context = callbacks.add_terminology_term_as_context;
    actions.add_visible_terminology_term_context = callbacks.add_visible_terminology_term_context;
    actions.find_terminology_usages = callbacks.find_terminology_usages;
    actions.change_terminology_meaning = callbacks.change_terminology_meaning;
    return actions;
}

void RenderProposalBanner(AppRuntimeState& state, const WorkbenchAreaCallbacks& callbacks) {
    const float banner_h = ImGui::GetStyle().WindowPadding.y * 2.0f + ImGui::GetTextLineHeight() +
                           ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeight() + 2.0f;
    const ImU32 banner_bg = ui::LerpColor(ui::GetTheme().surface_1, ui::GetTheme().attention, 0.18f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(banner_bg));
    ImGui::BeginChild("##proposal_preview_banner", ImVec2(0.0f, banner_h), true, ImGuiWindowFlags_NoScrollbar);
    auto& proposals = *state.proposal_controller;
    ImGui::TextUnformatted((proposals.creator_active ? AF_TR("PROPOSAL CREATOR") : AF_TR("PROPOSAL PREVIEW")).c_str());
    if (proposals.creator_active) {
        ImGui::TextDisabled("%s", AF_TR("Changes are recorded in the proposal draft. Save it from the review panel.").c_str());
    } else {
        ImGui::TextDisabled("%s", AF_TR("This is a preview. The project model has not been changed.").c_str());
    }
    ImGui::SameLine();
    if (proposals.creator_active) {
        const int op_count = static_cast<int>(proposals.ActiveOperationCount());
        ImGui::TextDisabled("%s", ui::i18n::trnf("{0} operation", "{0} operations", op_count, op_count).c_str());
        ImGui::SameLine();
    } else if (!proposals.preview_id.empty()) {
        if (ImGui::Button(AF_TR("Edit Proposal").c_str()) && callbacks.edit_proposal_by_id) {
            callbacks.edit_proposal_by_id(proposals.preview_id);
        }
        ImGui::SameLine();
    }
    const std::string exit_label = proposals.creator_active ? AF_TR("Discard Draft") : AF_TR("Exit Preview");
    if (ImGui::Button(exit_label.c_str())) {
        const bool was_creator = proposals.creator_active;
        if (callbacks.exit_proposal_canvas)
            callbacks.exit_proposal_canvas(was_creator);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void RenderGsnCanvasTab(AppRuntimeState& state, ui::UiState& ui_state, const WorkbenchAreaCallbacks& callbacks) {
    ui_state.center_view = ui::CenterView::GsnCanvas;
    if (state.IsProposalCanvasActive())
        RenderProposalBanner(state, callbacks);

    ui::ElementContextActions actions;
    if (state.proposal_controller->preview_active) {
        actions = ui::ElementContextActions{};
    } else if (state.proposal_controller->creator_active) {
        actions = MakeProposalContextActions(callbacks);
    } else {
        actions = MakeCanvasContextActions(callbacks);
    }
    const parser::AssuranceCase* visible_case =
        state.IsProposalCanvasActive()
            ? &state.proposal_controller->preview_model
            : (state.app_state.loaded_case.has_value() ? &state.app_state.loaded_case.value() : nullptr);
    ui_state.proposal_canvas_active = state.IsProposalCanvasActive();
    const sacm::AssuranceCasePackage* terminology_package =
        state.app_state.sacm_package.has_value() ? &state.app_state.sacm_package.value() : nullptr;
    ui::gsn::ShowGsnCanvasContent(ui_state, visible_case, actions, terminology_package);
}

void RenderArgumentPackageCanvasTab(AppRuntimeState& state,
                                    ui::UiState& ui_state,
                                    const WorkbenchAreaCallbacks& callbacks,
                                    WorkbenchState::ArgumentPackageCanvasTab& tab) {
    ui_state.center_view = ui::CenterView::GsnCanvas;
    if (!state.app_state.loaded_case.has_value() || !state.app_state.sacm_package.has_value()) {
        ImGui::TextDisabled("%s", AF_TR("No SACM argument model is loaded.").c_str());
        return;
    }

    const sacm::ArgumentPackage* argument_package = FindArgumentPackage(state.app_state.sacm_package.value(), tab);
    if (!argument_package) {
        ImGui::TextDisabled("%s", AF_TR("Argument package was not found in the loaded SACM model.").c_str());
        return;
    }

    // Audit divergence banner (when this project has an audit store and the
    // last replay didn't reproduce the SACM). Drawn before the toolbar so
    // the warning is the first thing the user sees on any package canvas.
    const bool has_audit_store = ProjectHasAuditStore(state);
    if (has_audit_store)
        RenderCanvasDivergenceBanner(state, callbacks);

    // Autosave-failure banner (sticky until dismissed or a later command
    // succeeds). Stacked above the divergence banner because an in-flight
    // autosave failure is a fresher, more actionable signal than a stale
    // replay-divergence verdict.
    RenderCanvasAutosaveErrorBanner(state);

    ArgumentPackageTabCache& cache = g_argument_package_canvas_caches[tab.key];
    const std::uint64_t current_revision = state.app_state.case_revision;
    const ui::UiState& ui_state_for_lang = ui::GetUiState();
    const bool inputs_match = cache.valid && cache.case_revision == current_revision &&
                              cache.argument_package_id == argument_package->id &&
                              cache.argument_package_gid == argument_package->gid && cache.tab_title == tab.title &&
                              cache.show_secondary_language == ui_state_for_lang.show_secondary_language &&
                              cache.secondary_language == ui_state_for_lang.active_secondary_lang;

    if (!inputs_match) {
        {
            core::perf::ScopedTimer perf_scope("app.wb.build_visible_case");
            cache.visible_case =
                BuildArgumentPackageProjection(state.app_state.loaded_case.value(), *argument_package, tab.title);
        }
        {
            core::perf::ScopedTimer perf_scope("app.wb.build_assurance_tree");
            cache.visible_tree = ui::gsn::BuildAssuranceTree(cache.visible_case, ui_state_for_lang.active_secondary_lang);
            core::ApplyTreeDisplayOrder(cache.visible_tree, state.tree_display_order);
        }
        cache.case_revision = current_revision;
        cache.argument_package_id = argument_package->id;
        cache.argument_package_gid = argument_package->gid;
        cache.tab_title = tab.title;
        cache.show_secondary_language = ui_state_for_lang.show_secondary_language;
        cache.secondary_language = ui_state_for_lang.active_secondary_lang;
        cache.renderer_seeded = false;
        cache.valid = true;
    }

    ui::ElementContextActions actions = MakeCanvasContextActions(callbacks);
    ui_state.proposal_canvas_active = false;
    ui::gsn::GsnCanvas& renderer = g_argument_package_canvas_renderers[tab.key];
    renderer.SetCaseRevision(state.app_state.case_revision);
    if (!cache.renderer_seeded) {
        core::perf::ScopedTimer perf_scope("app.wb.set_tree");
        renderer.SetTree(cache.visible_tree, ui_state.selected_element_id);
        cache.renderer_seeded = true;
    }

    const sacm::AssuranceCasePackage* terminology_package =
        state.app_state.sacm_package.has_value() ? &state.app_state.sacm_package.value() : nullptr;
    RenderArgumentPackageCanvasWithTimeline(state, ui_state, callbacks, tab, *argument_package,
                                            cache.visible_case, renderer, actions,
                                            terminology_package);
}

void RenderTerminologyPackageTab(AppRuntimeState& state, const WorkbenchAreaCallbacks& callbacks) {
    const sacm::TerminologyPackage* terminology_package = nullptr;
    if (state.app_state.sacm_package.has_value()) {
        terminology_package =
            core::FindTerminologyPackage(state.app_state.sacm_package.value(), state.terminology.selected_package_ref);
    }
    std::string delete_block_reason;
    const bool can_delete =
        terminology_package ? core::CanDeleteTerminologyPackage(*terminology_package, delete_block_reason) : false;

    ui::panels::TerminologyPackagePanelModel model;
    model.package = terminology_package;
    model.source_file_path = state.terminology.selected_package_file_path;
    model.name_buffer = state.terminology.package_name_buf;
    model.name_buffer_size = sizeof(state.terminology.package_name_buf);
    model.description_buffer = state.terminology.package_description_buf;
    model.description_buffer_size = sizeof(state.terminology.package_description_buf);
    model.can_delete = can_delete;
    model.delete_block_reason = delete_block_reason;
    model.selected_term_ref = state.terminology.selected_term_ref;
    model.selected_category_ref = state.terminology.selected_category_ref;
    model.search_buffer = state.terminology.filter_buf;
    model.search_buffer_size = sizeof(state.terminology.filter_buf);
    model.category_filter_buffer = state.terminology.category_filter_buf;
    model.category_filter_buffer_size = sizeof(state.terminology.category_filter_buf);
    if (terminology_package) {
        model.term_issues = core::ValidateTerminologyTerms(*terminology_package);
        model.term_usage_summaries =
            core::BuildTerminologyTermUsageSummaries(state.app_state.sacm_package.value(), *terminology_package);
        model.category_usage_summaries = core::BuildTerminologyCategoryUsageSummaries(*terminology_package);
    }

    ui::panels::TerminologyPackagePanelCallbacks package_callbacks;
    package_callbacks.apply_changes = callbacks.apply_terminology_package_edits;
    package_callbacks.delete_package = callbacks.delete_terminology_package;
    package_callbacks.add_term = callbacks.add_terminology_term;
    package_callbacks.select_term = callbacks.select_terminology_term;
    package_callbacks.edit_term = callbacks.edit_terminology_term_from_package;
    package_callbacks.delete_term = callbacks.delete_terminology_term;
    package_callbacks.find_term_usages = callbacks.find_terminology_term_usages;
    package_callbacks.set_category_filter = callbacks.set_terminology_category_filter;
    package_callbacks.add_category = callbacks.add_terminology_category;
    package_callbacks.select_category = callbacks.select_terminology_category;
    package_callbacks.edit_category = callbacks.edit_terminology_category;
    package_callbacks.delete_category = callbacks.delete_terminology_category;
    package_callbacks.seed_recommended_categories = callbacks.seed_recommended_terminology_categories;
    package_callbacks.list_ignored_terms = callbacks.list_ignored_terms;
    package_callbacks.restore_ignored_term = callbacks.restore_ignored_term;
    ui::panels::ShowTerminologyPackagePanel(model, package_callbacks);
}

} // namespace

void RenderWorkbenchArea(AppRuntimeState& state,
                         const frame::AppLayoutRegion& region,
                         ImGuiWindowFlags panel_flags,
                         const WorkbenchAreaCallbacks& callbacks) {
    ImGui::SetNextWindowPos(region.pos);
    ImGui::SetNextWindowSize(region.size);
    ImGui::Begin("Center View", nullptr, panel_flags | ImGuiWindowFlags_NoTitleBar);

    ui::UiState& ui_state = ui::GetUiState();
    frame::NormalizeCenterViewSelection(state, ui_state.center_view);

    if (ImGui::BeginTabBar("##center_tabs", ImGuiTabBarFlags_AutoSelectNewTabs)) {
        // If a package canvas tab activation was requested, explicitly queue focus to it.
        // AutoSelectNewTabs handles the "tab just created" case; this handles the "tab already exists" case
        // (Open Confidence Argument Tree on an ACP whose tree tab is already open).
        if (state.workbench.force_center_tab_selection && !state.workbench.active_argument_package_canvas_key.empty()) {
            const auto& tabs = state.workbench.argument_package_canvas_tabs;
            auto it = std::find_if(tabs.begin(), tabs.end(), [&](const auto& t) {
                return t.key == state.workbench.active_argument_package_canvas_key;
            });
            if (it != tabs.end()) {
                const std::string target_label =
                    it->title + "###argument_package_canvas_" + std::to_string(std::hash<std::string>{}(it->key));
                if (ImGuiTabBar* tb = ImGui::GetCurrentTabBar())
                    ImGui::TabBarQueueFocus(tb, target_label.c_str());
            }
        }

        if (state.workbench.show_gsn_tab && state.IsProposalCanvasActive()) {
            ImGuiTabItemFlags gsn_flags =
                (state.workbench.force_center_tab_selection && ui_state.center_view == ui::CenterView::GsnCanvas)
                    ? ImGuiTabItemFlags_SetSelected
                    : 0;
            if (ImGui::BeginTabItem((AF_TR("GSN Canvas") + "###gsn_canvas_tab").c_str(), nullptr, gsn_flags)) {
                state.workbench.active_argument_package_canvas_key.clear();
                RenderGsnCanvasTab(state, ui_state, callbacks);
                ImGui::EndTabItem();
            }
        }

        if (state.workbench.show_gsn_tab) {
            for (std::size_t index = 0; index < state.workbench.argument_package_canvas_tabs.size();) {
                auto& tab = state.workbench.argument_package_canvas_tabs[index];
                bool open = true;
                const bool select_tab = state.workbench.force_center_tab_selection &&
                                        state.workbench.active_argument_package_canvas_key == tab.key;
                const ImGuiTabItemFlags tab_flags = select_tab ? ImGuiTabItemFlags_SetSelected : 0;
                const std::string tab_label =
                    tab.title + "###argument_package_canvas_" + std::to_string(std::hash<std::string>{}(tab.key));
                if (ImGui::BeginTabItem(tab_label.c_str(), &open, tab_flags)) {
                    state.workbench.active_argument_package_canvas_key = tab.key;
                    RenderArgumentPackageCanvasTab(state, ui_state, callbacks, tab);
                    ImGui::EndTabItem();
                }
                if (!open) {
                    const bool removed_active = state.workbench.active_argument_package_canvas_key == tab.key;
                    g_argument_package_canvas_renderers.erase(tab.key);
                    g_argument_package_canvas_caches.erase(tab.key);
                    ForgetCanvasHistoryTab(tab.key);
                    state.workbench.argument_package_canvas_tabs.erase(
                        state.workbench.argument_package_canvas_tabs.begin() + static_cast<std::ptrdiff_t>(index));
                    if (removed_active) {
                        state.workbench.active_argument_package_canvas_key.clear();
                        state.workbench.force_center_tab_selection = true;
                    }
                    continue;
                }
                ++index;
            }
        }

        if (state.workbench.show_cse_tab) {
            ImGuiTabItemFlags cse_flags =
                (state.workbench.force_center_tab_selection && ui_state.center_view == ui::CenterView::CseRegister)
                    ? ImGuiTabItemFlags_SetSelected
                    : 0;
            if (ImGui::BeginTabItem((AF_TR("CSE Register") + "###cse_register_tab").c_str(), nullptr, cse_flags)) {
                ui_state.center_view = ui::CenterView::CseRegister;
                if (state.app_state.active_project_file_role == core::ProjectFileRole::J3377CaeRegister) {
                    ImGui::TextWrapped(
                        "%s",
                        ui::i18n::trf("J3377 CAE register file: {0}",
                                      state.app_state.active_project_file_path.string())
                            .c_str());
                    ImGui::TextDisabled(
                        "%s", AF_TR("Editable CAE register content will be implemented in a later workflow.").c_str());
                    ImGui::Separator();
                }
                ui::ShowCseRegisterView();
                ImGui::EndTabItem();
            }
        }

        if (state.workbench.show_evidence_tab) {
            ImGuiTabItemFlags evidence_flags =
                (state.workbench.force_center_tab_selection && ui_state.center_view == ui::CenterView::EvidenceRegister)
                    ? ImGuiTabItemFlags_SetSelected
                    : 0;
            if (ImGui::BeginTabItem((AF_TR("Evidence Register") + "###evidence_register_tab").c_str(), nullptr,
                                    evidence_flags)) {
                ui_state.center_view = ui::CenterView::EvidenceRegister;
                if (state.app_state.active_project_file_role == core::ProjectFileRole::EvidenceRegister) {
                    ImGui::TextWrapped(
                        "%s",
                        ui::i18n::trf("Evidence register file: {0}",
                                      state.app_state.active_project_file_path.string())
                            .c_str());
                    ImGui::TextDisabled(
                        "%s",
                        AF_TR("Editable evidence register content will be implemented in a later workflow.").c_str());
                    ImGui::Separator();
                }
                ui::ShowEvidenceRegisterView();
                ImGui::EndTabItem();
            }
        }

        if (state.workbench.show_package_details_tab) {
            ImGuiTabItemFlags package_flags =
                (state.workbench.force_center_tab_selection && ui_state.center_view == ui::CenterView::PackageDetails)
                    ? ImGuiTabItemFlags_SetSelected
                    : 0;
            if (ImGui::BeginTabItem((AF_TR("Package Details") + "###package_details_tab").c_str(), nullptr,
                                    package_flags)) {
                ui_state.center_view = ui::CenterView::PackageDetails;
                ui::panels::ShowPackageDetailsPanel(state.selected_package_node ? &state.selected_package_node.value()
                                                                                : nullptr,
                                                    state.selected_package_file_path);
                ImGui::EndTabItem();
            }
        }

        if (state.workbench.show_terminology_package_tab) {
            ImGuiTabItemFlags terminology_flags = (state.workbench.force_center_tab_selection &&
                                                   ui_state.center_view == ui::CenterView::TerminologyPackage)
                                                      ? ImGuiTabItemFlags_SetSelected
                                                      : 0;
            if (ImGui::BeginTabItem((AF_TR("Terminology Package") + "###terminology_package_tab").c_str(), nullptr,
                                    terminology_flags)) {
                ui_state.center_view = ui::CenterView::TerminologyPackage;
                RenderTerminologyPackageTab(state, callbacks);
                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
        state.workbench.force_center_tab_selection = false;
    }

    ImGui::End();
}

} // namespace app::areas
