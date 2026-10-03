#include "core/status_text.h"

#include <cstddef>

namespace core {

namespace {

// Parses the digits between a placeholder's braces. False when there are none
// or anything else is present ("{}", "{name}", "{0:>4}").
bool ParsePlaceholderIndex(std::string_view digits, std::size_t& index) {
    if (digits.empty() || digits.size() > 3)
        return false;
    index = 0;
    for (const char digit : digits) {
        if (digit < '0' || digit > '9')
            return false;
        index = index * 10 + static_cast<std::size_t>(digit - '0');
    }
    return true;
}

} // namespace

std::string FormatStatusText(std::string_view pattern, const std::vector<std::string>& arguments) {
    std::string result;
    result.reserve(pattern.size());
    std::size_t position = 0;
    while (position < pattern.size()) {
        const char character = pattern[position];
        const bool is_brace = character == '{' || character == '}';
        if (is_brace && position + 1 < pattern.size() && pattern[position + 1] == character) {
            result.push_back(character);
            position += 2;
            continue;
        }
        if (character == '{') {
            const std::size_t close = pattern.find('}', position);
            std::size_t index = 0;
            if (close != std::string_view::npos &&
                ParsePlaceholderIndex(pattern.substr(position + 1, close - position - 1), index) &&
                index < arguments.size()) {
                result.append(arguments[index]);
                position = close + 1;
                continue;
            }
        }
        result.push_back(character);
        ++position;
    }
    return result;
}

} // namespace core
