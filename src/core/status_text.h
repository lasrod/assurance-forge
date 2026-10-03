// A status message as its English msgid plus runtime arguments.
//
// `core` may not include `ui/i18n` (the layer rule), so it cannot translate the
// messages it reports. It records what it said instead -- the msgid and the
// values that fill it -- and the layer that shows the message translates the
// msgid and fills in the same values (#252).
#pragma once

#include <string>
#include <string_view>
#include <vector>

// Marks an English literal as a msgid without translating it -- gettext's N_().
// For text stored in English and translated where it is shown: the catalogue
// extractor needs to see the literal, and AF_TR(variable) is invisible to it.
// Defined here rather than in `ui/i18n` so the layers below `ui` can mark their
// msgids too.
#define AF_TR_NOOP(text) (text)

namespace core {

struct StatusText {
    std::string msgid;
    std::vector<std::string> arguments;
};

// Fills the positional placeholders ({0}, {1}, ...) of `pattern` from
// `arguments`. `{{` and `}}` are literal braces, as in std::format. A
// placeholder that is malformed or names a missing argument is left as written
// rather than throwing, so a bad translation can never crash the UI. Argument
// text is inserted as-is and never rescanned, so a file path containing braces
// is safe.
std::string FormatStatusText(std::string_view pattern, const std::vector<std::string>& arguments);

} // namespace core
