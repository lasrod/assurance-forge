#include "ui/ui_state.h"

namespace ui {

UiState& GetUiState() {
    static UiState instance;
    return instance;
}

}  // namespace ui
