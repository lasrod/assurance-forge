#include "ui/i18n/language.h"
#include "ui/i18n/localization.h"
#include "ui/i18n/mo_catalog.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace i18n = ui::i18n;

// Writes a minimal little-endian gettext .mo file from raw key/value entries.
// Keys may already contain context/plural separators; lengths exclude the
// terminating NUL (gettext convention) but the NUL is present in the blob.
void WriteMoFile(const std::filesystem::path& path, const std::vector<std::pair<std::string, std::string>>& entries) {
    std::filesystem::create_directories(path.parent_path());

    const auto append_u32 = [](std::string& out, std::uint32_t value) {
        out.push_back(static_cast<char>(value & 0xffu));
        out.push_back(static_cast<char>((value >> 8) & 0xffu));
        out.push_back(static_cast<char>((value >> 16) & 0xffu));
        out.push_back(static_cast<char>((value >> 24) & 0xffu));
    };

    const std::uint32_t count = static_cast<std::uint32_t>(entries.size());
    std::string originals_blob;
    std::string translations_blob;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> originals_index; // (length, blob offset)
    std::vector<std::pair<std::uint32_t, std::uint32_t>> translations_index;
    for (const auto& [key, value] : entries) {
        originals_index.emplace_back(static_cast<std::uint32_t>(key.size()),
                                     static_cast<std::uint32_t>(originals_blob.size()));
        originals_blob.append(key);
        originals_blob.push_back('\0');
        translations_index.emplace_back(static_cast<std::uint32_t>(value.size()),
                                        static_cast<std::uint32_t>(translations_blob.size()));
        translations_blob.append(value);
        translations_blob.push_back('\0');
    }

    const std::uint32_t originals_table = 28;
    const std::uint32_t translations_table = originals_table + count * 8;
    const std::uint32_t originals_start = translations_table + count * 8;
    const std::uint32_t translations_start = originals_start + static_cast<std::uint32_t>(originals_blob.size());

    std::string out;
    append_u32(out, 0x950412deu); // magic
    append_u32(out, 0);           // revision
    append_u32(out, count);
    append_u32(out, originals_table);
    append_u32(out, translations_table);
    append_u32(out, 0); // hash table size
    append_u32(out, 0); // hash table offset
    for (const auto& [length, offset] : originals_index) {
        append_u32(out, length);
        append_u32(out, originals_start + offset);
    }
    for (const auto& [length, offset] : translations_index) {
        append_u32(out, length);
        append_u32(out, translations_start + offset);
    }
    out.append(originals_blob);
    out.append(translations_blob);

    std::ofstream stream(path, std::ios::binary);
    ASSERT_TRUE(stream) << "Failed to open " << path.string() << " for writing";
    stream.write(out.data(), static_cast<std::streamsize>(out.size()));
    ASSERT_TRUE(stream.good()) << "Failed to write " << path.string();
}

std::filesystem::path MakeTempLocaleRoot() {
    // Two samples so parallel ctest runs can't collide on a shared temp path.
    std::random_device rd;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("af_i18n_" + std::to_string(rd()) + "_" + std::to_string(rd()));
    return root;
}

// Builds a synthetic ja catalog and initializes the i18n system to use it.
std::filesystem::path InitSyntheticJapanese() {
    const std::filesystem::path root = MakeTempLocaleRoot();
    const std::filesystem::path mo = root / "ja" / "LC_MESSAGES" / "assurance_forge.mo";
    WriteMoFile(mo,
                {
                    {"Hello", "こんにちは"},
                    {std::string("menu") + i18n::kContextSeparator + "Open", "メニューを開く"},
                    {std::string("{0} item") + i18n::kPluralSeparator + "{0} items", "{0} 個"},
                });
    i18n::LocalizationConfig config;
    config.localeDirectory = root;
    config.language = i18n::Language::Japanese;
    EXPECT_TRUE(i18n::Initialize(config));
    return root;
}

void InitRealCatalog(i18n::Language language) {
    i18n::LocalizationConfig config;
    config.localeDirectory = "assets/locale"; // tests run from CMAKE_SOURCE_DIR
    config.language = language;
    i18n::Initialize(config);
}

} // namespace

TEST(LocalizationTest, DefaultsToEnglishForUnknownLanguageCode) {
    EXPECT_EQ(i18n::ParseLanguageCode(""), i18n::Language::English);
    EXPECT_EQ(i18n::ParseLanguageCode("fr"), i18n::Language::English);
}

TEST(LocalizationTest, ParsesJapaneseLanguageCodes) {
    EXPECT_EQ(i18n::ParseLanguageCode("ja"), i18n::Language::Japanese);
    EXPECT_EQ(i18n::ParseLanguageCode("ja-JP"), i18n::Language::Japanese);
}

TEST(LocalizationTest, LanguageCodeRoundTrips) {
    EXPECT_EQ(i18n::LanguageCode(i18n::Language::English), "en");
    EXPECT_EQ(i18n::LanguageCode(i18n::Language::Japanese), "ja");
}

TEST(LocalizationTest, LanguageDisplayNameUsesAutonyms) {
    EXPECT_EQ(i18n::LanguageDisplayName(i18n::Language::English), "English");
    EXPECT_EQ(i18n::LanguageDisplayName(i18n::Language::Japanese), "日本語");
}

TEST(LocalizationTest, EnglishReturnsSourceStrings) {
    InitRealCatalog(i18n::Language::English);
    EXPECT_EQ(i18n::tr("File"), "File");
    EXPECT_EQ(i18n::tr("Open Project"), "Open Project");
    EXPECT_EQ(i18n::tr("Forge Confidence in Safety"), "Forge Confidence in Safety");
}

TEST(LocalizationTest, JapaneseCatalogLoadsAndTranslates) {
    InitRealCatalog(i18n::Language::Japanese);
    EXPECT_EQ(i18n::tr("File"), "ファイル");
    EXPECT_EQ(i18n::tr("Forge Confidence in Safety"), "安全への確信を鍛える");
    EXPECT_EQ(i18n::tr("Create Empty Assurance Project"), "空の保証プロジェクトを作成");
}

TEST(LocalizationTest, FallsBackToSourceForMissingTranslation) {
    InitRealCatalog(i18n::Language::Japanese);
    // A string that is not in the catalog at all echoes its source.
    EXPECT_EQ(i18n::tr("This string is not in any catalog"), "This string is not in any catalog");
}

TEST(LocalizationTest, MissingCatalogFileDoesNotCrashAndFallsBack) {
    i18n::LocalizationConfig config;
    config.localeDirectory = "this/directory/does/not/exist";
    config.language = i18n::Language::Japanese;
    EXPECT_FALSE(i18n::Initialize(config));
    EXPECT_EQ(i18n::tr("File"), "File");
}

TEST(LocalizationTest, ContextTranslationDistinguishesMeanings) {
    const std::filesystem::path root = InitSyntheticJapanese();
    EXPECT_EQ(i18n::trc("menu", "Open"), "メニューを開く");
    // No entry for this context: falls back to the source msgid.
    EXPECT_EQ(i18n::trc("toolbar", "Open"), "Open");
    std::filesystem::remove_all(root);
}

TEST(LocalizationTest, PluralTranslationSelectsForm) {
    const std::filesystem::path root = InitSyntheticJapanese();
    // Japanese has a single plural form, so count does not change the result.
    EXPECT_EQ(i18n::trn("{0} item", "{0} items", 1), "{0} 個");
    EXPECT_EQ(i18n::trn("{0} item", "{0} items", 5), "{0} 個");
    std::filesystem::remove_all(root);
}

TEST(LocalizationTest, PluralFallbackUsesEnglishCountRule) {
    InitRealCatalog(i18n::Language::English);
    EXPECT_EQ(i18n::trn("{0} item", "{0} items", 1), "{0} item");
    EXPECT_EQ(i18n::trn("{0} item", "{0} items", 3), "{0} items");
}

TEST(LocalizationTest, FormattedTranslationUsesPositionalPlaceholders) {
    InitRealCatalog(i18n::Language::English);
    EXPECT_EQ(i18n::trf("Loaded {0} elements.", 42), "Loaded 42 elements.");
    EXPECT_EQ(i18n::trnf("{0} item", "{0} items", 3, 3), "3 items");
}

TEST(LocalizationTest, SwitchingLanguageUpdatesSubsequentTranslations) {
    InitRealCatalog(i18n::Language::English);
    EXPECT_EQ(i18n::tr("File"), "File");
    EXPECT_TRUE(i18n::SetLanguage(i18n::Language::Japanese));
    EXPECT_EQ(i18n::tr("File"), "ファイル");
    EXPECT_TRUE(i18n::SetLanguage(i18n::Language::English));
    EXPECT_EQ(i18n::tr("File"), "File");
}

TEST(MoCatalogTest, ReadsSyntheticCatalog) {
    const std::filesystem::path root = MakeTempLocaleRoot();
    const std::filesystem::path mo = root / "assurance_forge.mo";
    WriteMoFile(mo, {{"Key", "Value"}});

    i18n::MoCatalog catalog;
    EXPECT_TRUE(catalog.Load(mo));
    ASSERT_NE(catalog.Find("Key"), nullptr);
    EXPECT_EQ(*catalog.Find("Key"), "Value");
    EXPECT_EQ(catalog.Find("Missing"), nullptr);

    std::filesystem::remove_all(root);
}

TEST(MoCatalogTest, RejectsMissingFile) {
    i18n::MoCatalog catalog;
    EXPECT_FALSE(catalog.Load("nope/missing.mo"));
    EXPECT_TRUE(catalog.Empty());
}
