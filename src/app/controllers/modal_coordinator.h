#pragma once

#include "app/app_events.h"

#include <string>

namespace app::controllers {

class ModalCoordinator {
public:
    bool show_not_implemented_modal = false;
    std::string not_implemented_feature;
    bool show_save_before_exit_modal = false;
    bool close_requested = false;
    bool show_reviewer_name_prompt = false;
    bool show_preferences_window = false;
    bool show_theme_tweak_window = false;

    void RequestClose();
    void CancelClose();
    bool ConsumeCloseRequest();
    void ApplyModalRequest(const ModalRequestEvent& event);
};

} // namespace app::controllers