#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>

namespace ui {

inline void CopyToBuffer(char* buffer, size_t buffer_size, const std::string& value) {
    if (!buffer || buffer_size == 0)
        return;
    const size_t count = std::min(buffer_size - 1, value.size());
    std::memcpy(buffer, value.data(), count);
    buffer[count] = '\0';
}

} // namespace ui