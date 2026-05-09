#pragma once

#include "core/terminology_package_service.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace ui::panels {

struct TerminologyUsagesPanelModel {
    bool has_search = false;
    std::string term_value;
    std::string term_name;
    std::string message;
    std::string error;
    const std::vector<core::TerminologyTermUsage>* usages = nullptr;
    int selected_usage_index = -1;
};

struct TerminologyUsagesPanelCallbacks {
    std::function<void(std::size_t)> select_usage;
    std::function<void(std::size_t)> activate_usage;
};

void ShowTerminologyUsagesPanelContent(const TerminologyUsagesPanelModel& model,
                                       const TerminologyUsagesPanelCallbacks& callbacks);

} // namespace ui::panels
