#include "ui/panels/terminology_package_panel.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>

namespace ui::panels {
namespace {

void RenderDisabledAction(const char* label) {
    ImGui::BeginDisabled();
    ImGui::Button(label);
    ImGui::EndDisabled();
}

core::TerminologyTermRef RefFor(const sacm::Term& term) {
    return core::TerminologyTermRef{term.id, term.gid};
}

bool SameRef(const core::TerminologyTermRef& left, const core::TerminologyTermRef& right) {
    if (!left.id.empty() && !right.id.empty() && left.id == right.id)
        return true;
    if (!left.gid.empty() && !right.gid.empty() && left.gid == right.gid)
        return true;
    return false;
}

std::string JoinCategoryRefs(const std::vector<std::string>& refs) {
    std::string result;
    for (const auto& ref : refs) {
        if (ref.empty())
            continue;
        if (!result.empty())
            result += ", ";
        result += ref;
    }
    return result;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

bool ContainsInsensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty())
        return true;
    return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
}

bool MatchesFilter(const sacm::Term& term, const std::string& filter) {
    if (filter.empty())
        return true;
    return ContainsInsensitive(term.value, filter) || ContainsInsensitive(term.name, filter) ||
           ContainsInsensitive(term.description, filter) || ContainsInsensitive(JoinCategoryRefs(term.category_refs), filter) ||
           ContainsInsensitive(term.externalReference, filter) || ContainsInsensitive(term.origin, filter);
}

int UsageCountFor(const core::TerminologyTermRef& ref,
                  const std::vector<core::TerminologyTermUsageSummary>& summaries) {
    for (const auto& summary : summaries) {
        if (SameRef(summary.term_ref, ref))
            return summary.count;
    }
    return 0;
}

std::vector<core::TerminologyTermIssue> IssuesFor(
    const core::TerminologyTermRef& ref, const std::vector<core::TerminologyTermIssue>& issues) {
    std::vector<core::TerminologyTermIssue> result;
    for (const auto& issue : issues) {
        if (SameRef(issue.term_ref, ref))
            result.push_back(issue);
    }
    return result;
}

ImVec4 IssueColor(core::TerminologyTermIssueSeverity severity) {
    switch (severity) {
    case core::TerminologyTermIssueSeverity::Error:
        return ImVec4(0.9f, 0.25f, 0.2f, 1.0f);
    case core::TerminologyTermIssueSeverity::Warning:
        return ImVec4(0.95f, 0.65f, 0.15f, 1.0f);
    case core::TerminologyTermIssueSeverity::Info:
        return ImVec4(0.45f, 0.55f, 0.7f, 1.0f);
    }
    return ImVec4(0.45f, 0.55f, 0.7f, 1.0f);
}

void RenderIssueMarker(const std::vector<core::TerminologyTermIssue>& issues) {
    if (issues.empty())
        return;

    core::TerminologyTermIssueSeverity severity = core::TerminologyTermIssueSeverity::Info;
    for (const auto& issue : issues) {
        if (issue.severity == core::TerminologyTermIssueSeverity::Error) {
            severity = issue.severity;
            break;
        }
        if (issue.severity == core::TerminologyTermIssueSeverity::Warning)
            severity = issue.severity;
    }

    ImGui::SameLine();
    ImGui::TextColored(IssueColor(severity), "!");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        for (const auto& issue : issues)
            ImGui::TextUnformatted(issue.message.c_str());
        ImGui::EndTooltip();
    }
}

void RenderTermsTable(const TerminologyPackagePanelModel& model, const TerminologyPackagePanelCallbacks& callbacks) {
    if (!ImGui::BeginTable(
            "##terms_table", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
        return;

    ImGui::TableSetupColumn("Term", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("Full Name / Display Name", ImGuiTableColumnFlags_WidthFixed, 190.0f);
    ImGui::TableSetupColumn("Definition", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Categories", ImGuiTableColumnFlags_WidthFixed, 140.0f);
    ImGui::TableSetupColumn("External Reference", ImGuiTableColumnFlags_WidthFixed, 160.0f);
    ImGui::TableSetupColumn("Origin", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("Used In", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableHeadersRow();

    int visible_rows = 0;
    const std::string filter = model.search_buffer ? model.search_buffer : "";
    for (const auto& term : model.package->terms) {
        if (!MatchesFilter(term, filter))
            continue;

        ++visible_rows;
        const core::TerminologyTermRef ref = RefFor(term);
        const bool selected = SameRef(ref, model.selected_term_ref);
        const std::vector<core::TerminologyTermIssue> issues = IssuesFor(ref, model.term_issues);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(term.id.empty() ? term.gid.c_str() : term.id.c_str());
        if (ImGui::Selectable(term.value.empty() ? "<empty>" : term.value.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns) &&
            callbacks.select_term) {
            callbacks.select_term(ref);
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && callbacks.edit_term) {
            callbacks.edit_term(ref);
        }
        RenderIssueMarker(issues);
        ImGui::PopID();
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(term.name.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(term.description.c_str());
        ImGui::TableSetColumnIndex(3);
        const std::string categories = JoinCategoryRefs(term.category_refs);
        ImGui::TextUnformatted(categories.c_str());
        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(term.externalReference.c_str());
        ImGui::TableSetColumnIndex(5);
        ImGui::TextUnformatted(term.origin.c_str());
        ImGui::TableSetColumnIndex(6);
        ImGui::Text("%d", UsageCountFor(ref, model.term_usage_summaries));
    }

    if (visible_rows == 0) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("No terms");
    }

    ImGui::EndTable();
}

} // namespace

void ShowTerminologyPackagePanel(TerminologyPackagePanelModel model,
                                 const TerminologyPackagePanelCallbacks& callbacks) {
    ImGui::BeginChild("TerminologyPackagePanel", ImVec2(0, 0), false, ImGuiWindowFlags_None);

    if (!model.package) {
        ImGui::TextDisabled("No terminology package selected.");
        ImGui::EndChild();
        return;
    }

    ImGui::TextUnformatted("Terminology Package");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", model.package->id.empty() ? model.package->gid.c_str() : model.package->id.c_str());
    if (!model.source_file_path.empty())
        ImGui::TextDisabled("%s", model.source_file_path.string().c_str());

    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("Package name", model.name_buffer, model.name_buffer_size) && callbacks.apply_changes) {
        callbacks.apply_changes();
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextMultiline(
            "Package description", model.description_buffer, model.description_buffer_size, ImVec2(-1.0f, 96.0f)) &&
        callbacks.apply_changes) {
        callbacks.apply_changes();
    }

    ImGui::Spacing();
    if (!model.can_delete)
        ImGui::BeginDisabled();
    if (ImGui::Button("Delete Package") && callbacks.delete_package)
        callbacks.delete_package();
    if (!model.can_delete)
        ImGui::EndDisabled();
    if (!model.can_delete && !model.delete_block_reason.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", model.delete_block_reason.c_str());
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Terms");
    if (ImGui::Button("Add Term") && callbacks.add_term)
        callbacks.add_term();
    ImGui::SameLine();
    if (callbacks.edit_term && (model.selected_term_ref.id.empty() && model.selected_term_ref.gid.empty()))
        ImGui::BeginDisabled();
    if (ImGui::Button("Edit Term") && callbacks.edit_term)
        callbacks.edit_term(model.selected_term_ref);
    if (callbacks.edit_term && (model.selected_term_ref.id.empty() && model.selected_term_ref.gid.empty()))
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (callbacks.delete_term && (model.selected_term_ref.id.empty() && model.selected_term_ref.gid.empty()))
        ImGui::BeginDisabled();
    if (ImGui::Button("Delete Term") && callbacks.delete_term)
        callbacks.delete_term(model.selected_term_ref);
    if (callbacks.delete_term && (model.selected_term_ref.id.empty() && model.selected_term_ref.gid.empty()))
        ImGui::EndDisabled();
    ImGui::SameLine();
    RenderDisabledAction("Add Category##terms_add_category");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("Search", model.search_buffer, model.search_buffer_size);
    ImGui::Spacing();
    RenderTermsTable(model, callbacks);

    if (!model.package->expressions.empty()) {
        ImGui::TextDisabled("%d legacy expression entr%s are present and shown read-only by older tooling.",
                            static_cast<int>(model.package->expressions.size()),
                            model.package->expressions.size() == 1 ? "y" : "ies");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Categories");
    RenderDisabledAction("Add Category##categories_add_category");
    ImGui::Spacing();
    if (ImGui::BeginTable(
            "##categories_table", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Name");
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("No categories");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("-");
        ImGui::EndTable();
    }

    ImGui::EndChild();
}

} // namespace ui::panels