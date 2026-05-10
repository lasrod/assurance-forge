#pragma once

#include <string>

namespace core {

std::string TrimWhitespace(const std::string& value);
std::string ToLower(std::string value);
bool StartsWith(const std::string& value, const std::string& prefix);
std::string StripLeadingHash(std::string value);
std::string NormalizeRef(std::string value);

} // namespace core