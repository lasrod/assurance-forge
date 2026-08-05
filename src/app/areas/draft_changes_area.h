#pragma once

// Builds the Draft Changes panel's view model from the workspace and its
// materialization, and renders it.
//
// Everything the panel shows about a group -- what it touches, whether it can be
// accepted right now, what accepting it would also accept -- is derived here
// against the materialized working model, so `ui` decides nothing and the panel
// cannot disagree with the canvas beside it.

#include "ui/panels/draft_changes_panel.h"

#include <functional>
#include <string>
#include <vector>

namespace app {
struct AppRuntimeState;
}

namespace app::areas {

struct DraftChangesAreaCallbacks {
    std::function<void(const std::string& group_id)> accept_group;
    std::function<void(const std::string& group_id)> reject_group;
    std::function<void()> accept_all;
    // Selects the group's first changed element and centres the canvas on it, so
    // a row is a way into the argument rather than only a description of one.
    std::function<void(const std::string& group_id)> focus_group;
};

// Derives the panel model. Separate from the render so it can be tested without
// an ImGui context -- the promotability of a change to a safety argument is not
// a rendering detail.
ui::panels::DraftChangesPanelModel BuildDraftChangesPanelModel(AppRuntimeState& state);

void RenderDraftChangesAreaContent(AppRuntimeState& state, const DraftChangesAreaCallbacks& callbacks);

} // namespace app::areas
