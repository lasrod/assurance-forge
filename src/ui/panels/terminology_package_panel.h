#pragma once

#include "core/terminology_package_service.h"
#include "sacm/sacm_model.h"

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
    char* search_buffer = nullptr;
    std::size_t search_buffer_size = 0;
    std::vector<core::TerminologyTermIssue> term_issues;
    std::vector<core::TerminologyTermUsageSummary> term_usage_summaries;
};

struct TerminologyPackagePanelCallbacks {
    std::function<void()> apply_changes;
    std::function<void()> delete_package;
    std::function<void()> add_term;
    std::function<void(const core::TerminologyTermRef&)> select_term;
    std::function<void(const core::TerminologyTermRef&)> edit_term;
    std::function<void(const core::TerminologyTermRef&)> delete_term;
};

void ShowTerminologyPackagePanel(TerminologyPackagePanelModel model, const TerminologyPackagePanelCallbacks& callbacks);

} // namespace ui::panels