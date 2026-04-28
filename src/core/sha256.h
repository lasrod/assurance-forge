#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace core {

class Sha256 {
public:
    static std::string HexDigest(const unsigned char* data, size_t size);
    static std::string HexDigest(std::string_view data);
    static std::string HexDigest(const std::vector<unsigned char>& data);
};

}  // namespace core