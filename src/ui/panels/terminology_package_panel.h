#pragma once

#include "core/drafts/draft_change_index.h"
#include "core/terminology_package_service.h"
#include "legacy_sacm/sacm_model.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ui::panels {

// How the working draft changed one glossary row (a term or a category), so
// the panel can badge it. A removed row has no row to badge; it is counted in
// the notice instead.
struct TerminologyDraftMark {
    std::string element_id;
    core::drafts::DraftElementChange change = core::drafts::DraftElementChange::Unchanged;
    // For a changed row, the projected fields that differ ("description",
    // "category_refs", ...), as the comparison names them.
    std::vector<std::string> fields;
};

struct TerminologyPackagePanelModel {
    const sacm::TerminologyPackage* package = nullptr;
    std::filesystem::path source_file_path;
    char* name_buffer = nullptr;
    std::size_t name_buffer_size = 0;
    char* description_buffer = nullptr;
    std::size_t description_buffer_size = 0;
    bool can_delete = false;
    std::string delete_block_reason;
    core::TerminologyTermRef selected_term_ref;
    core::TerminologyCategoryRef selected_category_ref;
    char* search_buffer = nullptr;
    std::size_t search_buffer_size = 0;
    char* category_filter_buffer = nullptr;
    std::size_t category_filter_buffer_size = 0;
    std::vector<core::TerminologyTermIssue> term_issues;
    std::vector<core::TerminologyTermUsageSummary> term_usage_summaries;
    std::vector<core::TerminologyCategoryUsageSummary> category_usage_summaries;

    // Set while the glossary shown is the working draft's rather than the
    // accepted one (ADR 0016): the line that says so, and how each row differs
    // from the accepted glossary. Both empty when the accepted glossary is shown.
    std::string working_draft_notice;
    std::vector<TerminologyDraftMark> draft_marks;

    // Set while a draft document exists (ADR 0016): terms and categories the
    // user adds, changes or deletes go into the working draft, and the panel
    // says so above its controls before the first click.
    std::string draft_edit_notice;

    // What the draft cannot take -- the package's own name and description,
    // deleting the package, deleting a category -- writes to the accepted
    // document, which the draft no longer descends from, and would be undone by
    // the accept. Those controls are disabled with the reason; term and
    // category editing, reading, searching and finding usages stay available.
    bool package_edits_locked = false;
    std::string package_edits_locked_reason;
};

// A persisted terminology-ignore decision shown in the panel's "Ignored terms"
// section so the user can review and restore (un-ignore) it.
struct IgnoredTerminologyEntry {
    std::string element_id;
    std::string term;
};

struct TerminologyPackagePanelCallbacks {
    std::function<void()> apply_changes;
    std::function<void()> delete_package;
    std::function<void()> add_term;
    std::function<void(const core::TerminologyTermRef&)> select_term;
    std::function<void(const core::TerminologyTermRef&)> edit_term;
    std::function<void(const core::TerminologyTermRef&)> delete_term;
    std::function<void(const core::TerminologyTermRef&)> find_term_usages;
    std::function<void(const std::string&)> set_category_filter;
    std::function<void()> add_category;
    std::function<void(const core::TerminologyCategoryRef&)> select_category;
    std::function<void(const core::TerminologyCategoryRef&)> edit_category;
    std::function<void(const core::TerminologyCategoryRef&)> delete_category;
    std::function<void()> seed_recommended_categories;
    // Ignored-terms management. list returns the project's persisted ignores;
    // restore un-ignores a single (element, term) entry.
    std::function<std::vector<IgnoredTerminologyEntry>()> list_ignored_terms;
    std::function<void(const std::string& element_id, const std::string& term)> restore_ignored_term;
};

void ShowTerminologyPackagePanel(TerminologyPackagePanelModel model, const TerminologyPackagePanelCallbacks& callbacks);

} // namespace ui::panels