#include "core/problems/problem_utils.h"
#include "core/problems/problems_manager.h"
#include "core/reviews/review_text_utils.h"
#include "core/string_utils.h"
#include "core/time_utils.h"
#include "ui/imgui_buffer_utils.h"

#include <gtest/gtest.h>

#include <regex>
#include <string>

TEST(StringUtilsTest, TrimsAsciiWhitespace) {
    EXPECT_EQ(core::TrimWhitespace("  value\t\n"), "value");
    EXPECT_EQ(core::TrimWhitespace("\r\n"), "");
    EXPECT_EQ(core::TrimWhitespace("already clean"), "already clean");
}

TEST(StringUtilsTest, LowercasesStartsWithAndNormalizesRefs) {
    EXPECT_EQ(core::ToLower("AbC-123"), "abc-123");
    EXPECT_TRUE(core::StartsWith("review-comment:123", "review-comment:"));
    EXPECT_FALSE(core::StartsWith("review", "review-comment:"));
    EXPECT_EQ(core::StripLeadingHash("#term-1"), "term-1");
    EXPECT_EQ(core::NormalizeRef("  #term-1\n"), "term-1");
}

TEST(TimeUtilsTest, FormatsUtcTimestamp) {
    const std::string value = core::NowUtcString();
    EXPECT_TRUE(std::regex_match(value, std::regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z)"))) << value;
}

TEST(ImguiBufferUtilsTest, CopiesAndNullTerminatesBuffers) {
    char buffer[5] = {};

    ui::CopyToBuffer(buffer, sizeof(buffer), "abcdef");

    EXPECT_STREQ(buffer, "abcd");
    EXPECT_EQ(buffer[4], '\0');
}

TEST(ImguiBufferUtilsTest, HandlesNullAndEmptyBuffers) {
    char buffer[1] = {'x'};

    ui::CopyToBuffer(nullptr, 10, "ignored");
    ui::CopyToBuffer(buffer, 0, "ignored");

    EXPECT_EQ(buffer[0], 'x');
}

TEST(ProblemUtilsTest, ClearsProblemsByIdPrefix) {
    core::ProblemsManager manager;
    core::ProblemItem first;
    first.id = "review-comment:1";
    core::ProblemItem second;
    second.id = "guideline-review:1";
    core::ProblemItem third;
    third.id = "terminology-term:1";
    manager.AddProblem(first);
    manager.AddProblem(second);
    manager.AddProblem(third);

    core::ClearProblemsByIdPrefix(manager, "review-comment:");

    EXPECT_FALSE(manager.GetProblemById("review-comment:1").has_value());
    EXPECT_TRUE(manager.GetProblemById("guideline-review:1").has_value());
    EXPECT_TRUE(manager.GetProblemById("terminology-term:1").has_value());
}

TEST(ReviewTextUtilsTest, TruncatesProblemMessagesWithEllipsis) {
    EXPECT_EQ(core::reviews::TruncateForProblemMessage("short", 10), "short");
    EXPECT_EQ(core::reviews::TruncateForProblemMessage("abcdef", 3), "abc...");
}