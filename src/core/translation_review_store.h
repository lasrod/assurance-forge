#pragma once

#include <string>
#include <vector>

namespace core::translation {

// Serializes the set of element IDs that need a secondary-language translation
// review to the project sidecar JSON document (pretty-printed, trailing newline)
// so it round-trips with ParseTranslationReview.
std::string SerializeTranslationReview(const std::vector<std::string>& element_ids);

// Parses the sidecar JSON document. Returns false (with `error` set) on malformed
// JSON or an unrecognised top-level shape. A missing "pending" array yields an
// empty result and success; individual malformed/empty entries are skipped.
bool ParseTranslationReview(const std::string& json,
                            std::vector<std::string>& out_element_ids,
                            std::string& error);

} // namespace core::translation
