#pragma once

#include "app/areas/modal_host.h"
#include "core/string_utils.h"
#include "ui/imgui_buffer_utils.h"

namespace app {
struct AppRuntimeState;
}

namespace app::areas {

using core::TrimWhitespace;
using ui::CopyToBuffer;

class ModalHost {
public:
    ModalHost(AppRuntimeState& state, bool& done, const ModalHostCallbacks& callbacks)
        : state_(state), done_(done), callbacks_(callbacks) {}

    void Render();

private:
    void RenderPreferencesWindow();
    void RenderNotImplementedModal();
    void RenderRemoveConfirmModal();
    void RenderDeleteReviewItemConfirmModal();
    void RenderStartupProjectWindow();
    void RenderCreateProjectModal();
    void RenderProjectFileNameModal();
    void RenderProjectLoadReportModal();
    void RenderSaveBeforeExitModal();
    void RenderSaveBeforeProjectFileOpenModal();
    void RenderCreateTerminologyPackageModal();
    void RenderCreatePatternModal();
    void RenderDeleteTerminologyPackageModal();
    void RenderTerminologyTermEditorModal();
    void RenderQuickDefineTermModal();
    void RenderDeleteTerminologyTermModal();
    void RenderTerminologyCategoryEditorModal();
    void RenderDeleteTerminologyCategoryModal();
    void RenderReviewerNamePromptModal();

    AppRuntimeState& state_;
    bool& done_;
    const ModalHostCallbacks& callbacks_;
};

} // namespace app::areas
