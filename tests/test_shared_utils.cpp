#include "core/problems/problem_utils.h"
#include "core/reviews/review_item.h"
#include "core/reviews/review_proposal.h"
#include "core/reviews/review_proposal_factory.h"
#include "core/problems/problems_manager.h"
#include "core/reviews/review_text_utils.h"
#include "core/string_utils.h"
#include "core/time_utils.h"
#include "core/terminology_text_utils.h"
#include "parser/model_utils.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_model.h"
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

TEST(StringUtilsTest, PathFromUtf8RoundTripsNonAsciiThroughPathToUtf8) {
    // The replacement for std::filesystem::u8path, which C++20 deprecated.
    //
    // `std::filesystem::path(value)` would compile and pass an ASCII test while
    // being wrong: that constructor reads a narrow string in the native
    // encoding, so on Windows it decodes UTF-8 bytes as the active code page and
    // mangles exactly the paths a Latin-1 developer never types. These cases are
    // non-ASCII on purpose -- an ASCII-only test cannot tell the two apart.
    const std::string japanese = "プロジェクト/ブレーキ.sacm";
    EXPECT_EQ(core::PathToUtf8(core::PathFromUtf8(japanese)), japanese);

    const std::string accented = "Sécurité/Freinage.sacm";
    EXPECT_EQ(core::PathToUtf8(core::PathFromUtf8(accented)), accented);

    const std::string ascii = "projects/brake.sacm";
    EXPECT_EQ(core::PathToUtf8(core::PathFromUtf8(ascii)), ascii);

    EXPECT_TRUE(core::PathFromUtf8("").empty());
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

TEST(TerminologyTextUtilsTest, JoinsAndSplitsCategoryRefs) {
    EXPECT_EQ(core::JoinCategoryRefs({"cat-a", "", "cat-b"}), "cat-a, cat-b");
    EXPECT_EQ(core::SplitCategoryRefs(" cat-a, cat-b cat-a "), (std::vector<std::string>{"cat-a", "cat-b"}));
    EXPECT_EQ(core::SplitNormalizedCategoryRefs(" #cat-a, cat-b #cat-a "),
              (std::vector<std::string>{"cat-a", "cat-b"}));
}

TEST(TerminologyTextUtilsTest, BuildsTermContextDisplayLabel) {
    sacm::Term term;
    term.id = "term-id";
    EXPECT_EQ(core::TermContextDisplayLabel(term), "term-id");
    term.name = "Display Name";
    EXPECT_EQ(core::TermContextDisplayLabel(term), "Display Name");
    term.value = "ABS";
    EXPECT_EQ(core::TermContextDisplayLabel(term), "ABS: Display Name");
    term.name = "ABS";
    EXPECT_EQ(core::TermContextDisplayLabel(term), "ABS");
}

TEST(ParserModelUtilsTest, IdentifiesRelationshipElementsAndTerminologyText) {
    parser::SacmElement claim;
    claim.type = "claim";
    claim.content = "Claim content";
    claim.description = "Claim description";
    EXPECT_FALSE(parser::IsRelationshipElement(claim));
    EXPECT_EQ(parser::ElementTerminologyText(claim), "Claim content");

    parser::SacmElement context;
    context.type = "assertedcontext";
    context.description = "Relationship description";
    EXPECT_TRUE(parser::IsRelationshipElement(context));
    EXPECT_EQ(parser::ElementTerminologyText(context), "Relationship description");
}
// The one relationship rule every model traversal shares. A near-miss such as
// "assertedcustom" must not count: the agent's read operations used to accept
// any "asserted" prefix, and this pins the three-type rule they now use.
TEST(ParserModelUtilsTest, RelationshipTypesAreExactlyTheThreeAssertedRelationships) {
    EXPECT_TRUE(parser::IsRelationshipType("assertedinference"));
    EXPECT_TRUE(parser::IsRelationshipType("assertedcontext"));
    EXPECT_TRUE(parser::IsRelationshipType("assertedevidence"));

    EXPECT_FALSE(parser::IsRelationshipType("assertedcustom"));
    EXPECT_FALSE(parser::IsRelationshipType("asserted"));
    EXPECT_FALSE(parser::IsRelationshipType("AssertedInference"));
    EXPECT_FALSE(parser::IsRelationshipType("claim"));
    EXPECT_FALSE(parser::IsRelationshipType(""));

    parser::SacmElement inference;
    inference.type = "assertedinference";
    EXPECT_TRUE(parser::IsRelationshipElement(inference));
    parser::SacmElement evidence;
    evidence.type = "assertedevidence";
    EXPECT_TRUE(parser::IsRelationshipElement(evidence));
    parser::SacmElement near_miss;
    near_miss.type = "assertedcustom";
    EXPECT_FALSE(parser::IsRelationshipElement(near_miss));
}
