#include "ui/panels/history_timeline_panel.h"

#include "core/audit/audit_diff.h"
#include "ui/theme.h"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
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

// Compact int slider modeled on `DrawOpinionSliderBar` in confidence_panel.cpp.
// Label on the left, "N / M" value on the right, draggable rounded bar with a
// circular handle. Snaps to integer values.
void DrawTransactionSliderBar(int& value, int min_v, int max_v,
                              const HistoryTimelinePanelCallbacks& callbacks) {
    const Theme& theme = GetTheme();
    value = std::clamp(value, min_v, max_v);

    ImGui::PushID("##transaction_slider_bar");
    const float line_height = ImGui::GetTextLineHeight();
    const float available_width = ImGui::GetContentRegionAvail().x;
    const float bar_height = std::max(8.0f, line_height * 0.52f);

    char value_text[48];
    std::snprintf(value_text, sizeof(value_text), "%d / %d", value, max_v);
    const float value_width = ImGui::CalcTextSize(value_text).x;
    const char* label = "Transaction";
    const float label_width = ImGui::CalcTextSize(label).x;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    const bool value_fits_on_label_line =
        label_width + ImGui::GetStyle().ItemSpacing.x + value_width <= available_width;
    if (value_fits_on_label_line) {
        const float value_x =
            std::max(label_width + ImGui::GetStyle().ItemSpacing.x, available_width - value_width);
        ImGui::SameLine(value_x);
    }
    ImGui::TextUnformatted(value_text);

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(24.0f, ImGui::GetContentRegionAvail().x), line_height + 2.0f);
    ImGui::InvisibleButton("##bar", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left) && max_v > min_v) {
        const float t = std::clamp((ImGui::GetIO().MousePos.x - cursor.x) / std::max(1.0f, size.x),
                                   0.0f, 1.0f);
        const int next_value =
            min_v + static_cast<int>(t * static_cast<float>(max_v - min_v) + 0.5f);
        if (next_value != value) {
            value = next_value;
            if (callbacks.on_select_sequence)
                callbacks.on_select_sequence(static_cast<std::uint64_t>(next_value));
        }
    }

    if (hovered || active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const float t = (max_v > min_v)
        ? static_cast<float>(value - min_v) / static_cast<float>(max_v - min_v)
        : 1.0f;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float y = cursor.y + (line_height - bar_height) * 0.5f;
    const ImVec2 mn(cursor.x, y);
    const ImVec2 mx(cursor.x + size.x, y + bar_height);
    const float fill_x = mn.x + size.x * t;
    const ImU32 color = theme.accent;
    draw_list->AddRectFilled(
        mn, mx, WithAlpha(theme.surface_3, hovered || active ? 0.86f : 0.60f), bar_height * 0.5f);
    draw_list->AddRectFilled(mn, ImVec2(fill_x, mx.y), WithAlpha(color, active ? 1.0f : 0.88f),
                             bar_height * 0.5f);
    draw_list->AddCircleFilled(ImVec2(fill_x, (mn.y + mx.y) * 0.5f), active ? 6.0f : 4.8f,
                               theme.text_primary, 18);
    draw_list->AddCircleFilled(ImVec2(fill_x, (mn.y + mx.y) * 0.5f), active ? 4.0f : 3.0f, color,
                               18);

    if (hovered || active)
        ImGui::SetTooltip("Drag to scrub transaction history");

    ImGui::PopID();
}

} // namespace

void ShowHistoryTimelineHeader(const HistoryTimelinePanelModel& model,
                               const HistoryTimelinePanelCallbacks& callbacks) {
    if (!model.has_audit_store || !model.transactions || model.transactions->empty()) {
        RenderEmptyState(model);
        return;
    }

    const std::vector<core::audit::AuditTransaction>& transactions = *model.transactions;
    const std::uint64_t latest_seq = transactions.back().transaction_sequence;
    const std::uint64_t selected = model.selected_sequence.value_or(latest_seq);

    int slider_value = static_cast<int>(selected);
    DrawTransactionSliderBar(slider_value, 0, static_cast<int>(latest_seq), callbacks);
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
