#include "app/areas/workbench_area.h"

#include "app/actions/terminology_actions_internal.h"
#include "app/app_runtime_state.h"
#include "app/areas/canvas_history_overlay.h"
#include "app/frame/app_layout_regions.h"
#include "app/frame/app_shell.h"
#include "core/argument_package_projection.h"
#include "core/perf/frame_profiler.h"
#include "core/project_summary.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/gsn/gsn_canvas_renderer.h"
#include "ui/i18n/localization.h"
#include "ui/panels/package_details_panel.h"
#include "ui/panels/project_overview_panel.h"
#include "ui/panels/terminology_package_panel.h"
#include "ui/register_views.h"
#include "legacy_sacm/sacm_model.h"
#include "ui/theme.h"
#include "ui/ui_state.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
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
    // Staging is deliberately not a model mutation, so it never moves
    // `case_revision`. Without this the first build that included an agent's
    // preview would be cached and never refreshed, and the canvas would freeze
    // on whatever the change set happened to contain at that moment.
    std::uint64_t change_set_revision = ~0ull;
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

// The renderer keys its own caches on one revision number, and the canvas has
// two inputs that move independently: the committed model, and the change set an
// agent is building against it. Combined so either one advancing invalidates.
std::uint64_t CombineRevisions(std::uint64_t case_revision, std::uint64_t change_set_revision) {
    return case_revision * 1000003ull + change_set_revision;
}

const sacm::ArgumentPackage* FindArgumentPackage(const sacm::AssuranceCasePackage& package,
                                                 const WorkbenchState::ArgumentPackageCanvasTab& tab) {
    return core::FindArgumentPackageByIdentity(package, tab.package_id, tab.package_gid);
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
        ImGui::TextDisabled(
            "%s", AF_TR("Changes are recorded in the proposal draft. Save it from the review panel.").c_str());
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
    else
        // The same banner every package tab shows, for the same reason: this
        // canvas renders the mode-dependent draft view too, and it was the one
        // surface where "Changes only" could be active with no badge, no mode
        // buttons, and no way back.
        RenderWorkingDraftBanner(state, callbacks);

    ui::ElementContextActions actions;
    if (state.proposal_controller->preview_active) {
        actions = ui::ElementContextActions{};
    } else if (state.proposal_controller->creator_active) {
        actions = MakeProposalContextActions(callbacks);
    } else {
        actions = MakeCanvasContextActions(callbacks);
    }
    // The working argument, not the accepted one, unless a proposal canvas has
    // taken the surface over. `draft_canvas_view` is the frame's single answer
    // to "which argument is on screen"; falling back to the accepted case covers
    // the first frame, before it has been published.
    const parser::AssuranceCase* accepted_or_working =
        state.draft_canvas_view != nullptr
            ? state.draft_canvas_view
            : (state.app_state.loaded_case.has_value() ? &state.app_state.loaded_case.value() : nullptr);
    const parser::AssuranceCase* visible_case =
        state.IsProposalCanvasActive() ? &state.proposal_controller->preview_model : accepted_or_working;
    ui_state.proposal_canvas_active = state.IsProposalCanvasActive();
    // The working argument's glossary (ADR 0016): the canvas draws the draft,
    // so the terms it detects in the draft's claims have to be the draft's.
    const sacm::AssuranceCasePackage* terminology_package = state.WorkingPackage();
    ui::gsn::ShowGsnCanvasContent(ui_state, visible_case, actions, terminology_package);
}

void RenderArgumentPackageCanvasTab(AppRuntimeState& state,
                                    ui::UiState& ui_state,
                                    const WorkbenchAreaCallbacks& callbacks,
                                    WorkbenchState::ArgumentPackageCanvasTab& tab) {
    ui_state.center_view = ui::CenterView::GsnCanvas;
    if (!state.app_state.loaded_case.has_value() || !state.app_state.has_projected_package()) {
        ImGui::TextDisabled("%s", AF_TR("No SACM argument model is loaded.").c_str());
        return;
    }

    const sacm::ArgumentPackage* argument_package = FindArgumentPackage(state.app_state.projected_package(), tab);
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

    // Above the canvas on every package tab, because the canvas below it may be
    // drawing claims no human has accepted and the user has to be able to tell.
    RenderWorkingDraftBanner(state, callbacks);

    ArgumentPackageTabCache& cache = g_argument_package_canvas_caches[tab.key];
    // The cache content is rebuilt from `state.draft_canvas_view`, so it is
    // keyed on the stamp published with that view -- never on the live case
    // revision, draft revision or view mode. The live values can move between
    // the frame-start publish and this check (the banner's mode buttons render
    // above this line, and a completed AI review stages draft groups in the
    // same frame's poll); keying on them records the new key against the
    // previous frame's view, and the tab then draws the argument from before
    // the change until the next key change. With the published stamp the
    // rebuild happens one frame later, from a view that matches it.
    // The change-set revision stays live because the agent bridge only mutates
    // change sets before the publish point in the frame.
    const std::uint64_t current_revision = state.draft_canvas_view_case_revision;
    const std::uint64_t change_set_revision = state.agent_change_sets.revision();
    const std::uint64_t draft_revision =
        state.draft_canvas_view_draft_revision * 8u + static_cast<std::uint64_t>(state.draft_canvas_view_mode);
    const ui::UiState& ui_state_for_lang = ui::GetUiState();
    const bool inputs_match = cache.valid && cache.case_revision == current_revision &&
                              cache.change_set_revision == CombineRevisions(change_set_revision, draft_revision) &&
                              cache.argument_package_id == argument_package->id &&
                              cache.argument_package_gid == argument_package->gid && cache.tab_title == tab.title &&
                              cache.show_secondary_language == ui_state_for_lang.show_secondary_language &&
                              cache.secondary_language == ui_state_for_lang.active_secondary_lang;

    // A draft has to be projected as a *preview*, even with no change set open.
    //
    // The package canvas filters by SACM package ownership, and a proposed
    // element belongs to no package at all -- so the plain ownership filter
    // drops every one of them and draws the accepted argument back, which from
    // the outside is indistinguishable from the add having done nothing. That is
    // exactly what "new elements cannot be added" looked like.
    std::optional<parser::AssuranceCase> draft_preview_model;
    const core::drafts::DraftWorkspace* live_draft = state.draft_workspace.workspace();
    // A document-backed draft belongs to no change group at all -- the user's own
    // edits go straight into the document -- so asking the workspace alone would
    // drop every element such a draft adds and draw the accepted argument back.
    const bool draft_has_changes =
        state.DraftDocumentHasChanges() || (live_draft != nullptr && live_draft->has_active_groups());
    // Gated on the draft itself, not on the decoration cache: the cache is
    // refreshed by the derived-view rebuild, which can lag or skip a frame,
    // and an empty `draft_added_ids` then dropped every proposed element from
    // the package projection -- the add looked like it did nothing.
    if (!state.agent_preview_case.has_value() && draft_has_changes && state.draft_canvas_view != nullptr) {
        draft_preview_model = *state.draft_canvas_view;
    }
    std::vector<std::string> preview_added_ids = state.agent_preview_added_ids;
    preview_added_ids.insert(preview_added_ids.end(), state.draft_added_ids.begin(), state.draft_added_ids.end());

    if (!inputs_match) {
        {
            core::perf::ScopedTimer perf_scope("app.wb.build_visible_case");
            cache.visible_case = BuildArgumentPackageCanvasCase(
                state.draft_canvas_view != nullptr ? *state.draft_canvas_view : state.app_state.loaded_case.value(),
                state.agent_preview_case.has_value() ? state.agent_preview_case : draft_preview_model,
                preview_added_ids,
                state.app_state.projected_package(),
                *argument_package,
                tab.title);
        }
        {
            core::perf::ScopedTimer perf_scope("app.wb.build_assurance_tree");
            cache.visible_tree =
                ui::gsn::BuildAssuranceTree(cache.visible_case, ui_state_for_lang.active_secondary_lang);
            core::ApplyTreeDisplayOrder(cache.visible_tree, state.tree_display_order);
        }
        cache.case_revision = current_revision;
        cache.change_set_revision = CombineRevisions(change_set_revision, draft_revision);
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
    // The renderer caches (ACP targets) key on this revision together with the
    // case pointer it is given, and it is given `cache.visible_case` -- so the
    // revision is the cache's own recorded identity, which moves exactly when
    // the visible case is rebuilt. The live revisions move at other times.
    renderer.SetCaseRevision(CombineRevisions(cache.case_revision, cache.change_set_revision));
    if (!cache.renderer_seeded) {
        core::perf::ScopedTimer perf_scope("app.wb.set_tree");
        renderer.SetTree(cache.visible_tree, ui_state.selected_element_id);
        cache.renderer_seeded = true;
    }

    // Consume the toolbar's fit request against whichever canvas is actually
    // rendering, so it follows the active tab without the toolbar needing to
    // know which renderer that is.
    if (ui_state.fit_canvas_pending) {
        ui_state.fit_canvas_pending = false;
        renderer.RequestFocusOnIds({}, /*fit_all_fallback=*/true);
    }

    // The working argument's glossary (ADR 0016): the canvas draws the draft,
    // so the terms it detects in the draft's claims have to be the draft's.
    const sacm::AssuranceCasePackage* terminology_package = state.WorkingPackage();
    RenderArgumentPackageCanvasWithTimeline(
        state, ui_state, callbacks, tab, *argument_package, cache.visible_case, renderer, actions, terminology_package);
}

// The glossary rows the working draft added or changed, read off the same
// comparison the banner and the canvas decorations use, so the tab cannot claim
// a draft change the banner does not count. Only terms and categories: the tab
// has rows for nothing else. A removed row has no row to badge and is counted
// in the notice.
void CollectGlossaryDraftMarks(AppRuntimeState& state, ui::panels::TerminologyPackagePanelModel& model) {
    if (!state.DraftDocumentHasChanges())
        return;
    const core::drafts::DraftDocumentDiff& diff = state.DraftDocumentChanges();
    // Indexed once: the draft view is the whole argument, and this runs on every
    // frame the tab is open. Removed elements are absent from the draft view and
    // live in the (small) removed list, which is searched as it is.
    std::unordered_map<std::string, const core::SacmElement*> draft_elements_by_id;
    draft_elements_by_id.reserve(state.draft_document_view.elements.size());
    for (const core::SacmElement& element : state.draft_document_view.elements)
        draft_elements_by_id.emplace(element.id, &element);
    int added = 0;
    int changed = 0;
    int removed = 0;
    for (const core::drafts::DraftDocumentChange& change : diff.changes) {
        const core::SacmElement* element = nullptr;
        if (change.change == core::drafts::DraftElementChange::Removed) {
            for (const core::SacmElement& candidate : diff.removed) {
                if (candidate.id == change.element_id) {
                    element = &candidate;
                    break;
                }
            }
        } else if (const auto found = draft_elements_by_id.find(change.element_id);
                   found != draft_elements_by_id.end()) {
            element = found->second;
        }
        if (element == nullptr || (element->type != "term" && element->type != "category"))
            continue;
        switch (change.change) {
        case core::drafts::DraftElementChange::Added:
            ++added;
            break;
        case core::drafts::DraftElementChange::Modified:
            ++changed;
            break;
        case core::drafts::DraftElementChange::Removed:
            ++removed;
            break;
        case core::drafts::DraftElementChange::Unchanged:
            break;
        }
        if (change.change != core::drafts::DraftElementChange::Removed)
            model.draft_marks.push_back(
                ui::panels::TerminologyDraftMark{change.element_id, change.change, change.fields});
    }
    if (added + changed + removed == 0)
        return;
    model.working_draft_notice =
        ui::i18n::trf("Working draft: {0} added, {1} changed, {2} removed in this glossary. Accept the draft on the "
                      "argument canvas to make them part of the case.",
                      std::to_string(added),
                      std::to_string(changed),
                      std::to_string(removed));
}

} // namespace

ui::panels::TerminologyPackagePanelModel BuildTerminologyPackagePanelModel(AppRuntimeState& state) {
    // The working argument's package (ADR 0016): the draft's glossary while a
    // draft differs from the accepted argument. A term an AI client has just
    // defined is in no other package, and this tab is where a human reads the
    // definition before deciding whether to accept it -- reading the accepted
    // package here left the tab showing the glossary from before the draft,
    // which looked like the client's change had not happened.
    const sacm::AssuranceCasePackage* package = state.WorkingPackage();
    const sacm::TerminologyPackage* terminology_package =
        package != nullptr ? core::FindTerminologyPackage(*package, state.terminology.selected_package_ref) : nullptr;
    // A glossary the draft itself created has no accepted counterpart for the
    // explorer to have selected, so the draft's only glossary stands in.
    if (terminology_package == nullptr && package != nullptr && state.DraftDocumentHasChanges() &&
        package->terminologyPackages.size() == 1u) {
        terminology_package = &package->terminologyPackages.front();
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
        model.term_usage_summaries = core::BuildTerminologyTermUsageSummaries(*package, *terminology_package);
        model.category_usage_summaries = core::BuildTerminologyCategoryUsageSummaries(*terminology_package);
    }

    // The same rules the actions enforce, stated on the tab so the user learns
    // them before the first click rather than from a refused one: term and
    // category edits go into the draft; the package's own fields and a category
    // delete wait for the draft to be accepted or discarded.
    std::string locked_reason;
    if (actions::detail::AcceptedGlossaryEditBlockedByDraft(
            state, AF_TR("Editing the package or deleting a category"), locked_reason)) {
        model.draft_edit_notice = actions::detail::GlossaryDraftEditNotice();
        model.package_edits_locked = true;
        // The helper's wording, already translated: one sentence for the tab and
        // the status line, so the two cannot drift apart.
        model.package_edits_locked_reason = locked_reason;
    }
    CollectGlossaryDraftMarks(state, model);
    return model;
}

namespace {

void RenderTerminologyPackageTab(AppRuntimeState& state, const WorkbenchAreaCallbacks& callbacks) {
    const ui::panels::TerminologyPackagePanelModel model = BuildTerminologyPackagePanelModel(state);

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

// Renders one of the two register tables against the controller's store and
// marks it dirty when the user edits a cell, so the next project save writes it.
//
// A register file that failed to load disables editing instead of accepting
// keystrokes: the controller refuses to save over a file it could not read, so
// anything typed here would be discarded on close without a word.
void RenderRegisterTable(AppRuntimeState& state,
                         const std::function<bool(core::registers::RegisterStore&)>& show_table) {
    controllers::RegisterController& register_assessments = *state.register_controller;
    const bool storage_failed = register_assessments.HasStorageError();
    if (storage_failed) {
        ImGui::TextWrapped("%s",
                           ui::i18n::trf("Register assessments could not be loaded, so edits cannot be saved: {0}",
                                         register_assessments.StorageError())
                               .c_str());
        ImGui::Separator();
    }

    ImGui::BeginDisabled(storage_failed);
    if (show_table(register_assessments.MutableStore()))
        register_assessments.MarkDirty();
    ImGui::EndDisabled();
}

ui::panels::ProjectOverviewPanelModel BuildProjectOverviewPanelModel(AppRuntimeState& state) {
    ui::panels::ProjectOverviewPanelModel model;
    model.project = state.app_state.current_project.has_value() ? &state.app_state.current_project.value() : nullptr;
    const parser::AssuranceCase* loaded_case =
        state.app_state.loaded_case.has_value() ? &state.app_state.loaded_case.value() : nullptr;
    std::vector<core::reviews::ProposalValidityResult> proposal_validities;
    for (const core::reviews::ReviewProposalSummary& proposal :
         state.proposal_controller->manager.ListProposals(loaded_case)) {
        proposal_validities.push_back(proposal.validity);
    }
    model.summary = core::BuildProjectSummary(model.project,
                                              loaded_case,
                                              loaded_case ? &state.current_tree : nullptr,
                                              state.problems_manager.GetProblems(),
                                              state.review_controller->Items(),
                                              proposal_validities);
    if (loaded_case)
        model.active_case_name = loaded_case->name;
    return model;
}

ui::panels::ProjectOverviewPanelCallbacks MakeProjectOverviewCallbacks(AppRuntimeState& state) {
    ui::panels::ProjectOverviewPanelCallbacks callbacks;
    callbacks.open_arguments = [&state]() {
        state.workbench.show_gsn_tab = true;
        ui::GetUiState().center_view = ui::CenterView::GsnCanvas;
        state.workbench.force_center_tab_selection = true;
    };
    callbacks.open_evidence = [&state]() {
        state.workbench.show_evidence_tab = true;
        ui::GetUiState().center_view = ui::CenterView::EvidenceRegister;
        state.workbench.force_center_tab_selection = true;
    };
    callbacks.open_reviews = [&state]() { state.workbench.focus_review_tab = true; };
    callbacks.open_conformance = [&state]() {
        state.workbench.show_cse_tab = true;
        ui::GetUiState().center_view = ui::CenterView::CseRegister;
        state.workbench.force_center_tab_selection = true;
    };
    return callbacks;
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

    // The view the caller asked for, read once before any tab renders. The tab
    // that is visible this frame writes its own view back into `center_view`
    // while it renders, so reading the field per tab reports the current tab,
    // not the requested one, and every tab later in the bar then concludes it
    // was not asked for. That is how a register clicked in the explorer opened
    // only while its tab did not exist yet: a new tab is picked up by
    // AutoSelectNewTabs, an existing one needs the flag below.
    const bool select_requested = state.workbench.force_center_tab_selection;
    const ui::CenterView requested_view = ui_state.center_view;

    if (ImGui::BeginTabBar("##center_tabs", ImGuiTabBarFlags_AutoSelectNewTabs)) {
        // If a package canvas tab activation was requested, explicitly queue focus to it.
        // AutoSelectNewTabs handles the "tab just created" case; this handles the "tab already exists" case
        // (Open Confidence Argument Tree on an ACP whose tree tab is already open).
        if (select_requested && requested_view == ui::CenterView::GsnCanvas &&
            !state.workbench.active_argument_package_canvas_key.empty()) {
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

        if (state.workbench.show_overview_tab) {
            const ImGuiTabItemFlags overview_flags =
                (select_requested && requested_view == ui::CenterView::ProjectOverview) ? ImGuiTabItemFlags_SetSelected
                                                                                        : 0;
            if (ImGui::BeginTabItem(
                    (AF_TR("Project Overview") + "###project_overview_tab").c_str(), nullptr, overview_flags)) {
                ui_state.center_view = ui::CenterView::ProjectOverview;
                ui::panels::ShowProjectOverviewPanel(BuildProjectOverviewPanelModel(state),
                                                     MakeProjectOverviewCallbacks(state));
                ImGui::EndTabItem();
            }
        }

        if (state.workbench.show_gsn_tab && state.IsProposalCanvasActive()) {
            ImGuiTabItemFlags gsn_flags =
                (select_requested && requested_view == ui::CenterView::GsnCanvas) ? ImGuiTabItemFlags_SetSelected : 0;
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
                const bool select_tab = select_requested && requested_view == ui::CenterView::GsnCanvas &&
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
                (select_requested && requested_view == ui::CenterView::CseRegister) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem((AF_TR("CSE Register") + "###cse_register_tab").c_str(), nullptr, cse_flags)) {
                ui_state.center_view = ui::CenterView::CseRegister;
                if (state.app_state.active_project_file_role == core::ProjectFileRole::J3377CaeRegister) {
                    ImGui::TextWrapped(
                        "%s",
                        ui::i18n::trf("J3377 CAE register file: {0}", state.app_state.active_project_file_path.string())
                            .c_str());
                    ImGui::TextDisabled(
                        "%s", AF_TR("Editable CAE register content will be implemented in a later workflow.").c_str());
                    ImGui::Separator();
                }
                ui::CseRegisterCallbacks cse_callbacks;
                cse_callbacks.set_attribute = callbacks.set_cse_attribute;
                cse_callbacks.migrate_assessments = callbacks.migrate_cse_assessments;
                cse_callbacks.locate = callbacks.locate_element;
                RenderRegisterTable(state, [&cse_callbacks](core::registers::RegisterStore& store) {
                    return ui::ShowCseRegisterView(store, cse_callbacks);
                });
                ImGui::EndTabItem();
            }
        }

        if (state.workbench.show_evidence_tab) {
            ImGuiTabItemFlags evidence_flags = (select_requested && requested_view == ui::CenterView::EvidenceRegister)
                                                   ? ImGuiTabItemFlags_SetSelected
                                                   : 0;
            if (ImGui::BeginTabItem(
                    (AF_TR("Evidence Register") + "###evidence_register_tab").c_str(), nullptr, evidence_flags)) {
                ui_state.center_view = ui::CenterView::EvidenceRegister;
                if (state.app_state.active_project_file_role == core::ProjectFileRole::EvidenceRegister) {
                    ImGui::TextWrapped(
                        "%s",
                        ui::i18n::trf("Evidence register file: {0}", state.app_state.active_project_file_path.string())
                            .c_str());
                    ImGui::TextDisabled(
                        "%s",
                        AF_TR("Editable evidence register content will be implemented in a later workflow.").c_str());
                    ImGui::Separator();
                }
                ui::EvidenceRegisterCallbacks register_callbacks;
                register_callbacks.locate = callbacks.locate_element;
                register_callbacks.remove = callbacks.remove_evidence;
                register_callbacks.set_location = callbacks.set_evidence_location;
                register_callbacks.open_location = callbacks.open_evidence_location;
                register_callbacks.set_attribute = callbacks.set_evidence_attribute;
                register_callbacks.migrate_assessments = callbacks.migrate_evidence_assessments;
                register_callbacks.browse_location = callbacks.browse_evidence_location;
                register_callbacks.create_evidence = callbacks.create_evidence;
                register_callbacks.link_evidence = callbacks.link_evidence;
                register_callbacks.unlink_evidence = callbacks.unlink_evidence;
                RenderRegisterTable(state, [&register_callbacks](core::registers::RegisterStore& store) {
                    return ui::ShowEvidenceRegisterView(store, register_callbacks);
                });
                ImGui::EndTabItem();
            }
        }

        if (state.workbench.show_package_details_tab) {
            ImGuiTabItemFlags package_flags = (select_requested && requested_view == ui::CenterView::PackageDetails)
                                                  ? ImGuiTabItemFlags_SetSelected
                                                  : 0;
            if (ImGui::BeginTabItem(
                    (AF_TR("Package Details") + "###package_details_tab").c_str(), nullptr, package_flags)) {
                ui_state.center_view = ui::CenterView::PackageDetails;
                ui::panels::ShowPackageDetailsPanel(state.selected_package_node ? &state.selected_package_node.value()
                                                                                : nullptr,
                                                    state.selected_package_file_path);
                ImGui::EndTabItem();
            }
        }

        if (state.workbench.show_terminology_package_tab) {
            ImGuiTabItemFlags terminology_flags =
                (select_requested && requested_view == ui::CenterView::TerminologyPackage)
                    ? ImGuiTabItemFlags_SetSelected
                    : 0;
            if (ImGui::BeginTabItem((AF_TR("Terminology Package") + "###terminology_package_tab").c_str(),
                                    nullptr,
                                    terminology_flags)) {
                ui_state.center_view = ui::CenterView::TerminologyPackage;
                RenderTerminologyPackageTab(state, callbacks);
                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
        // Only the request this frame acted on is consumed. A tab's own content
        // can raise one -- the Project Overview's buttons and the evidence
        // register's "Show in argument" run inside their tabs, after the tabs
        // before them have rendered -- and that request has to survive to the
        // next frame to be seen by every tab.
        if (select_requested) {
            state.workbench.force_center_tab_selection = false;
            // ImGui switches to the selected tab on the next frame; until then the
            // outgoing tab has written itself into `center_view`. Keep the request,
            // so the explorer highlights the row that was clicked rather than
            // flashing the old one for a frame.
            ui_state.center_view = requested_view;
        }
    }

    ImGui::End();
}

parser::AssuranceCase BuildArgumentPackageCanvasCase(const parser::AssuranceCase& committed,
                                                     const std::optional<parser::AssuranceCase>& agent_preview,
                                                     const std::vector<std::string>& agent_preview_added_ids,
                                                     const sacm::AssuranceCasePackage& package,
                                                     const sacm::ArgumentPackage& argument_package,
                                                     std::string_view fallback_title) {
    // While an agent has a change set open against the argument on screen, the
    // canvas draws its preview -- the argument as it would be if accepted --
    // rather than the committed model. The preview needs its own projection
    // because a staged element is in no SACM package at all; the plain
    // ownership filter would drop every one of them and draw the committed
    // argument back, which is indistinguishable from not having asked.
    if (agent_preview.has_value()) {
        return core::BuildArgumentPackagePreviewProjection(
            agent_preview.value(), package, argument_package, agent_preview_added_ids, fallback_title);
    }
    return core::BuildArgumentPackageProjection(committed, argument_package, fallback_title);
}

} // namespace app::areas
