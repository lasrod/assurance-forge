#pragma once

// Builds the Draft Changes panel's view model from the workspace and its
// materialization, and renders it.
//
// Everything the panel shows about a group -- what it touches, whether it can be
// accepted right now, what accepting it would also accept -- is derived here
// against the materialized working model, so `ui` decides nothing and the panel
// cannot disagree with the canvas beside it.

#include "core/terminology_package_service.h"
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
    // Takes the user to what the group changed, in whichever view can show it.
    std::function<void(const std::string& group_id)> focus_group;
};

// Where clicking a group's row should take the user.
//
// A row is a way into the change, not only a description of one -- but only the
// view that can actually show the change is worth going to. Sending a
// terminology group to the GSN canvas lands on a diagram that deliberately does
// not draw terms, so the click reads as broken.
enum class DraftGroupFocusKind {
    // Nothing to go to: the change is only readable on the row itself, so the
    // click leaves the user where they are rather than moving them somewhere it
    // cannot be seen.
    None,
    Canvas,
    Terminology,
};

struct DraftGroupFocus {
    DraftGroupFocusKind kind = DraftGroupFocusKind::None;
    std::string element_id;
    core::TerminologyPackageRef package_ref;
    core::TerminologyTermRef term_ref;
};

// Decides where a row's click goes, from the group's first changed element.
//
// A term resolves to the terminology view, but only once it is part of the
// accepted glossary: that view reads accepted terminology, so a term this draft
// has not had promoted yet is not in it and going there would report the term
// missing. Until then the row's own glossary lines are the only place it can be
// read, and the click stays put.
//
// Separate from the runtime so it can be tested without a window: which view
// shows a change is a decision, not a rendering detail.
DraftGroupFocus ResolveDraftGroupFocus(const AppRuntimeState& state, const std::string& group_id);

// Derives the panel model. Separate from the render so it can be tested without
// an ImGui context -- the promotability of a change to a safety argument is not
// a rendering detail.
ui::panels::DraftChangesPanelModel BuildDraftChangesPanelModel(AppRuntimeState& state);

void RenderDraftChangesAreaContent(AppRuntimeState& state, const DraftChangesAreaCallbacks& callbacks);

} // namespace app::areas
