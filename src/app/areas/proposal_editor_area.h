#pragma once

#include "core/element_factory.h"

#include <functional>
#include <string>

namespace app {
struct AppRuntimeState;
}

namespace app::areas {

struct ProposalEditorAreaCallbacks {
    std::function<void(core::RemoveMode)> remove_selected;
    std::function<bool()> refresh_preview;
    std::function<void(const std::string&)> set_status;
};

void RenderProposalElementEditor(AppRuntimeState& state, const ProposalEditorAreaCallbacks& callbacks);

} // namespace app::areas
