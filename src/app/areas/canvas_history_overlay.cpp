#include "app/areas/canvas_history_overlay.h"

#include "app/app_runtime_state.h"
#include "app/areas/audit_data_cache.h"
#include "app/areas/baseline_modal.h"
#include "core/argument_package_projection.h"
#include "core/audit/audit_baseline.h"
#include "core/audit/audit_diff.h"
#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_snapshot.h"
#include "core/audit/canonical_model_hash.h"
#include "core/audit/event_scope.h"
#include "core/audit/event_store.h"
#include "core/audit/history_highlights.h"
#include "core/audit/history_reconstruction.h"
#include "core/audit/timeline_model_builder.h"
#include "ui/element_context_menu.h"
#include "ui/i18n/localization.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/gsn/gsn_canvas_renderer.h"
#include "ui/timeline/timeline_widget.h"
#include "ui/theme.h"
#include "ui/ui_state.h"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace app::areas {

namespace {

// Per-tab reconstruction cache. Keyed on (project root, target sequence) for
// a single tab; invalidated when the user picks a different sequence or
// opens a different project. We keep the reconstructed `ReplayState` because
// the historical canvas needs to render against the full model.
struct ReconstructionCache {
    std::filesystem::path project_root;
    std::optional<std::uint64_t> sequence;
    std::string error;
    bool valid = false;
    bool has_state = false;
    core::audit::ReplayState state;
    core::AssuranceTree tree;
};

// Per-tab filtered-transaction cache. Recomputed when the underlying
// transactions list grows or the package identity changes.
struct FilteredTransactionsCache {
    std::size_t source_size = 0;
    std::uint64_t last_sequence = 0;
    std::string package_id;
    std::string package_gid;
    std::vector<core::audit::AuditTransaction> filtered;
    core::audit::ArgumentPackageScope scope;
    bool valid = false;
};

// All state the overlay holds for a single canvas tab.
struct CanvasHistoryState {
    ReconstructionCache reconstruction;
    FilteredTransactionsCache filtered;
    std::vector<core::audit::AuditTransaction> source_transactions;
    ui::gsn::GsnCanvas historical_renderer;
    bool historical_seeded = false;
    std::optional<std::uint64_t> historical_seeded_sequence;
    // Tracks the case_revision the live renderer was last associated with
    // a pinned-history view so we know when to re-seed the LIVE renderer
    // upon returning to live.
    std::uint64_t live_renderer_dirtied_for_revision = 0;
};

std::unordered_map<std::string, CanvasHistoryState> g_history_state_by_tab_key;

CanvasHistoryState& GetOrCreateHistoryState(const std::string& tab_key) {
    return g_history_state_by_tab_key[tab_key];
}

const std::vector<core::audit::AuditTransaction>& LoadTransactions(const core::AssuranceProject& project,
                                                                   std::string& error_out) {
    return GetCachedTransactions(project.rootPath, error_out);
}

// Walk transactions forward, growing the package scope as new child
// elements are created with parents already in scope. A transaction is
// included when any of its event ids overlaps the running scope. This is
// conservative-inclusive: it may miss top-goal create+delete pairs that
// never ended up in the LIVE model (rare), but never silently includes
// events that are unambiguously outside the package.
void RebuildFilteredTransactions(FilteredTransactionsCache& cache,
                                 const std::vector<core::audit::AuditTransaction>& source,
                                 const sacm::ArgumentPackage& argument_package) {
    const std::uint64_t last_seq = source.empty() ? 0 : source.back().transaction_sequence;
    if (cache.valid && cache.source_size == source.size() && cache.last_sequence == last_seq &&
        cache.package_id == argument_package.id && cache.package_gid == argument_package.gid) {
        return;
    }
    cache = FilteredTransactionsCache{};
    cache.source_size = source.size();
    cache.last_sequence = last_seq;
    cache.package_id = argument_package.id;
    cache.package_gid = argument_package.gid;

    core::audit::ArgumentPackageScope scope = core::audit::CollectArgumentPackageScope(argument_package);
    cache.filtered.reserve(source.size());
    for (const core::audit::AuditTransaction& tx : source) {
        if (core::audit::TransactionTouchesScope(tx, scope))
            cache.filtered.push_back(tx);
        for (const core::audit::AuditEvent& event : tx.events) {
            if (event.event_type != "CreateChildElement" || !event.payload.is_object())
                continue;
            auto parent_it = event.payload.find("parent_id");
            if (parent_it == event.payload.end() || !parent_it->is_string())
                continue;
            const std::string parent = parent_it->get<std::string>();
            const bool parent_in_scope = scope.element_ids.count(parent) > 0 || scope.element_gids.count(parent) > 0;
            if (!parent_in_scope)
                continue;
            auto add_if_string = [&](const char* key) {
                auto it = event.payload.find(key);
                if (it == event.payload.end() || !it->is_string())
                    return;
                const std::string value = it->get<std::string>();
                if (!value.empty())
                    scope.element_ids.insert(value);
            };
            add_if_string("generated_id");
            add_if_string("generated_relationship_id");
        }
    }
    cache.scope = std::move(scope);
    cache.valid = true;
}

void RefreshReconstruction(ReconstructionCache& cache,
                           CanvasHistoryState& tab_state,
                           const core::AssuranceProject& project,
                           std::uint64_t target_seq,
                           const sacm::ArgumentPackage& argument_package) {
    if (cache.valid && cache.project_root == project.rootPath && cache.sequence == target_seq) {
        return;
    }
    cache = ReconstructionCache{};
    cache.project_root = project.rootPath;
    cache.sequence = target_seq;
    tab_state.historical_seeded = false;
    tab_state.historical_seeded_sequence.reset();

    auto state = core::audit::ReconstructAtSequence(project, target_seq, argument_package.id, argument_package.gid);
    if (!state) {
        cache.error = state.error();
        cache.valid = true;
        return;
    }
    cache.tree = ui::gsn::BuildAssuranceTree(state->views.model, ui::GetUiState().active_secondary_lang);
    // The reconstructed document is not kept: the historical canvas renders from
    // the derived views. Undo is the caller that needs the document itself.
    cache.state = std::move(state->views);
    cache.has_state = true;
    cache.valid = true;
}

void RenderHistoricalCanvas(CanvasHistoryState& tab_state,
                            ui::UiState& ui_state,
                            const std::vector<core::audit::AuditTransaction>& transactions,
                            std::uint64_t target_seq,
                            const ui::gsn::CanvasOverlayButtons* overlay_buttons) {
    if (!tab_state.reconstruction.has_state) {
        if (!tab_state.reconstruction.error.empty())
            ImGui::TextDisabled("%s",
                                ui::i18n::trf("Reconstruction failed: {0}", tab_state.reconstruction.error).c_str());
        else
            ImGui::TextDisabled("%s", AF_TR("No reconstructed model to display.").c_str());
        return;
    }
    const bool sequence_changed =
        !tab_state.historical_seeded || tab_state.historical_seeded_sequence != tab_state.reconstruction.sequence;
    if (sequence_changed) {
        tab_state.historical_renderer.SetTree(tab_state.reconstruction.tree);
        tab_state.historical_seeded = true;
        tab_state.historical_seeded_sequence = tab_state.reconstruction.sequence;

        // Compute a focus set so the canvas pans to whatever the selected
        // transaction actually changed instead of leaving the user staring
        // at empty space when the historical layout shifts. Priority:
        //   1. Added or Modified ids that survive into the reconstructed
        //      state at `target_seq` (i.e. the element exists right now and
        //      can be centered).
        //   2. Fit-all fallback when the transaction is a pure deletion
        //      (the deleted element is gone from the reconstruction so the
        //      best we can do without an extra reconstruction at seq-1 is
        //      fit the entire visible package into the viewport).
        std::unordered_set<std::string> focus_ids;
        auto tx_it = std::find_if(
            transactions.begin(), transactions.end(), [target_seq](const core::audit::AuditTransaction& tx) {
                return tx.transaction_sequence == target_seq;
            });
        if (tx_it != transactions.end()) {
            const core::audit::AuditChangeSet cs = core::audit::ComputeChangeSet(*tx_it);
            const auto& model = tab_state.reconstruction.state.model;
            std::unordered_set<std::string> reachable;
            reachable.reserve(model.elements.size());
            for (const auto& el : model.elements)
                reachable.insert(el.id);
            auto consider = [&](const std::unordered_set<std::string>& src) {
                for (const std::string& id : src) {
                    if (reachable.count(id))
                        focus_ids.insert(id);
                }
            };
            consider(cs.added);
            consider(cs.modified);
        }
        // `fit_all_fallback=true` so target_seq==0 (no transaction) and
        // pure-deletion transactions still re-frame the canvas rather than
        // showing a blank area.
        tab_state.historical_renderer.RequestFocusOnIds(std::move(focus_ids), /*fit_all_fallback=*/true);
    }

    // Highlights are restricted to ids in the active package's running
    // scope so cross-package events don't tint nodes that aren't visible.
    auto highlights = core::audit::BuildHistoryHighlightsForSequence(transactions, target_seq);
    if (tab_state.filtered.valid) {
        const auto& scope = tab_state.filtered.scope;
        for (auto it = highlights.begin(); it != highlights.end();) {
            const bool in_scope = scope.element_ids.count(it->first) > 0 || scope.element_gids.count(it->first) > 0;
            if (in_scope)
                ++it;
            else
                it = highlights.erase(it);
        }
    }
    tab_state.historical_renderer.SetHistoryHighlights(std::move(highlights));

    ui::ElementContextActions readonly_actions;
    ui::gsn::ShowGsnCanvasContentWithRenderer(tab_state.historical_renderer,
                                              ui_state,
                                              &tab_state.reconstruction.state.model,
                                              readonly_actions,
                                              nullptr,
                                              overlay_buttons);
}

} // namespace

bool ProjectHasAuditStore(const AppRuntimeState& state) {
    if (!state.app_state.current_project.has_value())
        return false;
    return std::filesystem::exists(core::audit::ManifestPath(state.app_state.current_project->rootPath));
}

bool ProjectAuditLogHasTransactions(const AppRuntimeState& state) {
    if (!ProjectHasAuditStore(state))
        return false;
    const auto log = core::audit::EventLogPath(state.app_state.current_project->rootPath);
    std::error_code ec;
    if (!std::filesystem::exists(log, ec))
        return false;
    // The log is JSON-Lines; one committed transaction == at least one
    // non-empty line. File size 0 (or whitespace-only) means no transactions
    // have been recorded yet.
    const auto size = std::filesystem::file_size(log, ec);
    if (ec)
        return false;
    return size > 0;
}

void RenderCanvasDivergenceBanner(AppRuntimeState& state, const WorkbenchAreaCallbacks& callbacks) {
    if (!state.last_audit_verification.has_value() || !state.last_audit_verification->ran ||
        state.last_audit_verification->success) {
        return;
    }
    const auto& v = *state.last_audit_verification;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(80, 50, 0, 255));
    ImGui::BeginChild("##audit_warning_banner", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 7.0f), true);
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", AF_TR("Audit log divergence detected").c_str());
    ImGui::TextWrapped("%s",
                       AF_TR("The replayed audit history does not reproduce the on-disk SACM. This usually means "
                             "edits were applied through a path that did not record transactions. Pinned historical "
                             "views may be inaccurate.")
                           .c_str());
    if (!v.replayed_canonical_hash.empty() && !v.on_disk_canonical_hash.empty()) {
        ImGui::TextWrapped("%s",
                           ui::i18n::trf("replay={0}  on_disk={1}",
                                         v.replayed_canonical_hash.substr(0, 12),
                                         v.on_disk_canonical_hash.substr(0, 12))
                               .c_str());
    }
    if (ImGui::Button(AF_TR("Reconcile audit log\u2026").c_str()))
        ImGui::OpenPopup((AF_TR("Reconcile audit log") + "###reconcile_confirm").c_str());
    ImGui::SameLine();
    ImGui::TextWrapped("%s",
                       AF_TR("(archives current .af/ artifacts and rebuilds from the current SACM file)").c_str());

    // Confirmation modal. The Reconcile action is destructive in the sense
    // that the app will no longer surface the current audit history through
    // the timeline \u2014 it is moved aside, not deleted. Make the consequence
    // explicit so the user can back out.
    ImVec2 viewport_center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(viewport_center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    // Lock width to prevent the AlwaysAutoResize + GetContentRegionAvail()
    // feedback loop that grows the popup horizontally every frame. Width is
    // font-size relative so it scales with DPI.
    const float popup_width = ImGui::GetFontSize() * 40.0f;
    ImGui::SetNextWindowSizeConstraints(ImVec2(popup_width, 0.0f), ImVec2(popup_width, FLT_MAX));
    if (ImGui::BeginPopupModal((AF_TR("Reconcile audit log") + "###reconcile_confirm").c_str(),
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", AF_TR("This will rebuild the audit store.").c_str());
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped(
            "%s",
            AF_TR("The current `.af/manifest.af.json`, `.af/snapshots/`, and `.af/audit/` will be moved "
                  "to a timestamped `.af/backup_<UTC>/` folder, and a fresh audit store will be initialized "
                  "from the current SACM file on disk.")
                .c_str());
        ImGui::Spacing();
        ImGui::TextWrapped(
            "%s",
            AF_TR("Your existing transaction history is preserved on disk under the backup folder, but the "
                  "application timeline will start over from a new initial snapshot. Pinned historical views "
                  "from before this operation will no longer be browsable in-app.")
                .c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("%s", AF_TR("Continue?").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        const float button_width = 120.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float avail = ImGui::GetContentRegionAvail().x;
        const float used = button_width * 2.0f + spacing;
        if (avail > used)
            ImGui::Dummy(ImVec2(avail - used, 0.0f));
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(button_width, 0.0f)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.15f, 0.15f, 1.0f));
        if (ImGui::Button(AF_TR("Reconcile").c_str(), ImVec2(button_width, 0.0f))) {
            if (callbacks.reconcile_audit_store)
                callbacks.reconcile_audit_store();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void RenderCanvasAutosaveErrorBanner(AppRuntimeState& state) {
    if (state.last_autosave_error.empty())
        return;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(96, 16, 16, 255));
    ImGui::BeginChild("##autosave_error_banner", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 3.5f), true);
    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.55f, 1.0f), "%s", AF_TR("Autosave write failed").c_str());
    ImGui::TextWrapped("%s", state.last_autosave_error.c_str());
    ImGui::TextWrapped("%s",
                       AF_TR("Your most recent change is recorded in the audit log but the on-disk SACM "
                             "file may be out of date. Try a manual Save (File → Save) or "
                             "verify free disk space, file permissions, and any external sync agent.")
                           .c_str());
    if (ImGui::Button((AF_TR("Dismiss") + "##autosave_error").c_str()))
        state.last_autosave_error.clear();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

namespace {

// "Claude Code, SCCG AI Review, Jesper" -- who has contributed to what is on
// screen. A reviewer approving a change to a safety argument is entitled to know
// that before they read a word of it.
std::string DraftSourceSummary(const core::drafts::DraftWorkspace& workspace) {
    std::vector<std::string> labels;
    for (const core::drafts::DraftChangeGroup& group : workspace.groups) {
        if (!group.active() || group.operations.empty())
            continue;
        const std::string label = group.source_label.empty()
                                      ? std::string(core::drafts::DraftSourceToString(group.source))
                                      : group.source_label;
        if (std::find(labels.begin(), labels.end(), label) == labels.end())
            labels.push_back(label);
    }
    std::string summary;
    for (const std::string& label : labels) {
        if (!summary.empty())
            summary += ", ";
        summary += label;
    }
    return summary;
}

void DraftViewModeButton(ui::UiState& ui_state, ui::DraftViewMode mode, const std::string& label, const char* id) {
    const bool active = ui_state.draft_view_mode == mode;
    // The active mode is marked by its label, not only by its tint. A reader who
    // cannot distinguish the tint still has to be able to tell which argument
    // they are looking at -- that is the whole job of this control.
    const std::string text = (active ? "\xe2\x97\x8f " : "") + label;
    if (active) {
        const ui::Theme& theme = ui::GetTheme();
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(theme.accent));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(theme.accent_hover));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::InkOn(theme.accent)));
    }
    if (ImGui::Button((text + id).c_str()))
        ui_state.draft_view_mode = mode;
    if (active)
        ImGui::PopStyleColor(3);
}

} // namespace

void RenderWorkingDraftBanner(AppRuntimeState& state, const WorkbenchAreaCallbacks& callbacks) {
    const core::drafts::DraftWorkspace* workspace = state.draft_workspace.workspace();
    if (workspace == nullptr)
        return;
    const std::size_t staged = workspace->staged_group_count();
    if (staged == 0)
        return;

    ui::UiState& ui_state = ui::GetUiState();
    const bool needs_rebase = workspace->state == core::drafts::DraftWorkspaceState::NeedsRebase;
    const bool blocked = workspace->state == core::drafts::DraftWorkspaceState::Blocked;
    const bool promoting = workspace->state == core::drafts::DraftWorkspaceState::Promoting;

    // Derived from the theme rather than fixed, or the banner is legible in one
    // theme and not the other -- and the light theme is exactly where a dark
    // hardcoded tint leaves the text unreadable. `InkOn` picks the ink that has
    // contrast against whatever background the tint produced.
    const ui::Theme& theme = ui::GetTheme();
    const ImU32 banner_bg =
        ui::LerpColor(theme.surface_1, needs_rebase || blocked || promoting ? theme.attention : theme.accent, 0.18f);
    const ImU32 banner_ink = ui::InkOn(banner_bg);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(banner_bg));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(banner_ink));
    ImGui::BeginChild("##working_draft_banner", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 4.2f), true);

    // Stated in words, not implied by a tint. This is the line that stops a
    // proposal being read as the accepted safety argument.
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(banner_ink),
                       "%s",
                       ui::i18n::trnf("WORKING DRAFT — {0} unaccepted change",
                                      "WORKING DRAFT — {0} unaccepted changes",
                                      static_cast<int>(staged),
                                      staged)
                           .c_str());

    const std::string sources = DraftSourceSummary(*workspace);
    if (!sources.empty())
        ImGui::TextWrapped("%s", ui::i18n::trf("Sources: {0}", sources).c_str());

    if (needs_rebase) {
        ImGui::TextWrapped("%s",
                           AF_TR("The argument changed since this draft was written, so none of it is being "
                                 "applied. Inspect or discard it before continuing.")
                               .c_str());
    } else if (blocked) {
        ImGui::TextWrapped("%s",
                           AF_TR("This draft cannot be shown because one of its changes no longer applies. "
                                 "The accepted argument is displayed instead.")
                               .c_str());
    } else if (promoting) {
        ImGui::TextWrapped("%s",
                           AF_TR("Promotion is recorded, but the accepted SACM file is not yet confirmed. "
                                 "The draft is retained and cannot be edited or discarded.")
                               .c_str());
    }

    DraftViewModeButton(ui_state, ui::DraftViewMode::WorkingDraft, AF_TR("Working draft"), "##draft_view_working");
    ImGui::SameLine();
    DraftViewModeButton(
        ui_state, ui::DraftViewMode::AcceptedBaseline, AF_TR("Accepted baseline"), "##draft_view_accepted");
    ImGui::SameLine();
    DraftViewModeButton(ui_state, ui::DraftViewMode::ChangesOnly, AF_TR("Changes only"), "##draft_view_changes");

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(ImGui::GetFontSize(), 0.0f));
    ImGui::SameLine();

    // Promotion is disabled rather than hidden while the draft is not in a state
    // to be accepted, and the reason is on the tooltip. A button that silently
    // does nothing was the reported defect in the change-set flow it replaces.
    const bool promotable = !needs_rebase && !blocked && !promoting;
    ImGui::BeginDisabled(!promotable);
    if (ImGui::Button((AF_TR("Accept all") + "##draft_accept_all").c_str())) {
        if (callbacks.promote_working_draft)
            callbacks.promote_working_draft();
    }
    ImGui::EndDisabled();
    if (!promotable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        const std::string reason = needs_rebase ? AF_TR("The argument changed since this draft was written.")
                                   : promoting  ? AF_TR("Promotion is awaiting durable SACM completion.")
                                                : AF_TR("One of this draft's changes no longer applies.");
        ImGui::SetTooltip("%s", reason.c_str());
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(promoting);
    if (ImGui::Button((AF_TR("Discard draft") + "##draft_discard").c_str())) {
        if (callbacks.discard_working_draft)
            callbacks.discard_working_draft();
    }
    ImGui::EndDisabled();

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::Spacing();
}

void RenderArgumentPackageCanvasWithTimeline(AppRuntimeState& state,
                                             ui::UiState& ui_state,
                                             const WorkbenchAreaCallbacks& /*callbacks*/,
                                             WorkbenchState::ArgumentPackageCanvasTab& tab,
                                             const sacm::ArgumentPackage& argument_package,
                                             const parser::AssuranceCase& live_projection,
                                             ui::gsn::GsnCanvas& live_renderer,
                                             const ui::ElementContextActions& live_actions,
                                             const sacm::AssuranceCasePackage* terminology_package) {
    // No project / no audit store → render the live canvas without any
    // timeline rail. (Keeps Phase-1 fall-back behaviour symmetrical to the
    // old early-returns.)
    const bool has_audit =
        state.app_state.current_project.has_value() &&
        std::filesystem::exists(core::audit::ManifestPath(state.app_state.current_project->rootPath));
    if (!has_audit) {
        ui::gsn::ShowGsnCanvasContentWithRenderer(
            live_renderer, ui_state, &live_projection, live_actions, terminology_package, nullptr);
        return;
    }

    const core::AssuranceProject& project = state.app_state.current_project.value();
    CanvasHistoryState& tab_state = GetOrCreateHistoryState(tab.key);

    // Load + filter transactions. The cached helper returns a reference
    // owned by the audit-data cache; copy out so downstream code can keep
    // working with `tab_state.source_transactions` by value as before.
    std::string store_error;
    tab_state.source_transactions = LoadTransactions(project, store_error);
    RebuildFilteredTransactions(tab_state.filtered, tab_state.source_transactions, argument_package);
    const std::vector<core::audit::AuditTransaction>& visible_transactions = tab_state.filtered.filtered;

    // Load baselines (best-effort: warnings are ignored at this layer; the
    // timeline still renders with whatever loaded successfully). Cached so
    // the manifest + per-baseline sidecar reads only run when the files
    // actually change.
    const std::vector<core::audit::BaselineMetadata>& baselines = GetCachedBaselines(project.rootPath, nullptr);

    // Enumerate snapshots from disk (snapshot/<id>/snapshot.json). The
    // manifest only records `initial_snapshot_id`, so we walk the directory
    // and read each metadata file. Cached so the per-frame directory walk
    // + metadata reads only run when the snapshots directory changes.
    const std::vector<core::audit::SnapshotMetadata>& snapshots = GetCachedSnapshots(project.rootPath);

    // Read the manifest so the unified timeline builder can flag the
    // initial snapshot (sorts first at sequence 0, labelled "S0"). The
    // manifest is small JSON — a per-frame read is cheap relative to the
    // already-cached transactions/baselines/snapshots reads above and
    // avoids threading a new cache helper through `audit_data_cache`.
    std::string manifest_initial_snapshot_id;
    {
        core::audit::AuditManifest manifest;
        std::string manifest_error;
        if (core::audit::ReadAuditManifest(project.rootPath, manifest, manifest_error)) {
            manifest_initial_snapshot_id = manifest.initial_snapshot_id;
        }
    }

    // Clamp + decide live-vs-preview.
    const std::uint64_t latest_seq =
        visible_transactions.empty() ? 0 : visible_transactions.back().transaction_sequence;
    if (tab.timeline.preview_sequence.has_value() && *tab.timeline.preview_sequence > latest_seq) {
        tab.timeline.preview_sequence.reset();
    }
    const bool live = !tab.timeline.preview_sequence.has_value();
    const std::uint64_t target_seq = tab.timeline.preview_sequence.value_or(latest_seq);
    // Mirror for legacy consumers (transactions table, Phase 2 removal).
    tab.selected_transaction_sequence = tab.timeline.preview_sequence;

    if (!live) {
        RefreshReconstruction(tab_state.reconstruction, tab_state, project, target_seq, argument_package);
    }

    // Build the overlay buttons: timeline strip always; Live pill only when
    // a preview is active.
    ui::gsn::CanvasOverlayButtons overlay;
    if (!live) {
        overlay.on_return_to_live = [&tab]() { tab.timeline.preview_sequence.reset(); };
        overlay.historical_badge_text = ui::i18n::trf("Preview: Tx {0} (read-only)", target_seq);
    }

    // Capture by reference; the canvas invokes this synchronously during
    // ShowGsnCanvasContentWithRenderer, so the locals above outlive the call.
    auto strip_cb = [&, latest_seq](ImVec2 mn, ImVec2 mx) {
        core::audit::TimelineQuery query;
        query.view_mode = tab.timeline.view_mode;
        query.scope = tab.timeline.scope;
        query.package_id = argument_package.id;
        query.package_gid = argument_package.gid;
        query.initial_snapshot_id = manifest_initial_snapshot_id;
        core::audit::TimelineModel model =
            core::audit::BuildTimelineModel(visible_transactions, baselines, snapshots, query);

        ui::timeline::TimelineAction act =
            ui::timeline::RenderTimelineWidget(tab.timeline, model, mn, mx, tab.key.c_str());

        using ui::timeline::TimelineActionType;
        switch (act.type) {
        case TimelineActionType::PreviewSequence:
            if (act.sequence.has_value()) {
                if (*act.sequence >= latest_seq)
                    tab.timeline.preview_sequence.reset();
                else
                    tab.timeline.preview_sequence = act.sequence;
            }
            break;
        case TimelineActionType::ReturnToLatest:
            tab.timeline.preview_sequence.reset();
            break;
        case TimelineActionType::ChangeViewMode:
            if (act.view_mode.has_value())
                tab.timeline.view_mode = *act.view_mode;
            break;
        case TimelineActionType::CreateBaseline:
            app::areas::OpenBaselineModal(tab.baseline_modal, latest_seq, std::string());
            break;
        case TimelineActionType::CreateSnapshot: {
            core::audit::SnapshotMetadata created;
            std::string err;
            const std::string reason = "User-initiated snapshot from timeline";
            if (core::audit::CreateUserSnapshot(project.rootPath, reason, state.reviewer_name, created, err)) {
                state.app_state.status_message =
                    ui::i18n::trf("Snapshot created at sequence {0}.", created.transaction_sequence);
            } else {
                state.app_state.status_message = ui::i18n::trf("Failed to create snapshot: {0}", err);
            }
            break;
        }
        default:
            break;
        }
    };
    overlay.on_render_timeline_strip = strip_cb;

    // Render the chosen canvas (live or historical) with overlay attached.
    if (live) {
        live_renderer.ClearHistoryHighlights();
        ui::gsn::ShowGsnCanvasContentWithRenderer(
            live_renderer, ui_state, &live_projection, live_actions, terminology_package, &overlay);
    } else {
        RenderHistoricalCanvas(tab_state, ui_state, visible_transactions, target_seq, &overlay);
    }

    // Baseline-creation modal (opened by the timeline's actions menu).
    app::areas::RenderBaselineModal(tab.baseline_modal,
                                    project.rootPath,
                                    state.reviewer_name,
                                    [&state](const std::string& message) { state.app_state.status_message = message; });
}

void ForgetCanvasHistoryTab(const std::string& tab_key) {
    g_history_state_by_tab_key.erase(tab_key);
}

} // namespace app::areas
