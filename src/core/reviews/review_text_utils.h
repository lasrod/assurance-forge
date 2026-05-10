#pragma once

#include <cstddef>
#include <string>

namespace core::reviews {

std::string TruncateForProblemMessage(const std::string& value, size_t limit = 400);

} // namespace core::reviews