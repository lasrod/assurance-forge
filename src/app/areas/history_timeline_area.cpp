#include "app/areas/history_timeline_area.h"

#include "app/app_runtime_state.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_store.h"
#include "core/audit/canonical_model_hash.h"
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
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace app::areas {

namespace {

// Single-entry reconstruction cache. Keyed on (project root, target sequence)
// — when those match a prior call the cached state is reused, otherwise the
// replayer runs once and the result is cached. We hold onto the reconstructed
// ReplayState because the embedded read-only canvas needs the full model.
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

ReconstructionCache g_reconstruction_cache;

// Embedded read-only canvas renderer. One per process is enough — the
// History Timeline is a single tab and is not multi-instanced.
ui::gsn::GsnCanvas g_history_canvas;
bool g_history_canvas_seeded = false;
std::optional<std::uint64_t> g_seeded_sequence;

void RefreshReconstruction(const core::AssuranceProject& project, std::uint64_t target_seq) {
    if (g_reconstruction_cache.valid && g_reconstruction_cache.project_root == project.rootPath &&
        g_reconstruction_cache.sequence == target_seq) {
        return;
    }
    g_reconstruction_cache = ReconstructionCache{};
    g_reconstruction_cache.project_root = project.rootPath;
    g_reconstruction_cache.sequence = target_seq;
    g_history_canvas_seeded = false;
    g_seeded_sequence.reset();

    auto state = core::audit::ReconstructAtSequence(project, target_seq);
    if (!state) {
        g_reconstruction_cache.error = state.error();
        g_reconstruction_cache.valid = true;
        return;
    }
    g_reconstruction_cache.element_count = state->model.elements.size();
    g_reconstruction_cache.canonical_hash = core::audit::CanonicalModelHash(state->package);
    g_reconstruction_cache.tree = ui::gsn::BuildAssuranceTree(state->model);
    g_reconstruction_cache.state = std::move(*state);
    g_reconstruction_cache.has_state = true;
    g_reconstruction_cache.valid = true;
}

std::vector<core::audit::AuditTransaction> LoadTransactions(const core::AssuranceProject& project,
                                                            std::string& error_out) {
    auto store = core::audit::EventStore::Open(project.rootPath, error_out);
    if (!store)
        return {};
    return store->Transactions();
}

void RenderReconstructedCanvas(ui::UiState& ui_state,
                               const std::vector<core::audit::AuditTransaction>& transactions,
                               std::uint64_t target_seq) {
    if (!g_reconstruction_cache.has_state) {
        ImGui::TextDisabled("No reconstructed model to display.");
        return;
    }
    if (!g_history_canvas_seeded || g_seeded_sequence != g_reconstruction_cache.sequence) {
        g_history_canvas.SetTree(g_reconstruction_cache.tree);
        g_history_canvas_seeded = true;
        g_seeded_sequence = g_reconstruction_cache.sequence;
    }

    // Push per-element highlights for the transaction at target_seq. When
    // target_seq == 0 (initial snapshot) the highlight map is empty.
    g_history_canvas.SetHistoryHighlights(
        core::audit::BuildHistoryHighlightsForSequence(transactions, target_seq));

    // Empty actions struct → context-menu items are inert, preserving the
    // read-only contract for historical views.
    ui::ElementContextActions readonly_actions;
    ui::gsn::ShowGsnCanvasContentWithRenderer(
        g_history_canvas, ui_state, &g_reconstruction_cache.state.model, readonly_actions, nullptr);
}

} // namespace

void RenderHistoryTimelineArea(AppRuntimeState& state) {
    ui::UiState& ui_state = ui::GetUiState();

    ui::panels::HistoryTimelinePanelModel model;

    if (!state.app_state.current_project.has_value()) {
        model.has_audit_store = false;
        ui::panels::HistoryTimelinePanelCallbacks callbacks;
        ui::panels::ShowHistoryTimelinePanel(model, callbacks);
        return;
    }

    const core::AssuranceProject& project = state.app_state.current_project.value();
    const std::filesystem::path manifest_path = core::audit::ManifestPath(project.rootPath);
    model.has_audit_store = std::filesystem::exists(manifest_path);

    if (!model.has_audit_store) {
        ui::panels::HistoryTimelinePanelCallbacks callbacks;
        ui::panels::ShowHistoryTimelinePanel(model, callbacks);
        return;
    }

    std::string store_error;
    static std::vector<core::audit::AuditTransaction> s_transactions;
    s_transactions = LoadTransactions(project, store_error);
    model.transactions = &s_transactions;

    if (!store_error.empty()) {
        ImGui::TextDisabled("Failed to open audit store: %s", store_error.c_str());
        return;
    }

    if (s_transactions.empty()) {
        ui::panels::HistoryTimelinePanelCallbacks callbacks;
        ui::panels::ShowHistoryTimelinePanel(model, callbacks);
        // Reset selection when the log is empty.
        ui_state.history_selected_transaction_sequence.reset();
        return;
    }

    const std::uint64_t latest_seq = s_transactions.back().transaction_sequence;
    // Clamp any stale selection to the current range.
    if (ui_state.history_selected_transaction_sequence.has_value() &&
        *ui_state.history_selected_transaction_sequence > latest_seq) {
        ui_state.history_selected_transaction_sequence.reset();
    }

    const std::uint64_t target_seq = ui_state.history_selected_transaction_sequence.value_or(latest_seq);

    RefreshReconstruction(project, target_seq);
    model.selected_sequence = ui_state.history_selected_transaction_sequence;
    model.reconstructed_element_count = g_reconstruction_cache.element_count;
    model.reconstructed_canonical_hash = g_reconstruction_cache.canonical_hash;
    model.reconstruction_error = g_reconstruction_cache.error;

    ui::panels::HistoryTimelinePanelCallbacks callbacks;
    callbacks.on_select_sequence = [&ui_state, latest_seq](std::uint64_t seq) {
        // Selecting the latest sequence means the user is back to live; drop
        // the pin so the banner returns to "LIVE".
        if (seq >= latest_seq)
            ui_state.history_selected_transaction_sequence.reset();
        else
            ui_state.history_selected_transaction_sequence = seq;
    };
    callbacks.on_return_to_live = [&ui_state]() {
        ui_state.history_selected_transaction_sequence.reset();
    };

    // Split layout: header (banner + slider + summary) → embedded canvas →
    // transactions table. The canvas and table share the remaining vertical
    // space evenly via two scrolling child windows.
    ui::panels::ShowHistoryTimelineHeader(model, callbacks);

    ImGui::Spacing();
    ImGui::SeparatorText("Reconstructed canvas (read-only)");

    const float remaining = ImGui::GetContentRegionAvail().y;
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    // Reserve the table at the bottom and let the canvas take the upper 65%.
    const float canvas_h = std::max(120.0f, (remaining - spacing) * 0.65f);
    ImGui::BeginChild("##history_canvas_child", ImVec2(0.0f, canvas_h), true, ImGuiWindowFlags_NoScrollbar);
    RenderReconstructedCanvas(ui_state, s_transactions, target_seq);
    ImGui::EndChild();

    ImGui::SeparatorText("Transactions");
    ImGui::BeginChild("##history_transactions_child", ImVec2(0.0f, 0.0f), false);
    ui::panels::ShowHistoryTimelineTransactions(model, callbacks);
    ImGui::EndChild();
}

} // namespace app::areas
