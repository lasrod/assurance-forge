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

} // namespace core