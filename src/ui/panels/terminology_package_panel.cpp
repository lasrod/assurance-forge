#include "ui/panels/terminology_package_panel.h"

#include "core/string_utils.h"
#include "core/terminology_text_utils.h"
#include "imgui.h"
#include "ui/i18n/localization.h"
#include "ui/theme.h"
#include "ui/widgets/danger_button.h"

#include <algorithm>

namespace ui::panels {
namespace {

constexpr const char* kUncategorizedFilter = "__uncategorized";

core::TerminologyTermRef RefFor(const sacm::Term& term) {
    return core::TerminologyTermRef{term.id, term.gid};
}

core::TerminologyCategoryRef RefFor(const sacm::Category& category) {
    return core::TerminologyCategoryRef{category.id, category.gid};
}

bool SameRef(const core::TerminologyTermRef& left, const core::TerminologyTermRef& right) {
    if (!left.id.empty() && !right.id.empty() && left.id == right.id)
        return true;
    if (!left.gid.empty() && !right.gid.empty() && left.gid == right.gid)
        return true;
    return false;
}

bool SameRef(const core::TerminologyCategoryRef& left, const core::TerminologyCategoryRef& right) {
    if (!left.id.empty() && !right.id.empty() && left.id == right.id)
        return true;
    if (!left.gid.empty() && !right.gid.empty() && left.gid == right.gid)
        return true;
    return false;
}

std::string RefValue(const core::TerminologyCategoryRef& ref) {
    return !ref.id.empty() ? ref.id : ref.gid;
}

bool CategoryMatchesRef(const sacm::Category& category, const std::string& ref) {
    return (!category.id.empty() && category.id == ref) || (!category.gid.empty() && category.gid == ref);
}

bool TermHasCategoryRef(const sacm::TerminologyPackage& package,
                        const sacm::Term& term,
                        const std::string& category_ref) {
    for (const auto& ref : term.category_refs) {
        if (ref == category_ref)
            return true;
        for (const auto& category : package.categories) {
            if (CategoryMatchesRef(category, category_ref) && CategoryMatchesRef(category, ref))
                return true;
        }
    }
    return false;
}

std::string JoinCategoryNames(const sacm::TerminologyPackage& package, const std::vector<std::string>& refs) {
    std::string result;
    for (const auto& ref : refs) {
        if (ref.empty())
            continue;
        if (!result.empty())
            result += ", ";
        result += core::CategoryDisplayName(package, ref);
    }
    return result;
}

bool ContainsInsensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty())
        return true;
    return core::ToLower(haystack).find(core::ToLower(needle)) != std::string::npos;
}

bool MatchesFilter(const sacm::TerminologyPackage& package, const sacm::Term& term, const std::string& filter) {
    if (filter.empty())
        return true;
    return ContainsInsensitive(term.value, filter) || ContainsInsensitive(term.name, filter) ||
           ContainsInsensitive(term.description, filter) ||
           ContainsInsensitive(JoinCategoryNames(package, term.category_refs), filter) ||
           ContainsInsensitive(core::JoinCategoryRefs(term.category_refs), filter) ||
           ContainsInsensitive(term.externalReference, filter) || ContainsInsensitive(term.origin, filter);
}

bool MatchesCategoryFilter(const sacm::TerminologyPackage& package,
                           const sacm::Term& term,
                           const std::string& category_filter) {
    if (category_filter.empty())
        return true;
    if (category_filter == kUncategorizedFilter)
        return term.category_refs.empty();
    return TermHasCategoryRef(package, term, category_filter);
}

int UsageCountFor(const core::TerminologyTermRef& ref,
                  const std::vector<core::TerminologyTermUsageSummary>& summaries) {
    for (const auto& summary : summaries) {
        if (SameRef(summary.term_ref, ref))
            return summary.count;
    }
    return 0;
}

int UsageCountFor(const core::TerminologyCategoryRef& ref,
                  const std::vector<core::TerminologyCategoryUsageSummary>& summaries) {
    for (const auto& summary : summaries) {
        if (SameRef(summary.category_ref, ref))
            return summary.term_count;
    }
    return 0;
}

std::vector<core::TerminologyTermIssue> IssuesFor(const core::TerminologyTermRef& ref,
                                                  const std::vector<core::TerminologyTermIssue>& issues) {
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
        return ui::GetErrorColor();
    case core::TerminologyTermIssueSeverity::Warning:
        return ui::GetWarningColor();
    case core::TerminologyTermIssueSeverity::Info:
        return ui::GetInfoColor();
    }
    return ui::GetInfoColor();
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
        // Issue messages are produced as English msgids in core/; translate
        // at render time so they follow the current UI language.
        for (const auto& issue : issues)
            ImGui::TextUnformatted(AF_TR(issue.message).c_str());
        ImGui::EndTooltip();
    }
}

const TerminologyDraftMark* DraftMarkFor(const std::string& element_id,
                                         const std::vector<TerminologyDraftMark>& marks) {
    for (const TerminologyDraftMark& mark : marks) {
        if (!element_id.empty() && mark.element_id == element_id)
            return &mark;
    }
    return nullptr;
}

// The comparison names projected fields; the reader of a glossary knows them by
// the column they are shown in.
std::string DraftFieldLabel(const std::string& field) {
    if (field == "content")
        return AF_TR("term");
    if (field == "description")
        return AF_TR("definition");
    if (field == "name")
        return AF_TR("name");
    if (field == "category_refs")
        return AF_TR("categories");
    if (field == "external_reference")
        return AF_TR("external reference");
    if (field == "origin_ref")
        return AF_TR("origin");
    return field;
}

std::string JoinDraftFieldLabels(const std::vector<std::string>& fields) {
    std::string result;
    for (const std::string& field : fields) {
        if (!result.empty())
            result += ", ";
        result += DraftFieldLabel(field);
    }
    return result;
}

// A badge beside a row the working draft added or changed, so a reader can
// tell a definition an AI client proposed from one the case already holds.
void RenderDraftMark(const TerminologyDraftMark* mark) {
    if (mark == nullptr || mark->change == core::drafts::DraftElementChange::Unchanged)
        return;
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::GetTheme().accent), "%s", AF_TR("draft").c_str());
    if (!ImGui::IsItemHovered())
        return;
    ImGui::BeginTooltip();
    if (mark->change == core::drafts::DraftElementChange::Added) {
        ImGui::TextUnformatted(AF_TR("Added by the working draft; not yet accepted.").c_str());
    } else {
        ImGui::TextUnformatted(
            ui::i18n::trf("Changed by the working draft ({0}); not yet accepted.", JoinDraftFieldLabels(mark->fields))
                .c_str());
    }
    ImGui::EndTooltip();
}

void RenderTermsTable(const TerminologyPackagePanelModel& model, const TerminologyPackagePanelCallbacks& callbacks) {
    if (!ImGui::BeginTable(
            "##terms_table", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
        return;

    ImGui::TableSetupColumn(AF_TR("Term").c_str(), ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn(AF_TR("Full Name / Display Name").c_str(), ImGuiTableColumnFlags_WidthFixed, 190.0f);
    ImGui::TableSetupColumn(AF_TR("Definition").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(AF_TR("Categories").c_str(), ImGuiTableColumnFlags_WidthFixed, 140.0f);
    ImGui::TableSetupColumn(AF_TR("External Reference").c_str(), ImGuiTableColumnFlags_WidthFixed, 160.0f);
    ImGui::TableSetupColumn(AF_TR("Origin").c_str(), ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn(AF_TR("Used In").c_str(), ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableHeadersRow();

    int visible_rows = 0;
    const std::string filter = model.search_buffer ? model.search_buffer : "";
    const std::string category_filter = model.category_filter_buffer ? model.category_filter_buffer : "";
    for (std::size_t term_index = 0; term_index < model.package->terms.size(); ++term_index) {
        const auto& term = model.package->terms[term_index];
        if (!MatchesFilter(*model.package, term, filter) ||
            !MatchesCategoryFilter(*model.package, term, category_filter))
            continue;

        ++visible_rows;
        const core::TerminologyTermRef ref = RefFor(term);
        const bool selected = SameRef(ref, model.selected_term_ref);
        const std::vector<core::TerminologyTermIssue> issues = IssuesFor(ref, model.term_issues);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(static_cast<int>(term_index));
        const ImGuiSelectableFlags selectable_flags =
            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
        if (ImGui::Selectable(
                term.value.empty() ? AF_TR("<empty>").c_str() : term.value.c_str(), selected, selectable_flags) &&
            callbacks.select_term) {
            callbacks.select_term(ref);
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && callbacks.edit_term) {
            callbacks.edit_term(ref);
        }
        RenderDraftMark(DraftMarkFor(term.id, model.draft_marks));
        RenderIssueMarker(issues);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(term.name.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(term.description.c_str());
        ImGui::TableSetColumnIndex(3);
        const std::string categories = JoinCategoryNames(*model.package, term.category_refs);
        ImGui::TextUnformatted(categories.c_str());
        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(term.externalReference.c_str());
        ImGui::TableSetColumnIndex(5);
        ImGui::TextUnformatted(term.origin.c_str());
        ImGui::TableSetColumnIndex(6);
        const int usage_count = UsageCountFor(ref, model.term_usage_summaries);
        if (callbacks.find_term_usages) {
            const std::string label = std::to_string(usage_count) + "##find_usages";
            if (ImGui::SmallButton(label.c_str()))
                callbacks.find_term_usages(ref);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", AF_TR("Find usages").c_str());
        } else {
            ImGui::Text("%d", usage_count);
        }
        ImGui::PopID();
    }

    if (visible_rows == 0) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", AF_TR("No terms").c_str());
    }

    ImGui::EndTable();
}

std::string CategoryPreview(const TerminologyPackagePanelModel& model) {
    const std::string filter = model.category_filter_buffer ? model.category_filter_buffer : "";
    if (filter.empty())
        return AF_TR("All categories");
    if (filter == kUncategorizedFilter)
        return AF_TR("Uncategorized");
    return core::CategoryDisplayName(*model.package, filter);
}

void RenderCategoryFilter(const TerminologyPackagePanelModel& model,
                          const TerminologyPackagePanelCallbacks& callbacks) {
    const std::string active_filter = model.category_filter_buffer ? model.category_filter_buffer : "";
    const std::string preview = CategoryPreview(model);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo((AF_TR("Category filter") + "##category_filter").c_str(), preview.c_str())) {
        if (ImGui::Selectable(AF_TR("All categories").c_str(), active_filter.empty()) && callbacks.set_category_filter)
            callbacks.set_category_filter("");
        if (ImGui::Selectable(AF_TR("Uncategorized").c_str(), active_filter == kUncategorizedFilter) &&
            callbacks.set_category_filter)
            callbacks.set_category_filter(kUncategorizedFilter);
        for (const auto& category : model.package->categories) {
            const core::TerminologyCategoryRef ref = RefFor(category);
            const std::string value = RefValue(ref);
            const bool selected = active_filter == value;
            const std::string label = category.name.empty() ? value : category.name;
            if (ImGui::Selectable(label.c_str(), selected) && callbacks.set_category_filter)
                callbacks.set_category_filter(value);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void RenderCategoriesTable(const TerminologyPackagePanelModel& model,
                           const TerminologyPackagePanelCallbacks& callbacks) {
    if (!ImGui::BeginTable(
            "##categories_table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
        return;

    ImGui::TableSetupColumn(AF_TR("ID").c_str(), ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn(AF_TR("Name").c_str(), ImGuiTableColumnFlags_WidthFixed, 180.0f);
    ImGui::TableSetupColumn(AF_TR("Description").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(AF_TR("Used By").c_str(), ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableHeadersRow();

    for (const auto& category : model.package->categories) {
        const core::TerminologyCategoryRef ref = RefFor(category);
        const std::string id = RefValue(ref);
        const bool selected = SameRef(ref, model.selected_category_ref);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(id.c_str());
        if (ImGui::Selectable(id.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns) && callbacks.select_category)
            callbacks.select_category(ref);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && callbacks.edit_category)
            callbacks.edit_category(ref);
        RenderDraftMark(DraftMarkFor(category.id, model.draft_marks));
        ImGui::PopID();
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(category.name.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(category.description.c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%d", UsageCountFor(ref, model.category_usage_summaries));
    }

    if (model.package->categories.empty()) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", AF_TR("No categories").c_str());
    }

    ImGui::EndTable();
}

} // namespace

void ShowTerminologyPackagePanel(TerminologyPackagePanelModel model,
                                 const TerminologyPackagePanelCallbacks& callbacks) {
    ImGui::BeginChild("TerminologyPackagePanel", ImVec2(0, 0), false, ImGuiWindowFlags_None);

    if (!model.package) {
        ImGui::TextDisabled("%s", AF_TR("No terminology package selected.").c_str());
        ImGui::EndChild();
        return;
    }

    ImGui::TextUnformatted(AF_TR("Terminology Package").c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", model.package->id.empty() ? model.package->gid.c_str() : model.package->id.c_str());
    if (!model.source_file_path.empty())
        ImGui::TextDisabled("%s", model.source_file_path.string().c_str());

    ImGui::Separator();
    ImGui::Spacing();

    // Said in words above the rows, not implied by a badge: a glossary drawn
    // from a working draft is not the accepted safety case's glossary, and a
    // reader has to be able to tell before they quote a definition from it.
    if (!model.working_draft_notice.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::GetTheme().accent));
        ImGui::TextWrapped("%s", model.working_draft_notice.c_str());
        ImGui::PopStyleColor();
    }
    if (!model.draft_edit_notice.empty())
        ImGui::TextWrapped("%s", model.draft_edit_notice.c_str());
    // Only the package-level controls lock: the draft takes term and category
    // edits, so disabling those would hide an edit the user can make.
    const bool locked = model.package_edits_locked;
    if (locked && !model.package_edits_locked_reason.empty())
        ImGui::TextDisabled("%s", model.package_edits_locked_reason.c_str());
    if (!model.working_draft_notice.empty() || !model.draft_edit_notice.empty() || locked)
        ImGui::Spacing();

    const ImGuiInputTextFlags text_flags = locked ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText((AF_TR("Package name") + "##package_name").c_str(),
                         model.name_buffer,
                         model.name_buffer_size,
                         text_flags) &&
        callbacks.apply_changes && !locked) {
        callbacks.apply_changes();
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextMultiline((AF_TR("Package description") + "##package_description").c_str(),
                                  model.description_buffer,
                                  model.description_buffer_size,
                                  ImVec2(-1.0f, 96.0f),
                                  text_flags) &&
        callbacks.apply_changes && !locked) {
        callbacks.apply_changes();
    }

    ImGui::Spacing();
    const bool delete_disabled = !model.can_delete || locked;
    if (delete_disabled)
        ImGui::BeginDisabled();
    if (ui::widgets::DangerButton(AF_TR("Delete Package").c_str()) && callbacks.delete_package)
        callbacks.delete_package();
    if (delete_disabled)
        ImGui::EndDisabled();
    if (!model.can_delete && !model.delete_block_reason.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", AF_TR(model.delete_block_reason).c_str());
    }

    ImGui::Spacing();
    ImGui::SeparatorText(AF_TR("Terms").c_str());
    const bool no_term_selected = model.selected_term_ref.id.empty() && model.selected_term_ref.gid.empty();
    if (ImGui::Button(AF_TR("Add Term").c_str()) && callbacks.add_term)
        callbacks.add_term();
    ImGui::SameLine();
    const bool edit_term_disabled = callbacks.edit_term && no_term_selected;
    if (edit_term_disabled)
        ImGui::BeginDisabled();
    if (ImGui::Button(AF_TR("Edit Term").c_str()) && callbacks.edit_term)
        callbacks.edit_term(model.selected_term_ref);
    if (edit_term_disabled)
        ImGui::EndDisabled();
    ImGui::SameLine();
    const bool delete_term_disabled = callbacks.delete_term && no_term_selected;
    if (delete_term_disabled)
        ImGui::BeginDisabled();
    if (ui::widgets::DangerButton(AF_TR("Delete Term").c_str()) && callbacks.delete_term)
        callbacks.delete_term(model.selected_term_ref);
    if (delete_term_disabled)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (callbacks.find_term_usages && no_term_selected)
        ImGui::BeginDisabled();
    if (ImGui::Button(AF_TR("Find Usages").c_str()) && callbacks.find_term_usages)
        callbacks.find_term_usages(model.selected_term_ref);
    if (callbacks.find_term_usages && no_term_selected)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button((AF_TR("Add Category") + "##terms_add_category").c_str()) && callbacks.add_category)
        callbacks.add_category();
    ImGui::SameLine();
    RenderCategoryFilter(model, callbacks);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText((AF_TR("Search") + "##term_search").c_str(), model.search_buffer, model.search_buffer_size);
    ImGui::Spacing();
    RenderTermsTable(model, callbacks);

    if (!model.package->expressions.empty()) {
        ImGui::TextDisabled(
            "%s",
            ui::i18n::trnf("{0} legacy expression entry is present and shown read-only by older tooling.",
                           "{0} legacy expression entries are present and shown read-only by older tooling.",
                           static_cast<int>(model.package->expressions.size()),
                           static_cast<int>(model.package->expressions.size()))
                .c_str());
    }

    ImGui::Spacing();
    ImGui::SeparatorText(AF_TR("Categories").c_str());
    if (ImGui::Button((AF_TR("Add Category") + "##categories_add_category").c_str()) && callbacks.add_category)
        callbacks.add_category();
    ImGui::SameLine();
    const bool has_selected_category =
        !model.selected_category_ref.id.empty() || !model.selected_category_ref.gid.empty();
    const bool category_edit_disabled = !has_selected_category;
    if (category_edit_disabled)
        ImGui::BeginDisabled();
    if (ImGui::Button(AF_TR("Edit Category").c_str()) && callbacks.edit_category)
        callbacks.edit_category(model.selected_category_ref);
    if (category_edit_disabled)
        ImGui::EndDisabled();
    ImGui::SameLine();
    // A category delete cascades into the terms that carry it, which the draft
    // cannot express -- so this one stays with the package-level lock.
    const bool category_delete_disabled = locked || !has_selected_category;
    if (category_delete_disabled)
        ImGui::BeginDisabled();
    if (ui::widgets::DangerButton(AF_TR("Delete Category").c_str()) && callbacks.delete_category)
        callbacks.delete_category(model.selected_category_ref);
    if (category_delete_disabled)
        ImGui::EndDisabled();
    if (model.package->categories.empty() && callbacks.seed_recommended_categories) {
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Add Recommended").c_str()))
            callbacks.seed_recommended_categories();
    }
    ImGui::Spacing();
    RenderCategoriesTable(model, callbacks);

    if (callbacks.list_ignored_terms) {
        ImGui::Spacing();
        ImGui::SeparatorText(AF_TR("Ignored terms").c_str());
        const std::vector<IgnoredTerminologyEntry> ignored = callbacks.list_ignored_terms();
        if (ignored.empty()) {
            ImGui::TextDisabled("%s", AF_TR("No ignored terms.").c_str());
        } else {
            for (std::size_t index = 0; index < ignored.size(); ++index) {
                const IgnoredTerminologyEntry& entry = ignored[index];
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::Button(AF_TR("Restore").c_str()) && callbacks.restore_ignored_term)
                    callbacks.restore_ignored_term(entry.element_id, entry.term);
                ImGui::SameLine();
                ImGui::TextUnformatted(entry.term.c_str());
                if (!entry.element_id.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", entry.element_id.c_str());
                }
                ImGui::PopID();
            }
        }
    }

    ImGui::EndChild();
}

} // namespace ui::panels