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
    // The library-backed part of the remove confirmation: what else this
    // delete reaches (SACM23-INT-002).
    void RenderRemovalPreview();
    void RenderDeleteReviewItemConfirmModal();
    void RenderStartupProjectWindow();
    void RenderCreateProjectModal();
    void RenderProjectFileNameModal();
    void RenderProjectLoadReportModal();
    void RenderSaveBeforeExitModal();
    void RenderSaveBeforeProjectFileOpenModal();
    void RenderCreateTerminologyPackageModal();
    void RenderDeleteTerminologyPackageModal();
    void RenderTerminologyTermEditorModal();
    void RenderQuickDefineTermModal();
    void RenderDeleteTerminologyTermModal();
    void RenderTermDeleteReferences();
    void RenderTerminologyCategoryEditorModal();
    void RenderDeleteTerminologyCategoryModal();
    void RenderReviewerNamePromptModal();
    // ADR 0014 gate 2: an MCP session asking for the open project. Shown as
    // soon as a request is pending; nothing is shared until the user answers.
    void RenderAccessRequestModal();
    // How far a draft rejection reaches: the changes built on top of the
    // rejected one go with it, or stay and wait for their author.
    void RenderDraftRejectionScopeModal();

    AppRuntimeState& state_;
    bool& done_;
    const ModalHostCallbacks& callbacks_;
};

} // namespace app::areas
