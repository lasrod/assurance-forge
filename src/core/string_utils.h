#pragma once

#include <filesystem>
#include <string>

namespace core {

std::string TrimWhitespace(const std::string& value);
std::string ToLower(std::string value);
bool StartsWith(const std::string& value, const std::string& prefix);
std::string StripLeadingHash(std::string value);
std::string NormalizeRef(std::string value);

// path::u8string() returns std::u8string in C++20+; this reinterprets the same
// bytes as std::string so callers that need a UTF-8 std::string can keep using one.
std::string PathToUtf8(const std::filesystem::path& path);

// The inverse, and the replacement for std::filesystem::u8path, which C++20
// deprecated.
//
// Not interchangeable with `std::filesystem::path(value)`. That constructor
// reads a narrow string in the *native* encoding -- the active code page on
// Windows -- so passing UTF-8 to it mangles every non-ASCII path, silently and
// only for the users who have them. Going through char8_t keeps the bytes
// meaning UTF-8.
std::filesystem::path PathFromUtf8(const std::string& value);

} // namespace core