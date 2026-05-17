#include "app/areas/workbench_area.h"

#include "app/app_runtime_state.h"
#include "app/frame/app_layout_regions.h"
#include "app/frame/app_shell.h"
#include "core/problems/problem_attention.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/gsn/gsn_canvas_renderer.h"
#include "ui/localization.h"
#include "ui/panels/package_details_panel.h"
#include "ui/panels/terminology_package_panel.h"
#include "ui/register_views.h"
#include "sacm/sacm_model.h"
#include "ui/theme.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace app::areas {
namespace {

std::unordered_map<std::string, ui::gsn::GsnCanvas> g_argument_package_canvas_renderers;

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
    auto found = std::find_if(package.argumentPackages.begin(), package.argumentPackages.end(), [&](const auto& pkg) {
        const bool id_matches = !tab.package_id.empty() && pkg.id == tab.package_id;
        const bool gid_matches = !tab.package_gid.empty() && pkg.gid == tab.package_gid;
        return id_matches || gid_matches;
    });
    return found == package.argumentPackages.end() ? nullptr : &*found;
}

parser::AssuranceCase BuildArgumentPackageProjection(const parser::AssuranceCase& source_model,
                                                     const sacm::ArgumentPackage& argument_package,
                                                     const std::string& fallback_name) {
    std::unordered_set<std::string> element_ids;
    std::unordered_set<std::string> element_gids;
    for (const sacm::Claim& claim : argument_package.claims)
        AddElementIdentity(claim, element_ids, element_gids);
    for (const sacm::ArgumentReasoning& reasoning : argument_package.argumentReasonings)
        AddElementIdentity(reasoning, element_ids, element_gids);
    for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences)
        AddElementIdentity(artifact_reference, element_ids, element_gids);
    for (const sacm::AssertedInference& inference : argument_package.assertedInferences)
        AddElementIdentity(inference, element_ids, element_gids);
    for (const sacm::AssertedContext& context : argument_package.assertedContexts)
        AddElementIdentity(context, element_ids, element_gids);
    for (const sacm::AssertedEvidence& evidence : argument_package.assertedEvidences)
        AddElementIdentity(evidence, element_ids, element_gids);

    parser::AssuranceCase projection;
    projection.id = argument_package.id.empty() ? source_model.id : argument_package.id;
    projection.name = argument_package.name.empty() ? fallback_name : argument_package.name;
    projection.description = argument_package.description;
    for (const parser::SacmElement& element : source_model.elements) {
        if (element_ids.count(element.id) > 0 || element_gids.count(element.gid) > 0)
            projection.elements.push_back(element);
    }
    for (const parser::AcpRecord& acp : source_model.acps) {
        if (element_ids.count(acp.target_id) > 0 || element_gids.count(acp.target_id) > 0)
            projection.acps.push_back(acp);
    }
    return projection;
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
    ImGui::TextUnformatted(proposals.creator_active ? "PROPOSAL CREATOR" : "PROPOSAL PREVIEW");
    if (proposals.creator_active) {
        ImGui::TextDisabled("Changes are recorded in the proposal draft. Save it from the review panel.");
    } else {
        ImGui::TextDisabled("This is a preview. The project model has not been changed.");
    }
    ImGui::SameLine();
    if (proposals.creator_active) {
        ImGui::TextDisabled("%d operation(s)", static_cast<int>(proposals.ActiveOperationCount()));
        ImGui::SameLine();
    } else if (!proposals.preview_id.empty()) {
        if (ImGui::Button("Edit Proposal") && callbacks.edit_proposal_by_id) {
            callbacks.edit_proposal_by_id(proposals.preview_id);
        }
        ImGui::SameLine();
    }
    const char* exit_label = proposals.creator_active ? "Discard Draft" : "Exit Preview";
    if (ImGui::Button(exit_label)) {
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
    ui_state.attention_element_ids = core::CollectAttentionElementIds(state.problems_manager.GetProblems());
    if (callbacks.sync_review_visual_states)
        callbacks.sync_review_visual_states();
    const sacm::AssuranceCasePackage* terminology_package =
        state.app_state.sacm_package.has_value() ? &state.app_state.sacm_package.value() : nullptr;
    ui::gsn::ShowGsnCanvasContent(ui_state, visible_case, actions, terminology_package);
}

void RenderArgumentPackageCanvasTab(AppRuntimeState& state,
                                    ui::UiState& ui_state,
                                    const WorkbenchAreaCallbacks& callbacks,
                                    const WorkbenchState::ArgumentPackageCanvasTab& tab) {
    ui_state.center_view = ui::CenterView::GsnCanvas;
    if (!state.app_state.loaded_case.has_value() || !state.app_state.sacm_package.has_value()) {
        ImGui::TextDisabled("No SACM argument model is loaded.");
        return;
    }

    const sacm::ArgumentPackage* argument_package = FindArgumentPackage(state.app_state.sacm_package.value(), tab);
    if (!argument_package) {
        ImGui::TextDisabled("Argument package was not found in the loaded SACM model.");
        return;
    }

    parser::AssuranceCase visible_case =
        BuildArgumentPackageProjection(state.app_state.loaded_case.value(), *argument_package, tab.title);
    core::AssuranceTree visible_tree = ui::gsn::BuildAssuranceTree(visible_case);
    core::ApplyTreeDisplayOrder(visible_tree, state.tree_display_order);

    ui::ElementContextActions actions = MakeCanvasContextActions(callbacks);
    ui_state.proposal_canvas_active = false;
    ui_state.attention_element_ids = core::CollectAttentionElementIds(state.problems_manager.GetProblems());
    if (callbacks.sync_review_visual_states)
        callbacks.sync_review_visual_states();
    ui::gsn::GsnCanvas& renderer = g_argument_package_canvas_renderers[tab.key];
    renderer.SetTree(visible_tree);
    ui::gsn::ShowGsnCanvasContentWithRenderer(renderer,
                                             ui_state,
                                             &visible_case,
                                             actions,
                                             &state.app_state.sacm_package.value());
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

    if (ImGui::BeginTabBar("##center_tabs")) {
        if (state.workbench.show_gsn_tab && state.IsProposalCanvasActive()) {
            ImGuiTabItemFlags gsn_flags =
            (state.workbench.force_center_tab_selection && ui_state.center_view == ui::CenterView::GsnCanvas)
                    ? ImGuiTabItemFlags_SetSelected
                    : 0;
            if (ImGui::BeginTabItem(ui::Tr(ui::MessageId::GsnCanvas), nullptr, gsn_flags)) {
                state.workbench.active_argument_package_canvas_key.clear();
                RenderGsnCanvasTab(state, ui_state, callbacks);
                ImGui::EndTabItem();
            }
        }

        if (state.workbench.show_gsn_tab) {
            for (std::size_t index = 0; index < state.workbench.argument_package_canvas_tabs.size();) {
                const auto& tab = state.workbench.argument_package_canvas_tabs[index];
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
            if (ImGui::BeginTabItem(ui::Tr(ui::MessageId::CseRegister), nullptr, cse_flags)) {
                ui_state.center_view = ui::CenterView::CseRegister;
                if (state.app_state.active_project_file_role == core::ProjectFileRole::J3377CaeRegister) {
                    ImGui::TextWrapped("J3377 CAE register file: %s",
                                       state.app_state.active_project_file_path.string().c_str());
                    ImGui::TextDisabled("Editable CAE register content will be implemented in a later workflow.");
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
            if (ImGui::BeginTabItem(ui::Tr(ui::MessageId::EvidenceRegister), nullptr, evidence_flags)) {
                ui_state.center_view = ui::CenterView::EvidenceRegister;
                if (state.app_state.active_project_file_role == core::ProjectFileRole::EvidenceRegister) {
                    ImGui::TextWrapped("Evidence register file: %s",
                                       state.app_state.active_project_file_path.string().c_str());
                    ImGui::TextDisabled("Editable evidence register content will be implemented in a later workflow.");
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
            if (ImGui::BeginTabItem("Package Details", nullptr, package_flags)) {
                ui_state.center_view = ui::CenterView::PackageDetails;
                ui::panels::ShowPackageDetailsPanel(state.selected_package_node ? &state.selected_package_node.value()
                                                                                : nullptr,
                                                    state.selected_package_file_path);
                ImGui::EndTabItem();
            }
        }

        if (state.workbench.show_terminology_package_tab) {
            ImGuiTabItemFlags terminology_flags =
                (state.workbench.force_center_tab_selection && ui_state.center_view == ui::CenterView::TerminologyPackage)
                    ? ImGuiTabItemFlags_SetSelected
                    : 0;
            if (ImGui::BeginTabItem("Terminology Package", nullptr, terminology_flags)) {
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
