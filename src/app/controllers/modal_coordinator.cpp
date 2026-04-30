#include "app/controllers/modal_coordinator.h"

namespace app::controllers {

void ModalCoordinator::RequestClose() {
    close_requested = true;
}

bool ModalCoordinator::ConsumeCloseRequest() {
    const bool requested = close_requested;
    close_requested = false;
    return requested;
}

void ModalCoordinator::ApplyModalRequest(const ModalRequestEvent& event) {
    switch (event.kind) {
        case ModalKind::NotImplemented:
            show_not_implemented_modal = event.open;
            if (!event.message.empty()) not_implemented_feature = event.message;
            break;
        case ModalKind::SaveBeforeExit:
            show_save_before_exit_modal = event.open;
            break;
        case ModalKind::ReviewerNamePrompt:
            show_reviewer_name_prompt = event.open;
            break;
        case ModalKind::Preferences:
            show_preferences_window = event.open;
            break;
        case ModalKind::ThemeTweaks:
            show_theme_tweak_window = event.open;
            break;
        default:
            break;
    }
}

}  // namespace app::controllers