// Centred placeholder for a panel with nothing to show.
//
// A panel-filling emptiness announced by one line of grey text jammed into the
// top-left corner reads as a rendering failure rather than as "there is nothing
// here yet" -- the eye goes to the void, not to the sentence explaining it.
//
// Only for cases that own the whole region. An empty *list* inside a populated
// panel ("No arguments yet." under a tree section) is correctly inline and
// should stay that way: centring it would detach it from the section it
// describes.
#pragma once

#include <string>

namespace ui::widgets {

// `detail` is an optional second line explaining how to get out of the empty
// state. It is centred with the message rather than left to the caller, because
// a caller emitting its own paragraph afterwards gets a centred headline above
// a left-aligned block sitting near the bottom of the panel.
void EmptyState(const std::string& message, const std::string& detail = std::string());

} // namespace ui::widgets
