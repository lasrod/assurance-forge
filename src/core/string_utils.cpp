#include "core/string_utils.h"

#include <algorithm>
#include <cctype>

namespace core {

std::string TrimWhitespace(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin)))
        ++begin;
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))))
        --end;
    return std::string(begin, end);
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin());
}

std::string StripLeadingHash(std::string value) {
    if (!value.empty() && value.front() == '#')
        value.erase(value.begin());
    return value;
}

std::string NormalizeRef(std::string value) {
    return StripLeadingHash(TrimWhitespace(value));
}

std::string PathToUtf8(const std::filesystem::path& path) {
    auto u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::filesystem::path PathFromUtf8(const std::string& value) {
    const std::u8string u8(reinterpret_cast<const char8_t*>(value.data()), value.size());
    return std::filesystem::path(u8);
}

} // namespace core