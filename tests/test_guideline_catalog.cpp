#include "core/guideline_catalog.h"

#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <utility>

namespace {

// Sets an environment variable for the life of the object, and puts back what
// was there before.
class ScopedEnvironment {
public:
    ScopedEnvironment(std::string name, const std::string& value) : name_(std::move(name)) {
        if (const char* previous = std::getenv(name_.c_str()))
            previous_ = previous;
        Set(name_, value);
    }
    ~ScopedEnvironment() {
        if (previous_.has_value())
            Set(name_, *previous_);
        else
            Unset(name_);
    }
    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

private:
    static void Set(const std::string& name, const std::string& value) {
#ifdef _WIN32
        _putenv_s(name.c_str(), value.c_str());
#else
        setenv(name.c_str(), value.c_str(), 1);
#endif
    }

    static void Unset(const std::string& name) {
#ifdef _WIN32
        _putenv_s(name.c_str(), "");
#else
        unsetenv(name.c_str());
#endif
    }

    std::string name_;
    std::optional<std::string> previous_;
};

} // namespace

TEST(GuidelineCatalogTest, BuildsFlatEntriesAndLookupIds) {
    parser::GuidelinesDocument document;
    parser::Guideline first;
    first.id = "CL.1";
    first.category = "CL";
    first.title = "Write each claim as a falsifiable proposition";
    document.guidelines.push_back(first);

    parser::Guideline second;
    second.id = "AR.1";
    second.category = "AR";
    second.title = "Link argument steps clearly";
    document.guidelines.push_back(second);

    parser::Guideline ignored;
    ignored.category = "CL";
    ignored.title = "Missing ID";
    document.guidelines.push_back(ignored);

    parser::ReviewProfile profile;
    profile.id = "claim_wording_review";
    profile.display_name = "Claim wording review";
    profile.description = "Reviews claim wording.";
    document.review_profiles.push_back(profile);

    core::GuidelineCatalog catalog = core::BuildGuidelineCatalog(std::move(document), "sccg.full.json");

    ASSERT_EQ(catalog.entries.size(), 2u);
    EXPECT_EQ(catalog.entries[0].id, "CL.1");
    EXPECT_EQ(catalog.entries[0].category, "CL");
    EXPECT_EQ(catalog.entries[1].id, "AR.1");
    EXPECT_EQ(catalog.ids.size(), 2u);
    EXPECT_TRUE(catalog.ids.count("CL.1") > 0);
    EXPECT_TRUE(catalog.ids.count("AR.1") > 0);
    ASSERT_EQ(catalog.review_profile_entries.size(), 1u);
    EXPECT_EQ(catalog.review_profile_entries[0].id, "claim_wording_review");
    EXPECT_TRUE(catalog.review_profile_ids.count("claim_wording_review") > 0);
    EXPECT_EQ(catalog.source_path.filename().string(), "sccg.full.json");
}

TEST(GuidelineCatalogTest, LoadsRepositoryGuidelines) {
    core::GuidelineCatalog catalog;
    std::string error;

    ASSERT_TRUE(core::LoadGuidelineCatalog(catalog, error)) << error;
    EXPECT_FALSE(catalog.entries.empty());
    EXPECT_TRUE(catalog.ids.count("CL.1") > 0);
    EXPECT_EQ(catalog.source_path.filename().string(), "dist");
    EXPECT_TRUE(catalog.review_profile_ids.count("claim_review") > 0);
}

// An explicit distribution is authoritative. One naming no directory used to
// fall back to discovery, so an evaluation meant for another catalogue ran
// against the shipped one and produced a valid-looking record.
TEST(GuidelineCatalogTest, AnOverrideNamingNoDirectoryFailsInsteadOfLoadingTheShippedCatalogue) {
    const std::filesystem::path missing = std::filesystem::temp_directory_path() / "af_sccg_override_not_there";
    std::filesystem::remove_all(missing);
    const ScopedEnvironment scoped("AF_SCCG_DIST_DIR", missing.string());

    core::GuidelineCatalog catalog;
    std::string error;
    EXPECT_FALSE(core::LoadGuidelineCatalog(catalog, error));
    EXPECT_NE(error.find("AF_SCCG_DIST_DIR"), std::string::npos) << error;
    EXPECT_TRUE(catalog.entries.empty());
}
