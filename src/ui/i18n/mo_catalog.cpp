#include "ui/i18n/mo_catalog.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace ui::i18n {
namespace {

constexpr std::uint32_t kMoMagic = 0x950412deu;
constexpr std::uint32_t kMoMagicSwapped = 0xde120495u;

std::uint32_t ReadU32(const unsigned char* data, bool swap) {
    std::uint32_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    if (swap) {
        value = ((value & 0x000000ffu) << 24) | ((value & 0x0000ff00u) << 8) | ((value & 0x00ff0000u) >> 8) |
                ((value & 0xff000000u) >> 24);
    }
    return value;
}

} // namespace

bool MoCatalog::Load(const std::filesystem::path& path) {
    messages_.clear();

    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return false;

    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    // 28-byte header: magic, revision, count, originals table, translations table.
    if (bytes.size() < 28)
        return false;

    const std::uint32_t magic = ReadU32(bytes.data(), false);
    bool swap = false;
    if (magic == kMoMagic)
        swap = false;
    else if (magic == kMoMagicSwapped)
        swap = true;
    else
        return false;

    const std::uint32_t count = ReadU32(bytes.data() + 8, swap);
    const std::uint32_t originals_table = ReadU32(bytes.data() + 12, swap);
    const std::uint32_t translations_table = ReadU32(bytes.data() + 16, swap);

    const auto read_string = [&](std::uint32_t table, std::uint32_t index, std::string& out) -> bool {
        const std::size_t entry = static_cast<std::size_t>(table) + static_cast<std::size_t>(index) * 8u;
        if (entry + 8u > bytes.size())
            return false;
        const std::uint32_t length = ReadU32(bytes.data() + entry, swap);
        const std::uint32_t offset = ReadU32(bytes.data() + entry + 4, swap);
        if (static_cast<std::size_t>(offset) + length > bytes.size())
            return false;
        out.assign(reinterpret_cast<const char*>(bytes.data() + offset), length);
        return true;
    };

    for (std::uint32_t i = 0; i < count; ++i) {
        std::string key;
        std::string value;
        if (!read_string(originals_table, i, key) || !read_string(translations_table, i, value)) {
            messages_.clear();
            return false;
        }
        messages_.emplace(std::move(key), std::move(value));
    }
    return true;
}

const std::string* MoCatalog::Find(const std::string& key) const {
    const auto it = messages_.find(key);
    return it == messages_.end() ? nullptr : &it->second;
}

} // namespace ui::i18n
