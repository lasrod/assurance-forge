#pragma once

#include "sacm/sacm_package_tree.h"

#include <filesystem>

namespace ui::panels {

void ShowPackageDetailsPanel(const sacm::SacmPackageTreeNode* package_node,
                             const std::filesystem::path& source_file_path);

} // namespace ui::panels
