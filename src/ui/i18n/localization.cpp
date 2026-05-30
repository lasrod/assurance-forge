#include "ui/i18n/localization.h"

#include "ui/i18n/mo_catalog.h"

#include <cstdio>
#include <set>

namespace ui::i18n {
namespace {

Language g_language = Language::English;
MoCatalog g_catalog;
bool g_catalog_loaded = false;
std::filesystem::path g_locale_directory;
std::string g_domain = "assurance_forge";
unsigned g_language_epoch = 0;

std::filesystem::path CatalogPath(Language language) {
    return g_locale_directory / LanguageCode(language) / "LC_MESSAGES" / (g_domain + ".mo");
}

// English needs no catalog: the source strings are the keys. For other
// languages, load the .mo and warn (once) on failure, falling back to English.
bool LoadCatalogFor(Language language) {
    g_catalog = MoCatalog{};
    g_catalog_loaded = false;
    if (language == Language::English)
        return true;

    const std::filesystem::path path = CatalogPath(language);
    if (!g_catalog.Load(path)) {
        // Warn at most once per unique catalog path per session so repeated
        // SetLanguage failures don't flood stderr.
        static std::set<std::filesystem::path> warned_paths;
        if (warned_paths.insert(path).second) {
            std::fprintf(stderr,
                         "[i18n] Could not load translation catalog for language '%s' (%s). Falling back to English.\n",
                         LanguageCode(language).c_str(),
                         path.string().c_str());
        }
        return false;
    }
    g_catalog_loaded = true;
    return true;
}

const std::string* Lookup(const std::string& key) {
    if (!g_catalog_loaded)
        return nullptr;
    return g_catalog.Find(key);
}

int PluralIndex(Language language, int count) {
    if (language == Language::Japanese)
        return 0; // Japanese has a single plural form.
    return count == 1 ? 0 : 1;
}

// Translations for plural messages are NUL-separated forms. Returns the
// index-th form, falling back to the last available form.
std::string_view NthPluralForm(std::string_view forms, int index) {
    std::size_t start = 0;
    int current = 0;
    std::string_view last = forms;
    while (true) {
        const std::size_t nul = forms.find('\0', start);
        const std::string_view piece =
            forms.substr(start, nul == std::string_view::npos ? std::string_view::npos : nul - start);
        last = piece;
        if (current == index)
            return piece;
        if (nul == std::string_view::npos)
            return last;
        start = nul + 1;
        ++current;
    }
}

} // namespace

bool Initialize(const LocalizationConfig& config) {
    g_locale_directory = config.localeDirectory;
    g_domain = config.domain.empty() ? "assurance_forge" : config.domain;
    // Force a load (don't early-out on the English default): the locale
    // directory and domain were just set, and consumers expect the catalog to
    // be ready after Initialize. Bump the epoch if the effective language
    // changed so any consumer that captured it earlier refreshes.
    const Language previous = g_language;
    const bool ok = LoadCatalogFor(config.language);
    g_language = ok ? config.language : Language::English;
    if (g_language != previous)
        ++g_language_epoch;
    return ok;
}

bool SetLanguage(Language language) {
    // No-op when the requested language is already effective: avoids a
    // redundant catalog reload (disk I/O + allocation) and never bumps the
    // epoch, so consumers don't run spurious refresh work.
    if (language == g_language)
        return true;
    const Language previous = g_language;
    const bool ok = LoadCatalogFor(language);
    g_language = ok ? language : Language::English;
    if (g_language != previous)
        ++g_language_epoch;
    return ok;
}

Language CurrentLanguage() {
    return g_language;
}

unsigned LanguageEpoch() {
    return g_language_epoch;
}

std::string tr(std::string_view msgid) {
    // English mode keeps no catalog (the source strings are the keys), so skip
    // building a lookup key — this is the hot path for every AF_TR per frame.
    if (!g_catalog_loaded)
        return std::string(msgid);
    if (const std::string* hit = Lookup(std::string(msgid)); hit && !hit->empty())
        return *hit;
    return std::string(msgid);
}

std::string trc(std::string_view context, std::string_view msgid) {
    if (!g_catalog_loaded)
        return std::string(msgid);
    std::string key;
    key.reserve(context.size() + 1 + msgid.size());
    key.append(context);
    key.push_back(kContextSeparator);
    key.append(msgid);
    if (const std::string* hit = Lookup(key); hit && !hit->empty())
        return *hit;
    return std::string(msgid);
}

std::string trn(std::string_view singular, std::string_view plural, int count) {
    if (!g_catalog_loaded)
        return std::string(count == 1 ? singular : plural);
    std::string key;
    key.reserve(singular.size() + 1 + plural.size());
    key.append(singular);
    key.push_back(kPluralSeparator);
    key.append(plural);
    if (const std::string* hit = Lookup(key); hit && !hit->empty()) {
        const std::string_view form = NthPluralForm(*hit, PluralIndex(g_language, count));
        if (!form.empty())
            return std::string(form);
    }
    return std::string(count == 1 ? singular : plural);
}

} // namespace ui::i18n
