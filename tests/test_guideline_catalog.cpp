#include <gtest/gtest.h>

#include "app/guideline_catalog.h"

#include <string>
#include <utility>

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

    app::GuidelineCatalog catalog = app::BuildGuidelineCatalog(std::move(document), "guidelines.yaml");

    ASSERT_EQ(catalog.entries.size(), 2u);
    EXPECT_EQ(catalog.entries[0].id, "CL.1");
    EXPECT_EQ(catalog.entries[0].category, "CL");
    EXPECT_EQ(catalog.entries[1].id, "AR.1");
    EXPECT_EQ(catalog.ids.size(), 2u);
    EXPECT_TRUE(catalog.ids.count("CL.1") > 0);
    EXPECT_TRUE(catalog.ids.count("AR.1") > 0);
    EXPECT_EQ(catalog.source_path.filename().string(), "guidelines.yaml");
}

TEST(GuidelineCatalogTest, LoadsRepositoryGuidelines) {
    app::GuidelineCatalog catalog;
    std::string error;

    ASSERT_TRUE(app::LoadGuidelineCatalog(catalog, error)) << error;
    EXPECT_FALSE(catalog.entries.empty());
    EXPECT_TRUE(catalog.ids.count("CL.1") > 0);
}