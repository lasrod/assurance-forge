#pragma once

#include "ai/ai_types.h"

#include <string>

namespace app {

// AI provider errors in the user's language (issue #451).
//
// `ai::ToString(AiErrorCode)` stays English: the `ai` layer cannot include
// `ui/i18n`, and the evaluation harness writes that text into its JSON records.
// These translate the same English at the one layer allowed to, and only for
// text shown on screen and never saved.

// The code's message, translated. Its English is `ai::ToString(code)` exactly.
std::string LocalizedAiErrorCode(ai::AiErrorCode code);

// What to show for a failure the provider reported as `code` with `message`.
// A message that only repeats the code's text -- how the provider reports
// authentication, rate-limit and quota failures -- is replaced by the
// translation. A more specific one, such as the network library's reason, is
// kept after the translated code so the detail is not lost.
std::string LocalizedAiErrorMessage(ai::AiErrorCode code, const std::string& message);

// A status the `ai` layer produced, with its message localized: a failure by
// its code as above, and the connection test's own progress and success text.
// Apply it where such a status is stored, not where it is shown -- statuses
// `app` builds itself are already translated.
ai::AiConnectionStatus LocalizedAiStatus(ai::AiConnectionStatus status);

} // namespace app
