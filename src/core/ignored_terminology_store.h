#pragma once

#include <string>
#include <vector>

namespace core::terminology {

// A single persisted "ignore this terminology suggestion" decision. Scoped to a
// specific element occurrence so it matches the in-memory keying (element_id +
// term value) used by the terminology problem sync.
struct IgnoredSuggestion {
    std::string element_id;
    std::string term;
};

// Serializes the ignore list to the project sidecar JSON document (pretty-printed,
// trailing newline) so it round-trips with ParseIgnoredSuggestions.
std::string SerializeIgnoredSuggestions(const std::vector<IgnoredSuggestion>& suggestions);

// Parses the sidecar JSON document. Returns false (with `error` set) on malformed
// JSON or an unrecognised top-level shape. A missing "ignored" array yields an
// empty result and success; individual malformed/empty entries are skipped.
bool ParseIgnoredSuggestions(const std::string& json,
                             std::vector<IgnoredSuggestion>& out_suggestions,
                             std::string& error);

} // namespace core::terminology
