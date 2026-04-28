#include "core/sha256.h"

#include "picosha2.h"

namespace core {

std::string Sha256::HexDigest(const unsigned char* data, size_t size) {
    static constexpr unsigned char empty = 0;
    const unsigned char* begin = size == 0 ? &empty : data;
    return picosha2::hash256_hex_string(begin, begin + size);
}

std::string Sha256::HexDigest(std::string_view data) {
    return HexDigest(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

std::string Sha256::HexDigest(const std::vector<unsigned char>& data) {
    return HexDigest(data.data(), data.size());
}

}  // namespace core