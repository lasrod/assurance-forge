#pragma once

#include "sacm/sacm_model.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

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
};

struct TerminologyPackagePanelCallbacks {
    std::function<void()> apply_changes;
    std::function<void()> delete_package;
};

void ShowTerminologyPackagePanel(TerminologyPackagePanelModel model,
                                 const TerminologyPackagePanelCallbacks& callbacks);

} // namespace ui::panels