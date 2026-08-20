#pragma once

// The one place a staged SCCG finding becomes reader-facing text.
//
// `core` cannot include ui/i18n, so a StagedFinding carries its detail in
// English plus the check id and parameters that built it. This helper maps the
// check id to a translated template -- the established pattern for data-borne
// English -- so the Draft Changes and review panels show the reviewer's
// language instead of the English sentence raw. In English the output is
// byte-identical to the finding's own detail, and a test holds that: two
// phrasings of one finding would be a translation bug wearing the mask of a
// wording improvement.
//
// A check id this helper does not know falls back to the English detail. That
// keeps an unknown finding readable, but every check added to
// core/sccg/staged_checks.cpp is expected to add its template here and its
// translation to tools/i18n/regenerate_ja_po.py in the same change.

#include "core/sccg/staged_checks.h"

#include <string>

namespace app::areas {

// The finding's detail, in the display language.
std::string StagedFindingText(const core::sccg::StagedFinding& finding);

} // namespace app::areas
