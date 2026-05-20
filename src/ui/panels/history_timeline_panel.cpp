#include "ui/panels/history_timeline_panel.h"

#include "core/audit/audit_diff.h"
#include "ui/theme.h"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace ui::panels {

namespace {

std::string SummarizeTransaction(const core::audit::AuditTransaction& tx) {
    const core::audit::AuditChangeSet cs = core::audit::ComputeChangeSet(tx);
    std::string summary;
    if (!cs.added.empty())
        summary += "+" + std::to_string(cs.added.size());
    if (!cs.modified.empty()) {
        if (!summary.empty()) summary += " ";
        summary += "~" + std::to_string(cs.modified.size());
    }
    if (!cs.deleted.empty()) {
        if (!summary.empty()) summary += " ";
        summary += "-" + std::to_string(cs.deleted.size());
    }
    if (summary.empty())
        summary = "(no element changes)";
    return summary;
}

void RenderEmptyState(const HistoryTimelinePanelModel& model) {
    if (!model.has_audit_store) {
        ImGui::TextDisabled("This project does not have an audit store yet.");
        ImGui::TextWrapped(
            "An audit log is created automatically the first time a model-mutating command "
            "is recorded for a SACM file in this project.");
        return;
    }
    ImGui::TextDisabled("No transactions have been recorded yet.");
    ImGui::TextWrapped(
        "Open a SACM model and use any model-mutating action (add or remove a node) — "
        "each command will appear here.");
}

} // namespace

void ShowHistoryTimelinePanel(const HistoryTimelinePanelModel& model,
                              const HistoryTimelinePanelCallbacks& callbacks) {
    if (!model.has_audit_store || !model.transactions || model.transactions->empty()) {
        RenderEmptyState(model);
        return;
    }
    ShowHistoryTimelineHeader(model, callbacks);
    ImGui::Spacing();
    ImGui::SeparatorText("Transactions");
    ShowHistoryTimelineTransactions(model, callbacks);
}

void ShowHistoryTimelineHeader(const HistoryTimelinePanelModel& model,
                               const HistoryTimelinePanelCallbacks& callbacks) {
    if (!model.has_audit_store || !model.transactions || model.transactions->empty()) {
        RenderEmptyState(model);
        return;
    }

    const std::vector<core::audit::AuditTransaction>& transactions = *model.transactions;
    const std::uint64_t latest_seq = transactions.back().transaction_sequence;
    const std::uint64_t selected = model.selected_sequence.value_or(latest_seq);

    // Status banner.
    const bool live = !model.selected_sequence.has_value();
    const ImU32 banner_bg = live
        ? ui::GetTheme().surface_1
        : ui::LerpColor(ui::GetTheme().surface_1, ui::GetTheme().attention, 0.18f);
    const float banner_h = ImGui::GetStyle().WindowPadding.y * 2.0f +
                           ImGui::GetTextLineHeightWithSpacing() * 2.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(banner_bg));
    ImGui::BeginChild("##history_banner", ImVec2(0.0f, banner_h), true, ImGuiWindowFlags_NoScrollbar);
    if (live) {
        ImGui::TextUnformatted("LIVE — showing the latest recorded state.");
        ImGui::TextDisabled("%llu transaction(s) recorded.",
                            static_cast<unsigned long long>(transactions.size()));
    } else {
        ImGui::TextUnformatted("HISTORICAL VIEW (read-only)");
        ImGui::SameLine();
        if (ImGui::SmallButton("Return to live") && callbacks.on_return_to_live)
            callbacks.on_return_to_live();
        ImGui::TextDisabled("Reconstructed at transaction %llu of %llu.",
                            static_cast<unsigned long long>(selected),
                            static_cast<unsigned long long>(latest_seq));
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // Slider over [0, latest]. 0 = initial snapshot, latest = current.
    int slider_value = static_cast<int>(selected);
    const int slider_min = 0;
    const int slider_max = static_cast<int>(latest_seq);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt("##history_slider", &slider_value, slider_min, slider_max,
                         "Transaction %d", ImGuiSliderFlags_AlwaysClamp)) {
        if (callbacks.on_select_sequence) {
            const std::uint64_t target = static_cast<std::uint64_t>(std::clamp(slider_value, slider_min, slider_max));
            callbacks.on_select_sequence(target);
        }
    }

    ImGui::Spacing();

    // Reconstructed-state summary.
    ImGui::SeparatorText("Reconstructed state");
    if (!model.reconstruction_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::GetTheme().attention));
        ImGui::TextWrapped("Reconstruction failed: %s", model.reconstruction_error.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::Text("Elements: %zu", model.reconstructed_element_count);
        if (model.reconstructed_canonical_hash.empty()) {
            ImGui::TextDisabled("Canonical hash: (pending)");
        } else {
            ImGui::TextDisabled("Canonical hash: %s", model.reconstructed_canonical_hash.c_str());
        }
    }
}

void ShowHistoryTimelineTransactions(const HistoryTimelinePanelModel& model,
                                     const HistoryTimelinePanelCallbacks& callbacks) {
    if (!model.has_audit_store || !model.transactions || model.transactions->empty())
        return;

    const std::vector<core::audit::AuditTransaction>& transactions = *model.transactions;
    const std::uint64_t latest_seq = transactions.back().transaction_sequence;
    const std::uint64_t selected = model.selected_sequence.value_or(latest_seq);

    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##history_transactions", 5, table_flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Timestamp (UTC)", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Author", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Changes", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // Iterate newest-first so the most recent activity is on top.
        for (auto it = transactions.rbegin(); it != transactions.rend(); ++it) {
            const core::audit::AuditTransaction& tx = *it;
            const bool is_selected = (tx.transaction_sequence == selected);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(tx.transaction_sequence));
            char label[32];
            std::snprintf(label, sizeof(label), "%llu",
                          static_cast<unsigned long long>(tx.transaction_sequence));
            if (ImGui::Selectable(label, is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                if (callbacks.on_select_sequence)
                    callbacks.on_select_sequence(tx.transaction_sequence);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(tx.timestamp.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(tx.author.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(tx.command_name.c_str());
            ImGui::TableSetColumnIndex(4);
            const std::string changes = SummarizeTransaction(tx);
            ImGui::TextUnformatted(changes.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

} // namespace ui::panels
