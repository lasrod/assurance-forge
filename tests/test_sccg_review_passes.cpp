// Merging the review passes of one profile.
//
// SCCG 0.8.0 lets a profile be reviewed as one request per pass. The merge is
// where a pass-split review could go quietly wrong: a pass that failed read as
// a pass that found nothing, a finding counted twice, or a finding filed under a
// guideline its pass was never given. These hold each of those closed.

#include "review/sccg/sccg_review_passes.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

review::SccgReviewPassRequest PassRequest(const std::string& id, std::vector<std::string> guideline_ids) {
    review::SccgReviewPassRequest pass;
    pass.pass_id = id;
    pass.display_name = id;
    pass.guideline_ids = std::move(guideline_ids);
    pass.request.prompt = "prompt for " + id + "\nsecond line";
    return pass;
}

std::vector<review::SccgReviewPassRequest> TwoPasses() {
    return {PassRequest("wording", {"CL.4", "CL.5"}), PassRequest("structure", {"AR.6", "AR.7"})};
}

// A pass's parse result: `cited` is what the model wrote, `allowed` whether the
// pass was given that guideline (the parser empties a refused id).
review::AiReviewParseResult Found(const std::vector<std::pair<std::string, bool>>& cited,
                                  const std::string& element_id = "G1") {
    review::AiReviewParseResult result;
    result.reviewedElementId = element_id;
    for (const auto& [guideline_id, allowed] : cited) {
        core::ProblemItem problem;
        problem.element_id = element_id;
        problem.guideline_id = allowed ? guideline_id : std::string{};
        problem.message = guideline_id + " finding";
        result.problems.push_back(problem);
        result.citedGuidelineIds.push_back(guideline_id);
        result.findingConfidences.push_back("high");
        result.suggestedElementTexts.push_back({});
        result.proposedOperations.push_back({});
    }
    return result;
}

review::ReviewPassOutcome Outcome(const std::string& pass_id, review::AiReviewParseResult result) {
    review::ReviewPassOutcome outcome;
    outcome.pass_id = pass_id;
    outcome.result = std::move(result);
    return outcome;
}

std::vector<std::string> GuidelinesOf(const review::AiReviewParseResult& result) {
    std::vector<std::string> ids;
    for (const core::ProblemItem& problem : result.problems)
        ids.push_back(problem.guideline_id);
    return ids;
}

} // namespace

TEST(SccgReviewPassesTest, MergesEveryPassesFindingsInPassOrder) {
    const std::vector<review::ReviewPassOutcome> outcomes{
        Outcome("structure", Found({{"AR.6", true}})),
        Outcome("wording", Found({{"CL.5", true}, {"CL.4", true}})),
    };

    const review::MergedReviewPasses merged = review::MergeReviewPasses(outcomes, TwoPasses(), "G1");
    EXPECT_TRUE(merged.complete());
    EXPECT_TRUE(merged.merged.errorMessage.empty());
    EXPECT_EQ(merged.passes_total, 2);
    // Pass order, not arrival order: the wording pass is listed first.
    EXPECT_EQ(GuidelinesOf(merged.merged), (std::vector<std::string>{"CL.5", "CL.4", "AR.6"}));
    // The parallel vectors stay parallel.
    EXPECT_EQ(merged.merged.citedGuidelineIds.size(), 3u);
    EXPECT_EQ(merged.merged.findingConfidences.size(), 3u);
    EXPECT_EQ(merged.merged.proposedOperations.size(), 3u);
}

// A pass that cites a guideline another pass reviews is answering a question it
// was not asked -- the owning pass asked it in its own request. Discarded, and
// said so, rather than counted twice or filed as an unknown reference.
TEST(SccgReviewPassesTest, DiscardsAFindingCitedOutsideItsPass) {
    const std::vector<review::ReviewPassOutcome> outcomes{
        Outcome("wording", Found({{"CL.5", true}})),
        Outcome("structure", Found({{"AR.6", true}, {"CL.5", false}})),
    };

    const review::MergedReviewPasses merged = review::MergeReviewPasses(outcomes, TwoPasses(), "G1");
    EXPECT_EQ(GuidelinesOf(merged.merged), (std::vector<std::string>{"CL.5", "AR.6"}));
    ASSERT_EQ(merged.discarded_findings.size(), 1u);
    EXPECT_NE(merged.discarded_findings[0].find("CL.5"), std::string::npos);
    EXPECT_NE(merged.discarded_findings[0].find("wording"), std::string::npos);
}

// An id no pass owns is a genuinely unknown reference: it stays, flagged, as in
// a single-request review. Raised by two passes it is still one reference.
TEST(SccgReviewPassesTest, KeepsAGenuinelyUnknownReferenceOnce) {
    const std::vector<review::ReviewPassOutcome> outcomes{
        Outcome("wording", Found({{"ZZ.1", false}})),
        Outcome("structure", Found({{"ZZ.1", false}})),
    };

    const review::MergedReviewPasses merged = review::MergeReviewPasses(outcomes, TwoPasses(), "G1");
    ASSERT_EQ(merged.merged.problems.size(), 1u);
    EXPECT_TRUE(merged.merged.problems[0].guideline_id.empty());
    EXPECT_EQ(merged.merged.citedGuidelineIds[0], "ZZ.1");
    EXPECT_TRUE(merged.discarded_findings.empty());
}

// ...but two different findings in ONE pass citing the same unknown id are two
// findings. Deduplication is across passes only.
TEST(SccgReviewPassesTest, DoesNotDeduplicateWithinAPass) {
    const std::vector<review::ReviewPassOutcome> outcomes{
        Outcome("wording", Found({{"ZZ.1", false}, {"ZZ.1", false}})),
        Outcome("structure", Found({})),
    };
    const review::MergedReviewPasses merged = review::MergeReviewPasses(outcomes, TwoPasses(), "G1");
    EXPECT_EQ(merged.merged.problems.size(), 2u);
}

// The failure a pass-split review must never hide: one pass failing looks
// exactly like one pass finding nothing unless the merge says otherwise.
TEST(SccgReviewPassesTest, ReportsAReviewWithAFailedPassAsIncomplete) {
    review::ReviewPassOutcome failed;
    failed.pass_id = "structure";
    failed.error = "the request failed: timeout";

    const std::vector<review::ReviewPassOutcome> outcomes{Outcome("wording", Found({{"CL.4", true}})), failed};
    const review::MergedReviewPasses merged = review::MergeReviewPasses(outcomes, TwoPasses(), "G1");

    EXPECT_FALSE(merged.complete());
    EXPECT_TRUE(merged.any_succeeded());
    EXPECT_EQ(merged.failed_pass_ids, (std::vector<std::string>{"structure"}));
    ASSERT_EQ(merged.pass_errors.size(), 1u);
    EXPECT_NE(merged.pass_errors[0].find("timeout"), std::string::npos);
    // The passes that ran still count: their findings reach the reviewer.
    EXPECT_EQ(GuidelinesOf(merged.merged), (std::vector<std::string>{"CL.4"}));
    EXPECT_TRUE(merged.merged.errorMessage.empty()) << "some passes succeeded, so this is not a failed review";
}

TEST(SccgReviewPassesTest, FailsTheReviewWhenEveryPassFailed) {
    review::ReviewPassOutcome parse_failed = Outcome("wording", {});
    parse_failed.result.errorMessage = "not JSON";
    review::ReviewPassOutcome request_failed;
    request_failed.pass_id = "structure";
    request_failed.error = "401";

    const review::MergedReviewPasses merged =
        review::MergeReviewPasses({parse_failed, request_failed}, TwoPasses(), "G1");
    EXPECT_FALSE(merged.any_succeeded());
    EXPECT_NE(merged.merged.errorMessage.find("not JSON"), std::string::npos);
    EXPECT_NE(merged.merged.errorMessage.find("401"), std::string::npos);
}

// A pass that never answered is failed, not empty.
TEST(SccgReviewPassesTest, TreatsAMissingPassAsFailed) {
    const review::MergedReviewPasses merged =
        review::MergeReviewPasses({Outcome("wording", Found({}))}, TwoPasses(), "G1");
    EXPECT_FALSE(merged.complete());
    EXPECT_EQ(merged.failed_pass_ids, (std::vector<std::string>{"structure"}));
}

TEST(SccgReviewPassesTest, FailsAPassThatReviewedADifferentElement) {
    const std::vector<review::ReviewPassOutcome> outcomes{
        Outcome("wording", Found({{"CL.4", true}})),
        Outcome("structure", Found({{"AR.6", true}}, "G9")),
    };
    const review::MergedReviewPasses merged = review::MergeReviewPasses(outcomes, TwoPasses(), "G1");
    EXPECT_EQ(merged.failed_pass_ids, (std::vector<std::string>{"structure"}));
    EXPECT_EQ(GuidelinesOf(merged.merged), (std::vector<std::string>{"CL.4"}));
}

// The debug view shows every pass's prompt under a separator. An edit that
// keeps the separators goes back to the pass it was made in.
TEST(SccgReviewPassesTest, SplitsAnEditedCombinedPromptBackIntoItsPasses) {
    std::vector<review::SccgReviewPassRequest> passes = TwoPasses();
    std::string combined = review::CombinePassPrompts(passes);

    // Unchanged, the round trip is exact.
    std::vector<review::SccgReviewPassRequest> round_trip = passes;
    ASSERT_TRUE(review::SplitPassPrompts(combined, round_trip));
    EXPECT_EQ(round_trip[0].request.prompt, passes[0].request.prompt);
    EXPECT_EQ(round_trip[1].request.prompt, passes[1].request.prompt);

    // An edit inside the second pass lands in the second pass only.
    const std::string original = passes[1].request.prompt;
    combined.replace(combined.find(original), original.size(), "edited structure prompt");
    ASSERT_TRUE(review::SplitPassPrompts(combined, passes));
    EXPECT_EQ(passes[0].request.prompt, "prompt for wording\nsecond line");
    EXPECT_EQ(passes[1].request.prompt, "edited structure prompt");
}

// An edit that removed a separator cannot be attributed to a pass, and the split
// refuses rather than guessing -- leaving the passes exactly as they were.
TEST(SccgReviewPassesTest, RefusesToSplitWhenASeparatorWasRemoved) {
    std::vector<review::SccgReviewPassRequest> passes = TwoPasses();
    const std::string combined = review::CombinePassPrompts(passes);
    const std::size_t second = combined.find("===== SCCG review pass 2/2");
    ASSERT_NE(second, std::string::npos);
    const std::string mangled = combined.substr(0, second) + "whatever the user typed";

    EXPECT_FALSE(review::SplitPassPrompts(mangled, passes));
    EXPECT_EQ(passes[0].request.prompt, "prompt for wording\nsecond line");
    EXPECT_EQ(passes[1].request.prompt, "prompt for structure\nsecond line");
}

// Text before the first separator belongs to no pass. Splitting from the first
// separator dropped it -- a reviewer's note never sent -- so the split refuses,
// and the edited prompt goes as one request with the note in it.
TEST(SccgReviewPassesTest, RefusesToSplitWhenTextPrecedesTheFirstSeparator) {
    std::vector<review::SccgReviewPassRequest> passes = TwoPasses();
    const std::string prefixed = "Reviewer's note: look hard at the wording.\n" + review::CombinePassPrompts(passes);

    EXPECT_FALSE(review::SplitPassPrompts(prefixed, passes));
    EXPECT_EQ(passes[0].request.prompt, "prompt for wording\nsecond line");
    EXPECT_EQ(passes[1].request.prompt, "prompt for structure\nsecond line");
}
