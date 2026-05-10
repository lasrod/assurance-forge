#include "core/problems/problem_utils.h"
#include "core/reviews/review_item.h"
#include "core/reviews/review_proposal.h"
#include "core/reviews/review_proposal_factory.h"
#include "core/problems/problems_manager.h"
#include "core/reviews/review_text_utils.h"
#include "core/string_utils.h"
#include "core/time_utils.h"
#include "parser/model_utils.h"
#include "parser/xml_parser.h"
#include "ui/imgui_buffer_utils.h"

#include <gtest/gtest.h>

#include <regex>
#include <string>
#include <vector>

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

TEST(ParserModelUtilsTest, FindsElementsByExplicitLookupSemantics) {
    parser::AssuranceCase model;
    parser::SacmElement first;
    first.id = "id-1";
    first.gid = "gid-1";
    parser::SacmElement second;
    second.id = "id-2";
    second.gid = "gid-2";
    model.elements = {first, second};

    EXPECT_EQ(parser::FindElementById(model, "id-1")->gid, "gid-1");
    EXPECT_EQ(parser::FindElementById(model, "gid-1"), nullptr);
    EXPECT_EQ(parser::FindElementByIdOrGidValue(model, "gid-2")->id, "id-2");
    EXPECT_EQ(parser::FindElementByIdOrGid(model, "", "gid-1")->id, "id-1");
}

TEST(ReviewProposalFactoryTest, BuildsDraftProposalWithAnchorHashes) {
    parser::AssuranceCase model;
    model.id = "case-1";
    parser::SacmElement anchor;
    anchor.id = "G1";
    anchor.gid = "gid-G1";
    anchor.type = "claim";
    anchor.content = "System is acceptably safe.";
    model.elements.push_back(anchor);

    core::reviews::ReviewItem item;
    item.id = "review-1";
    item.title = "Improve claim";
    item.message = "This claim needs a clearer scope.";

    core::reviews::ReviewProposal proposal = core::reviews::BuildDraftReviewProposal(item, model, anchor);

    EXPECT_EQ(proposal.review_item_id, "review-1");
    EXPECT_EQ(proposal.title, "Improve claim");
    EXPECT_EQ(proposal.anchor_element_id, "G1");
    EXPECT_EQ(proposal.affected_existing_element_ids, std::vector<std::string>{"G1"});
    EXPECT_FALSE(proposal.id.empty());
    EXPECT_FALSE(proposal.created_utc.empty());
    EXPECT_FALSE(proposal.base_model_hash.empty());
    EXPECT_FALSE(proposal.base_element_hashes["G1"].empty());
}