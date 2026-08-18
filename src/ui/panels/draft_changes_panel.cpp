#include "ui/panels/draft_changes_panel.h"

#include "ui/i18n/localization.h"
#include "ui/theme.h"

#include "imgui.h"

#include <string>
#include <vector>

namespace ui::panels {
namespace {

// Joins ids for display. They are catalog identifiers, so they are shown
// verbatim and only the surrounding sentence is translated.
std::string JoinIds(const std::vector<std::string>& ids) {
    std::string joined;
    for (const std::string& id : ids) {
        if (!joined.empty())
            joined += ", ";
        joined += id;
    }
    return joined;
}

// The counts, as a sentence rather than a row of numbers.
//
// Only the non-zero parts appear. A group that adds two claims should not have
// to be read past "0 modified, 0 removed" to find that out, and a relationship
// change is called out separately because it can alter the meaning of an
// argument more than a reworded claim can.
std::string ChangeSummary(const DraftChangeRow& row) {
    std::vector<std::string> parts;
    if (row.elements_added > 0)
        parts.push_back(
            ui::i18n::trnf("{0} element added", "{0} elements added", row.elements_added, row.elements_added));
    if (row.elements_modified > 0) {
        parts.push_back(ui::i18n::trnf(
            "{0} element changed", "{0} elements changed", row.elements_modified, row.elements_modified));
    }
    if (row.elements_removed > 0) {
        parts.push_back(
            ui::i18n::trnf("{0} element removed", "{0} elements removed", row.elements_removed, row.elements_removed));
    }
    if (row.relationships_added > 0) {
        parts.push_back(ui::i18n::trnf(
            "{0} relationship added", "{0} relationships added", row.relationships_added, row.relationships_added));
    }
    if (row.relationships_removed > 0) {
        parts.push_back(ui::i18n::trnf("{0} relationship removed",
                                       "{0} relationships removed",
                                       row.relationships_removed,
                                       row.relationships_removed));
    }
    if (parts.empty()) {
        return ui::i18n::trnf("{0} operation, nothing applied yet",
                              "{0} operations, nothing applied yet",
                              row.operation_count,
                              row.operation_count);
    }

    std::string joined;
    for (const std::string& part : parts) {
        if (!joined.empty())
            joined += ", ";
        joined += part;
    }
    return joined;
}

void RenderLabelledList(const std::string& label, const std::vector<std::string>& values) {
    if (values.empty())
        return;
    ImGui::TextUnformatted(label.c_str());
    for (const std::string& value : values) {
        ImGui::Bullet();
        ImGui::TextWrapped("%s", value.c_str());
    }
}

void RenderRow(const DraftChangeRow& row, const DraftChangesPanelCallbacks& callbacks) {
    ImGui::PushID(row.group_id.c_str());

    const ui::Theme& theme = ui::GetTheme();
    const std::string header = row.title.empty() ? ui::i18n::trf("Untitled change ({0})", row.group_id) : row.title;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (row.selected)
        flags |= ImGuiTreeNodeFlags_Selected;
    const bool open = ImGui::TreeNodeEx("##draft_group", flags, "%s", header.c_str());
    if (ImGui::IsItemClicked() && callbacks.select_group)
        callbacks.select_group(row.group_id);

    if (!open) {
        ImGui::PopID();
        return;
    }

    // Provenance first. Whether an AI or a person wrote this is the first thing
    // a reviewer needs, and it is stated in words rather than carried by a tint
    // -- a reader who cannot distinguish the tint is still being asked to
    // approve a change to a safety argument.
    ImGui::TextUnformatted(ui::i18n::trf("Source: {0}", row.source_label).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ("(" + row.source_kind_label + ")").c_str());
    if (!row.source_session_id.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ui::i18n::trf("session {0}", row.source_session_id).c_str());
    }

    ImGui::TextUnformatted(ui::i18n::trf("State: {0}", row.state_label).c_str());
    ImGui::TextWrapped("%s", ChangeSummary(row).c_str());

    if (!row.summary.empty())
        ImGui::TextWrapped("%s", row.summary.c_str());
    if (!row.rationale.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted(AF_TR("Rationale:").c_str());
        ImGui::TextWrapped("%s", row.rationale.c_str());
    }

    if (!row.guideline_ids.empty())
        ImGui::TextWrapped("%s", ui::i18n::trf("Guidelines: {0}", JoinIds(row.guideline_ids)).c_str());
    if (!row.review_item_ids.empty())
        ImGui::TextWrapped("%s", ui::i18n::trf("Review items: {0}", JoinIds(row.review_item_ids)).c_str());

    if (!row.glossary_lines.empty()) {
        ImGui::Spacing();
        RenderLabelledList(AF_TR("Glossary:"), row.glossary_lines);
    }

    ImGui::Spacing();
    RenderLabelledList(AF_TR("Depends on:"), row.depends_on_titles);
    // Named before the button is pressed, never after. Widening a selection
    // silently is the failure the closure exists to prevent.
    RenderLabelledList(AF_TR("Accepting this also accepts:"), row.also_accepts_titles);

    if (!row.findings.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted(AF_TR("Findings (advisory):").c_str());
        for (const std::string& finding : row.findings) {
            ImGui::Bullet();
            ImGui::TextWrapped("%s", finding.c_str());
        }
    }

    if (row.needs_attention) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.attention));
        ImGui::TextWrapped("%s",
                           AF_TR("A change this one was built on was rejected, so it is no longer applied. "
                                 "Its author can retarget it at what remains.")
                               .c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    // Disabled rather than hidden, with the reason on the tooltip. A button that
    // silently does nothing was the reported defect in the flow this replaces.
    ImGui::BeginDisabled(!row.promotable);
    if (ImGui::Button(AF_TR("Accept").c_str()) && callbacks.accept_group)
        callbacks.accept_group(row.group_id);
    ImGui::EndDisabled();
    if (!row.promotable && !row.blocked_reason.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", row.blocked_reason.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button(AF_TR("Reject").c_str()) && callbacks.reject_group)
        callbacks.reject_group(row.group_id);

    if (!row.promotable && !row.blocked_reason.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.attention));
        ImGui::TextWrapped("%s", row.blocked_reason.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::TreePop();
    ImGui::Separator();
    ImGui::PopID();
}

} // namespace

void RenderDraftChangesPanel(const DraftChangesPanelModel& model, const DraftChangesPanelCallbacks& callbacks) {
    if (!model.has_workspace || model.rows.empty()) {
        ImGui::TextDisabled("%s", AF_TR("No unaccepted changes. The argument on screen is the accepted one.").c_str());
        return;
    }

    const ui::Theme& theme = ui::GetTheme();
    if (!model.workspace_blocked_reason.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.attention));
        ImGui::TextWrapped("%s", model.workspace_blocked_reason.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
    // The last refusal, in full, above the rows it was about. Pressing Accept
    // and watching the count stay where it was is the moment this answers.
    if (!model.accept_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.attention));
        ImGui::TextWrapped("%s", ui::i18n::trf("The last accept did not happen: {0}", model.accept_error).c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    const std::size_t count = model.rows.size();
    ImGui::TextUnformatted(
        ui::i18n::trnf("{0} unaccepted change", "{0} unaccepted changes", static_cast<int>(count), count).c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!model.workspace_blocked_reason.empty());
    if (ImGui::Button(AF_TR("Accept all").c_str()) && callbacks.accept_all)
        callbacks.accept_all();
    ImGui::EndDisabled();
    ImGui::Separator();

    for (const DraftChangeRow& row : model.rows)
        RenderRow(row, callbacks);
}

} // namespace ui::panels
