#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace ui::i18n {

// gettext key encodings used when building lookup keys.
inline constexpr char kContextSeparator = '\x04'; // EOT, separates msgctxt from msgid
inline constexpr char kPluralSeparator = '\0';    // NUL, separates singular from plural

// Reads a gettext .mo binary catalog into an in-memory message map. The map is
// keyed by the raw original string (already including any context/plural
// separators), exactly as gettext stores it.
class MoCatalog {
public:
    // Loads the catalog at the given path. Returns false (leaving the catalog
    // empty) if the file is missing or is not a valid .mo. Never throws.
    bool Load(const std::filesystem::path& path);

    bool Empty() const {
        return messages_.empty();
    }

    // Returns the translation for an already-encoded key, or nullptr if absent.
    const std::string* Find(const std::string& key) const;

private:
    std::unordered_map<std::string, std::string> messages_;
};

} // namespace ui::i18n
