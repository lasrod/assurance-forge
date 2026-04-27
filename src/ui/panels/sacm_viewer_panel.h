#pragma once

#include "imgui.h"

#include "core/app_state.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace ui::panels {

struct SacmViewerPanelModel {
    core::AppState& app_state;
    char* dir_path_buf = nullptr;
    std::size_t dir_path_buf_size = 0;
    char* file_path_buf = nullptr;
    std::size_t file_path_buf_size = 0;
    std::vector<std::string>& xml_files;
    int& selected_file_idx;
    bool& show_overwrite_confirm;
};

struct SacmViewerPanelCallbacks {
    std::function<void()> scan_directory;
    std::function<void()> on_load_success;
    std::function<void()> on_load_failure;
};

void ShowSacmViewerPanel(float width,
                         float height,
                         float top_y,
                         ImGuiWindowFlags panel_flags,
                         SacmViewerPanelModel model,
                         const SacmViewerPanelCallbacks& callbacks);

}  // namespace ui::panels
