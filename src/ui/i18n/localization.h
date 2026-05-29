#pragma once

#include "ui/i18n/language.h"

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace ui::i18n {

struct LocalizationConfig {
    std::filesystem::path localeDirectory;
    std::string domain = "assurance_forge";
    Language language = Language::English;
};

// Loads the catalog for config.language. Never throws; a missing or invalid
// catalog logs a warning and leaves the system in English. Returns true when
// the requested language is usable.
bool Initialize(const LocalizationConfig& config);

// Switches the active language, lazily loading its catalog. Returns false (and
// stays effectively English) when the catalog cannot be loaded.
bool SetLanguage(Language language);

Language CurrentLanguage();

// Monotonic counter that increments on every successful SetLanguage call.
// Consumers can compare it across frames to detect language switches without
// subscribing to callbacks — useful for re-translating cached strings (e.g.
// problem messages that were trf'd at sync time).
unsigned LanguageEpoch();

// Returns the translation of msgid, or msgid itself when untranslated.
std::string tr(std::string_view msgid);

// Context-qualified translation, for identical English text with different
// meanings (e.g. "Open" as a menu verb vs. a state).
std::string trc(std::string_view context, std::string_view msgid);

// Plural-aware translation. Falls back to the English singular/plural source.
std::string trn(std::string_view singular, std::string_view plural, int count);

namespace detail {

// Formats a translated pattern with std::vformat. Placeholders must be
// positional ({0}, {1}, ...) so translators can reorder them. A malformed
// translation pattern falls back to the unformatted pattern rather than
// throwing, so a bad catalog can never crash the UI.
template <class... Args>
std::string FormatTranslated(std::string pattern, const Args&... args) {
    try {
        return std::vformat(pattern, std::make_format_args(args...));
    } catch (const std::format_error&) {
        return pattern;
    }
}

} // namespace detail

template <class... Args>
std::string trf(std::string_view msgid, const Args&... args) {
    return detail::FormatTranslated(tr(msgid), args...);
}

template <class... Args>
std::string trcf(std::string_view context, std::string_view msgid, const Args&... args) {
    return detail::FormatTranslated(trc(context, msgid), args...);
}

template <class... Args>
std::string trnf(std::string_view singular, std::string_view plural, int count, const Args&... args) {
    return detail::FormatTranslated(trn(singular, plural, count), args...);
}

} // namespace ui::i18n

#define AF_TR(text) ::ui::i18n::tr(text)
#define AF_TR_CTX(context, text) ::ui::i18n::trc(context, text)
