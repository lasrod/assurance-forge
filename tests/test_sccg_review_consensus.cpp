// Agreement across repeated reviews.
//
// The provider offers no way to make a review repeatable -- gpt-5.5 and
// gpt-5.6-sol both reject `temperature`, and `seed` is not a Responses API
// parameter -- so the tool measures the variation instead of suppressing it.
// These hold the counting rules, because a consensus that miscounts is worse
// than none: it puts a number of runs behind a finding that did not earn it.

#include "review/sccg/sccg_review_consensus.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

review::AiReviewParseResult RunCiting(const std::vector<std::string>& guideline_ids,
                                      const std::string& element_id = "G1",
                                      const std::string& message_suffix = {}) {
    review::AiReviewParseResult result;
    result.reviewedElementId = element_id;
    for (const std::string& guideline_id : guideline_ids) {
        core::ProblemItem problem;
        problem.guideline_id = guideline_id;
        problem.element_id = element_id;
        problem.severity = core::ProblemSeverity::Warning;
        problem.message = guideline_id + " objection" + message_suffix;
        result.problems.push_back(problem);
        result.findingConfidences.push_back("high");
        result.suggestedElementTexts.push_back({});
        result.proposedOperations.push_back({});
    }
    return result;
}

review::AiReviewParseResult FailedRun(const std::string& error) {
    review::AiReviewParseResult result;
    result.errorMessage = error;
    return result;
}

const review::ConsensusFinding* Find(const std::vector<review::ConsensusFinding>& findings,
                                     const std::string& guideline_id) {
    for (const review::ConsensusFinding& finding : findings) {
        if (finding.guideline_id == guideline_id)
            return &finding;
    }
    return nullptr;
}

} // namespace

TEST(SccgReviewConsensusTest, CountsTheRunsThatCitedEachGuideline) {
    const std::vector<review::AiReviewParseResult> runs{
        RunCiting({"CL.5", "AR.6"}),
        RunCiting({"CL.5", "RD.2"}),
        RunCiting({"CL.5"}),
    };

    const review::ConsensusReviewResult consensus = review::BuildConsensusReview(runs, 3, 1, {});
    ASSERT_EQ(consensus.runs_succeeded, 3);
    ASSERT_EQ(consensus.findings.size(), 3u);

    const review::ConsensusFinding* unanimous = Find(consensus.findings, "CL.5");
    ASSERT_NE(unanimous, nullptr);
    EXPECT_EQ(unanimous->runs_citing, 3);
    EXPECT_EQ(unanimous->runs_total, 3);
    EXPECT_TRUE(unanimous->unanimous());

    const review::ConsensusFinding* once = Find(consensus.findings, "AR.6");
    ASSERT_NE(once, nullptr);
    EXPECT_EQ(once->runs_citing, 1);
    EXPECT_FALSE(once->unanimous());

    // Best-supported first, so a reviewer reads the strongest finding first.
    EXPECT_EQ(consensus.findings.front().guideline_id, "CL.5");
}

// A run that raised the same guideline against the same element twice has found
// one thing worth saying twice. Counting both would let one run out-vote two.
TEST(SccgReviewConsensusTest, ARunVotesOncePerGuideline) {
    const std::vector<review::AiReviewParseResult> runs{
        RunCiting({"CL.5", "CL.5", "CL.5"}),
        RunCiting({"AR.6"}),
    };

    const review::ConsensusReviewResult consensus = review::BuildConsensusReview(runs, 2, 1, {});
    const review::ConsensusFinding* repeated = Find(consensus.findings, "CL.5");
    ASSERT_NE(repeated, nullptr);
    EXPECT_EQ(repeated->runs_citing, 1) << "three findings in one run are still one run";
    EXPECT_EQ(repeated->runs_total, 2);
    // Every wording is kept even though the vote is one.
    EXPECT_EQ(repeated->messages.size(), 3u);
}

// The same guideline against two different elements is two findings: a reviewer
// disposes of them separately.
TEST(SccgReviewConsensusTest, SeparatesTheSameGuidelineOnDifferentElements) {
    std::vector<review::AiReviewParseResult> runs{RunCiting({"AR.6"}, "G1"), RunCiting({"AR.6"}, "G2")};
    const review::ConsensusReviewResult consensus = review::BuildConsensusReview(runs, 2, 1, {});
    ASSERT_EQ(consensus.findings.size(), 2u);
    EXPECT_EQ(consensus.findings[0].runs_citing, 1);
    EXPECT_EQ(consensus.findings[1].runs_citing, 1);
}

// Below the floor is kept, not dropped. A finding one run of three raised is
// weak evidence, and discarding it silently would hide the instability this
// exists to measure.
TEST(SccgReviewConsensusTest, KeepsFindingsBelowTheFloorSeparately) {
    const std::vector<review::AiReviewParseResult> runs{
        RunCiting({"CL.5", "AR.6"}),
        RunCiting({"CL.5"}),
        RunCiting({"CL.5"}),
    };

    const review::ConsensusReviewResult consensus = review::BuildConsensusReview(runs, 3, 2, {});
    ASSERT_EQ(consensus.findings.size(), 1u);
    EXPECT_EQ(consensus.findings[0].guideline_id, "CL.5");
    ASSERT_EQ(consensus.below_threshold.size(), 1u);
    EXPECT_EQ(consensus.below_threshold[0].guideline_id, "AR.6");
    EXPECT_EQ(consensus.below_threshold[0].runs_citing, 1);
}

// 2 of 2 agreeing when the third run errored is not 3 of 3, and the record has
// to be able to say which happened.
TEST(SccgReviewConsensusTest, CountsAgainstTheRunsThatSucceeded) {
    const std::vector<review::AiReviewParseResult> runs{
        RunCiting({"CL.5"}),
        FailedRun("the response was not JSON"),
        RunCiting({"CL.5"}),
    };

    const review::ConsensusReviewResult consensus = review::BuildConsensusReview(runs, 3, 1, {});
    EXPECT_EQ(consensus.runs_requested, 3);
    EXPECT_EQ(consensus.runs_succeeded, 2);
    ASSERT_EQ(consensus.run_errors.size(), 1u);
    ASSERT_EQ(consensus.findings.size(), 1u);
    EXPECT_EQ(consensus.findings[0].runs_citing, 2);
    EXPECT_EQ(consensus.findings[0].runs_total, 2) << "the failed run must not count as a run that disagreed";
    EXPECT_TRUE(consensus.findings[0].unanimous());
}

TEST(SccgReviewConsensusTest, ReportsNoConsensusWhenEveryRunFailed) {
    const std::vector<review::AiReviewParseResult> runs{FailedRun("boom"), FailedRun("boom")};
    const review::ConsensusReviewResult consensus = review::BuildConsensusReview(runs, 2, 1, {});
    EXPECT_FALSE(consensus.ok());
    EXPECT_TRUE(consensus.findings.empty());
    EXPECT_EQ(consensus.run_errors.size(), 2u);
}

// A finding a deterministic pre-check independently reached is corroborated by
// something that did not come from a model. That is the signal that replaced
// the severity the model was told what to write.
TEST(SccgReviewConsensusTest, CarriesPrecheckCorroboration) {
    review::sccg::PrecheckResult fired;
    fired.precheck_id = "check-evidence-citation-precision";
    fired.guideline_ids = {"EV.4"};
    fired.candidate = true;

    review::sccg::PrecheckResult silent;
    silent.precheck_id = "check-explicit-strategy";
    silent.guideline_ids = {"AR.2"};
    silent.candidate = false;

    const std::vector<review::AiReviewParseResult> runs{RunCiting({"EV.4", "AR.2"})};
    const review::ConsensusReviewResult consensus = review::BuildConsensusReview(runs, 1, 1, {fired, silent});

    const review::ConsensusFinding* corroborated = Find(consensus.findings, "EV.4");
    ASSERT_NE(corroborated, nullptr);
    ASSERT_EQ(corroborated->corroborating_precheck_ids.size(), 1u);
    EXPECT_EQ(corroborated->corroborating_precheck_ids[0], "check-evidence-citation-precision");

    // A pre-check that did not fire corroborates nothing -- "did not fire" and
    // "agrees" are different facts.
    const review::ConsensusFinding* uncorroborated = Find(consensus.findings, "AR.2");
    ASSERT_NE(uncorroborated, nullptr);
    EXPECT_TRUE(uncorroborated->corroborating_precheck_ids.empty());
}
