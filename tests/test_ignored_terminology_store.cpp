#include "core/ignored_terminology_store.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

namespace {

using core::terminology::IgnoredSuggestion;
using core::terminology::ParseIgnoredSuggestions;
using core::terminology::SerializeIgnoredSuggestions;

TEST(IgnoredTerminologyStore, RoundTripsEntries) {
    const std::vector<IgnoredSuggestion> items = {
        {"G1", "ECU"},
        {"G2", "brake controller"},
        {"", "TLA"},
    };
    const std::string json = SerializeIgnoredSuggestions(items);

    std::vector<IgnoredSuggestion> parsed;
    std::string error;
    ASSERT_TRUE(ParseIgnoredSuggestions(json, parsed, error)) << error;
    ASSERT_EQ(parsed.size(), items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        EXPECT_EQ(parsed[i].element_id, items[i].element_id);
        EXPECT_EQ(parsed[i].term, items[i].term);
    }
}

TEST(IgnoredTerminologyStore, EmptyListRoundTrips) {
    const std::string json = SerializeIgnoredSuggestions({});
    std::vector<IgnoredSuggestion> parsed;
    std::string error;
    ASSERT_TRUE(ParseIgnoredSuggestions(json, parsed, error)) << error;
    EXPECT_TRUE(parsed.empty());
}

TEST(IgnoredTerminologyStore, MissingArrayYieldsEmptySuccess) {
    std::vector<IgnoredSuggestion> parsed;
    std::string error;
    ASSERT_TRUE(ParseIgnoredSuggestions(R"({"format":"assurance-forge-ignored-terminology"})", parsed, error)) << error;
    EXPECT_TRUE(parsed.empty());
}

TEST(IgnoredTerminologyStore, MalformedJsonFails) {
    std::vector<IgnoredSuggestion> parsed;
    std::string error;
    EXPECT_FALSE(ParseIgnoredSuggestions("{ not json", parsed, error));
    EXPECT_FALSE(error.empty());
}

TEST(IgnoredTerminologyStore, SkipsEntriesWithEmptyTerm) {
    std::vector<IgnoredSuggestion> parsed;
    std::string error;
    const std::string json = R"({"ignored":[{"element_id":"G1","term":""},{"element_id":"G2","term":"OK"}]})";
    ASSERT_TRUE(ParseIgnoredSuggestions(json, parsed, error)) << error;
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].term, "OK");
    EXPECT_EQ(parsed[0].element_id, "G2");
}

} // namespace
