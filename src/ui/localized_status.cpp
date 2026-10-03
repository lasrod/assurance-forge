#include "ui/localized_status.h"

#include "core/app_state.h"
#include "core/status_text.h"
#include "ui/i18n/localization.h"

namespace ui {

std::string LocalizedStatusMessage(const core::AppState& app_state) {
    if (!app_state.status_source_is_current())
        return app_state.status_message;
    return core::FormatStatusText(i18n::tr(app_state.status_source.msgid), app_state.status_source.arguments);
}

} // namespace ui
