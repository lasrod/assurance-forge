#pragma once

#include <nlohmann/json.hpp>

#include <exception>

namespace core::audit {

// The value at `key`, or `fallback` when the key is absent, null, or holds a
// value that does not convert to T.
template <typename T>
T ReadOr(const nlohmann::json& j, const char* key, T fallback) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null())
        return fallback;
    try {
        return it->get<T>();
    } catch (const std::exception&) {
        return fallback;
    }
}

} // namespace core::audit
