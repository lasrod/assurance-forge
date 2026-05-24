#include "app/areas/history_panel_area.h"

#include "app/app_runtime_state.h"
#include "core/audit/audit_diff.h"
#include "core/audit/event_store.h"
#include "ui/panels/history_timeline_panel.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace app::areas {

namespace {

WorkbenchState::ArgumentPackageCanvasTab* FindActiveCanvasTab(AppRuntimeState& state) {
    const std::string& key = state.workbench.active_argument_package_canvas_key;
    if (key.empty())
        return nullptr;
    for (auto& tab : state.workbench.argument_package_canvas_tabs) {
        if (tab.key == key)
            return &tab;
    }
    return nullptr;
}

void SetPreviewSequenceOnActiveTab(AppRuntimeState& state, std::optional<std::uint64_t> seq) {
    WorkbenchState::ArgumentPackageCanvasTab* tab = FindActiveCanvasTab(state);
    if (!tab)
        return;
    tab->timeline.preview_sequence = seq;
    tab->selected_transaction_sequence = seq;
}

std::string ToLower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty())
        return true;
    return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
}

} // namespace

void RenderHistoryPanelContent(AppRuntimeState& state, const HistoryPanelAreaCallbacks& /*callbacks*/) {
    if (!state.app_state.current_project.has_value()) {
        ImGui::TextDisabled("No project is currently open.");
        return;
    }

    const core::AssuranceProject& project = state.app_state.current_project.value();

    std::string error;
    auto store = core::audit::EventStore::Open(project.rootPath, error);
    std::vector<core::audit::AuditTransaction> transactions;
    bool has_audit = false;
    if (store) {
        transactions = store->Transactions();
        has_audit = true;
    } else if (!error.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "Audit store error: %s", error.c_str());
    }

    WorkbenchState::ArgumentPackageCanvasTab* active_tab = FindActiveCanvasTab(state);
    std::optional<std::uint64_t> selected_seq;
    if (active_tab)
        selected_seq = active_tab->timeline.preview_sequence;

    // --- Filter bar ---
    // Author (substring, case-insensitive) and Command (exact match) filters
    // live in WorkbenchState alongside the element-id filter (which is set by
    // the Inspector's "View Element History" action). Collect the distinct
    // command names from the full transaction list to populate the dropdown.
    std::set<std::string> command_names;
    for (const core::audit::AuditTransaction& tx : transactions) {
        if (!tx.command_name.empty())
            command_names.insert(tx.command_name);
    }

    {
        ImGui::PushID("##history_filter_bar");
        char author_buf[128];
        std::snprintf(author_buf, sizeof(author_buf), "%s", state.workbench.history_filter_author.c_str());
        ImGui::Text("Filter:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputTextWithHint("##author", "author", author_buf, sizeof(author_buf)))
            state.workbench.history_filter_author = author_buf;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        const char* current_cmd =
            state.workbench.history_filter_command.empty() ? "(any command)"
                                                            : state.workbench.history_filter_command.c_str();
        if (ImGui::BeginCombo("##command", current_cmd)) {
            if (ImGui::Selectable("(any command)", state.workbench.history_filter_command.empty()))
                state.workbench.history_filter_command.clear();
            for (const std::string& cmd : command_names) {
                const bool selected = (state.workbench.history_filter_command == cmd);
                if (ImGui::Selectable(cmd.c_str(), selected))
                    state.workbench.history_filter_command = cmd;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        const bool any_filter_active = !state.workbench.history_filter_author.empty() ||
                                       !state.workbench.history_filter_command.empty() ||
                                       !state.workbench.history_filter_element_id.empty();
        ImGui::SameLine();
        ImGui::BeginDisabled(!any_filter_active);
        if (ImGui::SmallButton("Clear all")) {
            state.workbench.history_filter_author.clear();
            state.workbench.history_filter_command.clear();
            state.workbench.history_filter_element_id.clear();
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    // Build the filtered transaction list. A transaction passes when it
    // satisfies every active filter (AND-combined).
    const std::string& filter_element = state.workbench.history_filter_element_id;
    const std::string& filter_author = state.workbench.history_filter_author;
    const std::string& filter_command = state.workbench.history_filter_command;
    const bool filter_active =
        !filter_element.empty() || !filter_author.empty() || !filter_command.empty();
    std::vector<core::audit::AuditTransaction> filtered_transactions;
    if (filter_active) {
        filtered_transactions.reserve(transactions.size());
        for (const core::audit::AuditTransaction& tx : transactions) {
            if (!filter_author.empty() && !ContainsCaseInsensitive(tx.author, filter_author))
                continue;
            if (!filter_command.empty() && tx.command_name != filter_command)
                continue;
            if (!filter_element.empty()) {
                const core::audit::AuditChangeSet cs = core::audit::ComputeChangeSet(tx);
                if (!cs.added.count(filter_element) && !cs.modified.count(filter_element) &&
                    !cs.deleted.count(filter_element))
                    continue;
            }
            filtered_transactions.push_back(tx);
        }
    }
    const std::vector<core::audit::AuditTransaction>& visible_transactions =
        filter_active ? filtered_transactions : transactions;

    // Toolbar: revision summary + return-to-live.
    if (has_audit && !transactions.empty()) {
        const std::uint64_t latest_seq = transactions.back().transaction_sequence;
        const std::uint64_t current = selected_seq.value_or(latest_seq);
        ImGui::Text("Transactions: %zu   |   Current: Tx %llu / %llu",
                    transactions.size(),
                    static_cast<unsigned long long>(current),
                    static_cast<unsigned long long>(latest_seq));
        ImGui::SameLine();
        ImGui::BeginDisabled(!selected_seq.has_value());
        if (ImGui::SmallButton("Return to live"))
            SetPreviewSequenceOnActiveTab(state, std::nullopt);
        ImGui::EndDisabled();
        if (!active_tab) {
            ImGui::SameLine();
            ImGui::TextDisabled(" (open a package canvas to preview)");
        }
        if (filter_active) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f),
                               " | Showing %zu of %zu (filtered)",
                               filtered_transactions.size(),
                               transactions.size());
        }
        ImGui::Separator();
    }

    ui::panels::HistoryTimelinePanelModel model;
    model.has_audit_store = has_audit;
    model.transactions = &visible_transactions;
    model.selected_sequence = selected_seq;

    ui::panels::HistoryTimelinePanelCallbacks callbacks_in;
    callbacks_in.on_select_sequence = [&state](std::uint64_t seq) {
        SetPreviewSequenceOnActiveTab(state, seq);
    };
    callbacks_in.on_return_to_live = [&state]() {
        SetPreviewSequenceOnActiveTab(state, std::nullopt);
    };

    ui::panels::ShowHistoryTimelineTransactions(model, callbacks_in);
}

} // namespace app::areas
