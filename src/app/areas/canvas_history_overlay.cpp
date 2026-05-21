#include "app/areas/canvas_history_overlay.h"

#include "app/app_runtime_state.h"
#include "core/argument_package_projection.h"
#include "core/audit/audit_paths.h"
#include "core/audit/canonical_model_hash.h"
#include "core/audit/event_scope.h"
#include "core/audit/event_store.h"
#include "core/audit/history_highlights.h"
#include "core/audit/history_reconstruction.h"
#include "ui/element_context_menu.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/gsn/gsn_canvas_renderer.h"
#include "ui/panels/history_timeline_panel.h"
#include "ui/ui_state.h"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
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
    std::size_t element_count = 0;
    std::string canonical_hash;
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

std::vector<core::audit::AuditTransaction> LoadTransactions(const core::AssuranceProject& project,
                                                            std::string& error_out) {
    auto store = core::audit::EventStore::Open(project.rootPath, error_out);
    if (!store)
        return {};
    return store->Transactions();
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

    core::audit::ArgumentPackageScope scope =
        core::audit::CollectArgumentPackageScope(argument_package);
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
            const bool parent_in_scope =
                scope.element_ids.count(parent) > 0 || scope.element_gids.count(parent) > 0;
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

    auto state = core::audit::ReconstructAtSequence(project, target_seq, argument_package.id,
                                                    argument_package.gid);
    if (!state) {
        cache.error = state.error();
        cache.valid = true;
        return;
    }
    cache.element_count = state->model.elements.size();
    cache.canonical_hash = core::audit::CanonicalModelHash(state->package);
    cache.tree = ui::gsn::BuildAssuranceTree(state->model);
    cache.state = std::move(*state);
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
            ImGui::TextDisabled("Reconstruction failed: %s", tab_state.reconstruction.error.c_str());
        else
            ImGui::TextDisabled("No reconstructed model to display.");
        return;
    }
    if (!tab_state.historical_seeded ||
        tab_state.historical_seeded_sequence != tab_state.reconstruction.sequence) {
        tab_state.historical_renderer.SetTree(tab_state.reconstruction.tree);
        tab_state.historical_seeded = true;
        tab_state.historical_seeded_sequence = tab_state.reconstruction.sequence;
    }

    // Highlights are restricted to ids in the active package's running
    // scope so cross-package events don't tint nodes that aren't visible.
    auto highlights = core::audit::BuildHistoryHighlightsForSequence(transactions, target_seq);
    if (tab_state.filtered.valid) {
        const auto& scope = tab_state.filtered.scope;
        for (auto it = highlights.begin(); it != highlights.end();) {
            const bool in_scope =
                scope.element_ids.count(it->first) > 0 || scope.element_gids.count(it->first) > 0;
            if (in_scope)
                ++it;
            else
                it = highlights.erase(it);
        }
    }
    tab_state.historical_renderer.SetHistoryHighlights(std::move(highlights));

    ui::ElementContextActions readonly_actions;
    ui::gsn::ShowGsnCanvasContentWithRenderer(tab_state.historical_renderer, ui_state,
                                              &tab_state.reconstruction.state.model,
                                              readonly_actions, nullptr, overlay_buttons);
}

} // namespace

bool ProjectHasAuditStore(const AppRuntimeState& state) {
    if (!state.app_state.current_project.has_value())
        return false;
    return std::filesystem::exists(core::audit::ManifestPath(state.app_state.current_project->rootPath));
}

void RenderCanvasDivergenceBanner(AppRuntimeState& state,
                                  const WorkbenchAreaCallbacks& callbacks) {
    if (!state.last_audit_verification.has_value() || !state.last_audit_verification->ran ||
        state.last_audit_verification->success) {
        return;
    }
    const auto& v = *state.last_audit_verification;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(80, 50, 0, 255));
    ImGui::BeginChild("##audit_warning_banner",
                      ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 4.5f), true);
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Audit log divergence detected");
    ImGui::TextWrapped(
        "The replayed audit history does not reproduce the on-disk SACM. This usually means "
        "edits were applied through a path that did not record transactions. Pinned historical "
        "views may be inaccurate.");
    if (!v.replayed_canonical_hash.empty() && !v.on_disk_canonical_hash.empty()) {
        ImGui::Text("replay=%s  on_disk=%s", v.replayed_canonical_hash.substr(0, 12).c_str(),
                    v.on_disk_canonical_hash.substr(0, 12).c_str());
    }
    if (ImGui::Button("Reconcile audit log")) {
        if (callbacks.reconcile_audit_store)
            callbacks.reconcile_audit_store();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(archives current .af/ artifacts and rebuilds from the current SACM file)");
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void RenderCanvasHistoryOverlay(AppRuntimeState& state,
                                ui::UiState& ui_state,
                                const WorkbenchAreaCallbacks& /*callbacks*/,
                                WorkbenchState::ArgumentPackageCanvasTab& tab,
                                const sacm::ArgumentPackage& argument_package,
                                const parser::AssuranceCase& live_projection,
                                ui::gsn::GsnCanvas& live_renderer,
                                const ui::gsn::CanvasOverlayButtons* overlay_buttons) {
    if (!state.app_state.current_project.has_value()) {
        ImGui::TextDisabled("Open a project to browse history.");
        return;
    }
    if (!ProjectHasAuditStore(state)) {
        ImGui::TextDisabled("This project has no audit store.");
        return;
    }

    const core::AssuranceProject& project = state.app_state.current_project.value();
    CanvasHistoryState& tab_state = GetOrCreateHistoryState(tab.key);

    std::string store_error;
    tab_state.source_transactions = LoadTransactions(project, store_error);
    if (!store_error.empty()) {
        ImGui::TextDisabled("Failed to open audit store: %s", store_error.c_str());
        return;
    }

    RebuildFilteredTransactions(tab_state.filtered, tab_state.source_transactions, argument_package);
    const std::vector<core::audit::AuditTransaction>& visible_transactions = tab_state.filtered.filtered;

    ui::panels::HistoryTimelinePanelModel model;
    model.has_audit_store = true;
    model.transactions = &visible_transactions;

    if (visible_transactions.empty()) {
        ImGui::TextDisabled("No transactions recorded for this argument package yet.");
        tab.selected_transaction_sequence.reset();
        return;
    }

    const std::uint64_t latest_seq = visible_transactions.back().transaction_sequence;
    if (tab.selected_transaction_sequence.has_value() &&
        *tab.selected_transaction_sequence > latest_seq) {
        tab.selected_transaction_sequence.reset();
    }
    const bool live = !tab.selected_transaction_sequence.has_value();
    const std::uint64_t target_seq = tab.selected_transaction_sequence.value_or(latest_seq);

    if (live) {
        model.selected_sequence.reset();
    } else {
        RefreshReconstruction(tab_state.reconstruction, tab_state, project, target_seq,
                              argument_package);
        model.selected_sequence = tab.selected_transaction_sequence;
    }

    ui::panels::HistoryTimelinePanelCallbacks slider_callbacks;
    slider_callbacks.on_select_sequence = [&tab, latest_seq](std::uint64_t seq) {
        if (seq >= latest_seq)
            tab.selected_transaction_sequence.reset();
        else
            tab.selected_transaction_sequence = seq;
    };
    slider_callbacks.on_return_to_live = [&tab]() { tab.selected_transaction_sequence.reset(); };

    ui::panels::ShowHistoryTimelineHeader(model, slider_callbacks);

    ImGui::Spacing();
    const std::string scope_label = std::string(" — ArgumentPackage: ") +
                                    (argument_package.name.empty() ? tab.title : argument_package.name);
    if (live)
        ImGui::SeparatorText((std::string("Live model") + scope_label).c_str());
    else
        ImGui::SeparatorText((std::string("Reconstructed (read-only)") + scope_label).c_str());

    // Augment the incoming overlay buttons with a return-to-live affordance
    // whenever we're pinned to a past transaction.
    ui::gsn::CanvasOverlayButtons inner_buttons;
    if (overlay_buttons)
        inner_buttons = *overlay_buttons;
    if (!live)
        inner_buttons.on_return_to_live = [&tab]() { tab.selected_transaction_sequence.reset(); };
    const ui::gsn::CanvasOverlayButtons* inner_buttons_ptr =
        (overlay_buttons || !live) ? &inner_buttons : nullptr;

    const float remaining = ImGui::GetContentRegionAvail().y;
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    const float canvas_h = std::max(120.0f, (remaining - spacing) * 0.65f);
    ImGui::BeginChild("##canvas_history_canvas_region", ImVec2(0.0f, canvas_h), true,
                      ImGuiWindowFlags_NoScrollbar);
    if (live) {
        // The caller already seeded `live_renderer` against `live_projection`
        // for this case_revision; we just need to clear any leftover
        // historical highlights from a previous pinned view.
        live_renderer.ClearHistoryHighlights();
        ui::ElementContextActions readonly_actions;
        const sacm::AssuranceCasePackage* terminology_package =
            state.app_state.sacm_package.has_value() ? &state.app_state.sacm_package.value() : nullptr;
        ui::gsn::ShowGsnCanvasContentWithRenderer(live_renderer, ui_state, &live_projection,
                                                  readonly_actions, terminology_package,
                                                  inner_buttons_ptr);
    } else {
        RenderHistoricalCanvas(tab_state, ui_state, visible_transactions, target_seq,
                               inner_buttons_ptr);
    }
    ImGui::EndChild();

    ImGui::SeparatorText("Transactions");
    ImGui::BeginChild("##canvas_history_transactions_region", ImVec2(0.0f, 0.0f), false);
    ui::panels::ShowHistoryTimelineTransactions(model, slider_callbacks);
    ImGui::EndChild();
}

void ForgetCanvasHistoryTab(const std::string& tab_key) {
    g_history_state_by_tab_key.erase(tab_key);
}

} // namespace app::areas
