#pragma once

#include "core/terminology_package_service.h"
#include "legacy_sacm/sacm_model.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ui::panels {

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