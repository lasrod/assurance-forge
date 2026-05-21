#include "app/areas/history_timeline_area.h"

#include "app/app_runtime_state.h"
#include "app/areas/workbench_area.h"
#include "core/argument_package_projection.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_store.h"
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
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace app::areas {

namespace {

// Single-entry reconstruction cache. Keyed on (project root, target sequence,
// active argument-package key) — when those match a prior call the cached
// state is reused, otherwise the replayer runs once and the result is
// cached. We hold onto the reconstructed ReplayState because the embedded
// read-only canvas needs the full model.
struct ReconstructionCache {
    std::filesystem::path project_root;
    std::optional<std::uint64_t> sequence;
    std::string argument_package_key;
    std::size_t element_count = 0;
    std::string canonical_hash;
    std::string error;
    bool valid = false;
    bool has_state = false;
    core::audit::ReplayState state;
    core::AssuranceTree tree;
};

ReconstructionCache g_reconstruction_cache;

// Active-package context resolved once per frame.
struct ActiveArgumentPackageContext {
    bool has_active_package = false;
    std::string canvas_key;        // workbench canvas-tab key
    std::string package_id;
    std::string package_gid;
    std::string package_name;
    core::audit::ArgumentPackageScope scope; // ids ever in scope
};

// Cached filtered transaction list. Recomputed when either the underlying
// transactions list changes (size + last sequence) or the active package
// changes.
struct FilteredTransactionsCache {
    std::size_t source_size = 0;
    std::uint64_t last_sequence = 0;
    std::string argument_package_key;
    std::vector<core::audit::AuditTransaction> filtered;
    core::audit::ArgumentPackageScope scope;
    bool valid = false;
};

FilteredTransactionsCache g_filtered_cache;

// Embedded read-only canvas renderer. One per process is enough — the
// History Timeline is a single tab and is not multi-instanced.
ui::gsn::GsnCanvas g_history_canvas;
bool g_history_canvas_seeded = false;
std::optional<std::uint64_t> g_seeded_sequence;

// Separate renderer for the LIVE view so we don't fight the historical
// renderer's seed cache when the user toggles between LIVE and a pinned
// sequence. Keyed on `case_revision` so the tree is rebuilt only when the
// model actually changes.
ui::gsn::GsnCanvas g_live_canvas;
core::AssuranceTree g_live_tree;
std::uint64_t g_live_seeded_revision = ~std::uint64_t{0};

void RefreshReconstruction(const core::AssuranceProject& project, std::uint64_t target_seq,
                           const ActiveArgumentPackageContext& active) {
    if (g_reconstruction_cache.valid && g_reconstruction_cache.project_root == project.rootPath &&
        g_reconstruction_cache.sequence == target_seq &&
        g_reconstruction_cache.argument_package_key == active.canvas_key) {
        return;
    }
    g_reconstruction_cache = ReconstructionCache{};
    g_reconstruction_cache.project_root = project.rootPath;
    g_reconstruction_cache.sequence = target_seq;
    g_reconstruction_cache.argument_package_key = active.canvas_key;
    g_history_canvas_seeded = false;
    g_seeded_sequence.reset();

    auto state = core::audit::ReconstructAtSequence(project, target_seq, active.package_id,
                                                    active.package_gid);
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

// Resolve which ArgumentPackage the History Timeline should be scoped to.
// The History tab is single-instance and follows the workbench's currently
// active GSN canvas tab. When no canvas tab is active (or the project hasn't
// loaded an SACM yet) we fall back to project-wide scope.
ActiveArgumentPackageContext ResolveActiveArgumentPackage(const AppRuntimeState& state) {
    ActiveArgumentPackageContext ctx;
    if (state.workbench.active_argument_package_canvas_key.empty())
        return ctx;
    const auto& tabs = state.workbench.argument_package_canvas_tabs;
    auto it = std::find_if(tabs.begin(), tabs.end(), [&](const auto& tab) {
        return tab.key == state.workbench.active_argument_package_canvas_key;
    });
    if (it == tabs.end())
        return ctx;
    if (!state.app_state.sacm_package.has_value())
        return ctx;
    const sacm::ArgumentPackage* pkg = core::FindArgumentPackageByIdentity(
        state.app_state.sacm_package.value(), it->package_id, it->package_gid);
    if (!pkg)
        return ctx;
    ctx.has_active_package = true;
    ctx.canvas_key = it->key;
    ctx.package_id = it->package_id;
    ctx.package_gid = it->package_gid;
    ctx.package_name = pkg->name.empty() ? it->title : pkg->name;
    ctx.scope = core::audit::CollectArgumentPackageScope(*pkg);
    return ctx;
}

// Walk transactions forward, growing the package scope as new child elements
// are created with parents already in scope. A transaction is included when
// any of its event ids overlaps the *running* scope. This is conservative-
// inclusive: it may miss top-goal create+delete pairs that never ended up
// in the LIVE model (rare), but never silently includes events that are
// unambiguously outside the package.
void RebuildFilteredTransactions(const std::vector<core::audit::AuditTransaction>& source,
                                 const ActiveArgumentPackageContext& active) {
    const std::uint64_t last_seq = source.empty() ? 0 : source.back().transaction_sequence;
    if (g_filtered_cache.valid && g_filtered_cache.source_size == source.size() &&
        g_filtered_cache.last_sequence == last_seq &&
        g_filtered_cache.argument_package_key == active.canvas_key) {
        return;
    }
    g_filtered_cache = FilteredTransactionsCache{};
    g_filtered_cache.source_size = source.size();
    g_filtered_cache.last_sequence = last_seq;
    g_filtered_cache.argument_package_key = active.canvas_key;

    if (!active.has_active_package) {
        // No filter — leave filtered empty; caller uses the source list.
        g_filtered_cache.valid = true;
        return;
    }

    core::audit::ArgumentPackageScope scope = active.scope;
    g_filtered_cache.filtered.reserve(source.size());
    for (const core::audit::AuditTransaction& tx : source) {
        const bool include = core::audit::TransactionTouchesScope(tx, scope);
        if (include)
            g_filtered_cache.filtered.push_back(tx);
        // Grow scope: any CreateChildElement whose parent is in scope means
        // its generated ids are now in the active package.
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
    g_filtered_cache.scope = std::move(scope);
    g_filtered_cache.valid = true;
}

void RenderReconstructedCanvas(ui::UiState& ui_state,
                               const std::vector<core::audit::AuditTransaction>& transactions,
                               std::uint64_t target_seq,
                               const ActiveArgumentPackageContext& active) {
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
    // an active package is set, restrict the highlight map to ids known to
    // belong to that package's running scope so highlights from cross-
    // package events don't leak into the canvas.
    auto highlights = core::audit::BuildHistoryHighlightsForSequence(transactions, target_seq);
    if (active.has_active_package) {
        const auto& scope = g_filtered_cache.valid ? g_filtered_cache.scope : active.scope;
        for (auto it = highlights.begin(); it != highlights.end();) {
            const bool in_scope = scope.element_ids.count(it->first) > 0 ||
                                  scope.element_gids.count(it->first) > 0;
            if (in_scope)
                ++it;
            else
                it = highlights.erase(it);
        }
    }
    g_history_canvas.SetHistoryHighlights(std::move(highlights));

    // Empty actions struct → context-menu items are inert, preserving the
    // read-only contract for historical views.
    ui::ElementContextActions readonly_actions;
    ui::gsn::ShowGsnCanvasContentWithRenderer(
        g_history_canvas, ui_state, &g_reconstruction_cache.state.model, readonly_actions, nullptr);
}

void RenderLiveCanvas(ui::UiState& ui_state, AppRuntimeState& state,
                      const ActiveArgumentPackageContext& active) {
    if (!state.app_state.loaded_case.has_value()) {
        ImGui::TextDisabled("No SACM model is currently loaded.");
        return;
    }

    // When an active package is set, project the live model down to that
    // package's elements before seeding the canvas. The cache key is
    // (case_revision, active package key) so toggling tabs invalidates the
    // tree without recomputing on every frame.
    static parser::AssuranceCase s_live_projected;
    static std::string s_live_projection_key;
    const parser::AssuranceCase* model_to_render = &state.app_state.loaded_case.value();
    if (active.has_active_package && state.app_state.sacm_package.has_value()) {
        const sacm::ArgumentPackage* pkg = core::FindArgumentPackageByIdentity(
            state.app_state.sacm_package.value(), active.package_id, active.package_gid);
        if (pkg) {
            const std::string projection_key = std::to_string(state.app_state.case_revision) + "|" +
                                               active.canvas_key;
            if (projection_key != s_live_projection_key) {
                s_live_projected = core::BuildArgumentPackageProjection(
                    state.app_state.loaded_case.value(), *pkg, active.package_name);
                s_live_projection_key = projection_key;
                g_live_seeded_revision = ~std::uint64_t{0};
            }
            model_to_render = &s_live_projected;
        }
    } else if (!s_live_projection_key.empty()) {
        s_live_projection_key.clear();
        s_live_projected = parser::AssuranceCase{};
        g_live_seeded_revision = ~std::uint64_t{0};
    }

    if (g_live_seeded_revision != state.app_state.case_revision) {
        g_live_tree = ui::gsn::BuildAssuranceTree(*model_to_render);
        g_live_canvas.SetTree(g_live_tree);
        g_live_canvas.SetCaseRevision(state.app_state.case_revision);
        g_live_seeded_revision = state.app_state.case_revision;
    }
    g_live_canvas.ClearHistoryHighlights();
    ui::ElementContextActions readonly_actions;
    const sacm::AssuranceCasePackage* terminology_package =
        state.app_state.sacm_package.has_value() ? &state.app_state.sacm_package.value() : nullptr;
    ui::gsn::ShowGsnCanvasContentWithRenderer(
        g_live_canvas, ui_state, model_to_render, readonly_actions, terminology_package);
}

} // namespace

void RenderHistoryTimelineArea(AppRuntimeState& state, const WorkbenchAreaCallbacks& workbench_callbacks) {
    ui::UiState& ui_state = ui::GetUiState();

    // Audit divergence banner. Drawn before any early-return so the user
    // sees the warning even when the project has no transactions yet.
    if (state.last_audit_verification.has_value() && state.last_audit_verification->ran &&
        !state.last_audit_verification->success) {
        const auto& v = *state.last_audit_verification;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(80, 50, 0, 255));
        ImGui::BeginChild("##audit_warning_banner",
                          ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 4.5f),
                          true);
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                           "Audit log divergence detected");
        ImGui::TextWrapped(
            "The replayed audit history does not reproduce the on-disk SACM. This usually means "
            "edits were applied through a path that did not record transactions. Pinned historical "
            "views may be inaccurate.");
        if (!v.replayed_canonical_hash.empty() && !v.on_disk_canonical_hash.empty()) {
            ImGui::Text("replay=%s  on_disk=%s",
                        v.replayed_canonical_hash.substr(0, 12).c_str(),
                        v.on_disk_canonical_hash.substr(0, 12).c_str());
        }
        if (ImGui::Button("Reconcile audit log")) {
            if (workbench_callbacks.reconcile_audit_store)
                workbench_callbacks.reconcile_audit_store();
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            "(archives current .af/ artifacts and rebuilds from the current SACM file)");
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

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

    const ActiveArgumentPackageContext active = ResolveActiveArgumentPackage(state);
    RebuildFilteredTransactions(s_transactions, active);

    const std::vector<core::audit::AuditTransaction>* visible_transactions =
        active.has_active_package ? &g_filtered_cache.filtered : &s_transactions;
    model.transactions = visible_transactions;

    if (!store_error.empty()) {
        ImGui::TextDisabled("Failed to open audit store: %s", store_error.c_str());
        return;
    }

    if (visible_transactions->empty()) {
        ui::panels::HistoryTimelinePanelCallbacks callbacks;
        ui::panels::ShowHistoryTimelinePanel(model, callbacks);
        // Reset selection when the (filtered) log is empty.
        ui_state.history_selected_transaction_sequence.reset();
        return;
    }

    const std::uint64_t latest_seq = visible_transactions->back().transaction_sequence;
    // Clamp any stale selection to the current range.
    if (ui_state.history_selected_transaction_sequence.has_value() &&
        *ui_state.history_selected_transaction_sequence > latest_seq) {
        ui_state.history_selected_transaction_sequence.reset();
    }

    const bool live = !ui_state.history_selected_transaction_sequence.has_value();
    const std::uint64_t target_seq = ui_state.history_selected_transaction_sequence.value_or(latest_seq);

    // LIVE view: render the actual loaded model rather than replaying the
    // audit log. If past mutations bypassed the command bus (e.g. legacy
    // mutation paths, or work done before the bus was installed for the
    // current session), the replayed state would diverge from the on-disk
    // SACM and confuse users. The historical reconstruction is only the
    // truth when a sequence is explicitly pinned.
    if (live && state.app_state.loaded_case.has_value()) {
        model.selected_sequence.reset();
        if (active.has_active_package && state.app_state.sacm_package.has_value()) {
            const sacm::ArgumentPackage* pkg = core::FindArgumentPackageByIdentity(
                state.app_state.sacm_package.value(), active.package_id, active.package_gid);
            model.reconstructed_element_count =
                pkg ? (pkg->claims.size() + pkg->argumentReasonings.size() +
                       pkg->artifactReferences.size() + pkg->assertedInferences.size() +
                       pkg->assertedContexts.size() + pkg->assertedEvidences.size())
                    : std::size_t{0};
        } else {
            model.reconstructed_element_count = state.app_state.loaded_case->elements.size();
        }
        model.reconstructed_canonical_hash =
            state.app_state.sacm_package.has_value()
                ? core::audit::CanonicalModelHash(state.app_state.sacm_package.value())
                : std::string{};
        model.reconstruction_error.clear();
    } else {
        RefreshReconstruction(project, target_seq, active);
        model.selected_sequence = ui_state.history_selected_transaction_sequence;
        model.reconstructed_element_count = g_reconstruction_cache.element_count;
        model.reconstructed_canonical_hash = g_reconstruction_cache.canonical_hash;
        model.reconstruction_error = g_reconstruction_cache.error;
    }

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
    const std::string scope_label = active.has_active_package
                                        ? std::string(" — ArgumentPackage: ") + active.package_name
                                        : std::string(" — All packages");
    if (live)
        ImGui::SeparatorText((std::string("Live model (read-only)") + scope_label).c_str());
    else
        ImGui::SeparatorText((std::string("Reconstructed canvas (read-only)") + scope_label).c_str());

    const float remaining = ImGui::GetContentRegionAvail().y;
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    // Reserve the table at the bottom and let the canvas take the upper 65%.
    const float canvas_h = std::max(120.0f, (remaining - spacing) * 0.65f);
    ImGui::BeginChild("##history_canvas_child", ImVec2(0.0f, canvas_h), true, ImGuiWindowFlags_NoScrollbar);
    if (live) {
        RenderLiveCanvas(ui_state, state, active);
    } else {
        RenderReconstructedCanvas(ui_state, *visible_transactions, target_seq, active);
    }
    ImGui::EndChild();

    ImGui::SeparatorText("Transactions");
    ImGui::BeginChild("##history_transactions_child", ImVec2(0.0f, 0.0f), false);
    ui::panels::ShowHistoryTimelineTransactions(model, callbacks);
    ImGui::EndChild();
}

} // namespace app::areas
