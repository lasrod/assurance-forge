#pragma once

#include <string>
#include <string_view>

namespace ui::i18n {

enum class Language {
    English,
    Japanese,
};

inline Language ParseLanguageCode(std::string_view code) {
    if (code == "ja" || code == "ja-JP" || code == "jp")
        return Language::Japanese;
    return Language::English;
}

inline std::string LanguageCode(Language language) {
    return language == Language::Japanese ? "ja" : "en";
}

// Each language is shown in its own script (autonym), independent of the
// current UI language, which is the standard convention for language pickers.
inline std::string LanguageDisplayName(Language language) {
    return language == Language::Japanese ? "日本語" : "English";
}

} // namespace ui::i18n
