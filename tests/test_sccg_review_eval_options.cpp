// The evaluation harness's command line.
//
// The harness itself calls a paid provider and is never a CTest; what its
// arguments mean is plain parsing. A sweep started with a misread number is a
// sweep paid for and wrong, and --first-run exists so records from several
// invocations can share a directory -- which only works if the numbers are the
// ones asked for.

#include "eval/sccg_review_eval_options.h"

#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Parsed {
    bool ok = false;
    eval::Options options;
    std::string error;
};

Parsed Parse(const std::vector<std::string>& arguments) {
    Parsed parsed;
    parsed.ok = eval::ParseArgs(arguments, parsed.options, parsed.error);
    return parsed;
}

} // namespace

TEST(SccgReviewEvalOptionsTest, NumbersAnInvocationsRunsFromOneByDefault) {
    const Parsed parsed = Parse({"--project", "case"});
    ASSERT_TRUE(parsed.ok) << parsed.error;
    EXPECT_EQ(parsed.options.first_run, 1);
    EXPECT_EQ(parsed.options.runs, 1);
    EXPECT_EQ(eval::LastRunNumber(parsed.options), 1);
    EXPECT_TRUE(parsed.options.all_elements);
    EXPECT_EQ(parsed.options.out_dir, std::filesystem::path("sccg-eval-out"));
}

// Extending a three-run sweep by two: the new runs are 4 and 5, not 1 and 2.
TEST(SccgReviewEvalOptionsTest, NumbersRunsOnFromFirstRun) {
    const Parsed parsed = Parse({"--project", "case", "--first-run", "4", "--runs", "2", "--consensus", "2"});
    ASSERT_TRUE(parsed.ok) << parsed.error;
    EXPECT_EQ(parsed.options.first_run, 4);
    EXPECT_EQ(parsed.options.runs, 2);
    EXPECT_EQ(eval::LastRunNumber(parsed.options), 5);
    EXPECT_EQ(parsed.options.consensus_minimum, 2);
}

// Each number fits an int on its own; the last run number need not.
TEST(SccgReviewEvalOptionsTest, RefusesRunNumbersThatDoNotFitAnInt) {
    const int largest = std::numeric_limits<int>::max();
    const Parsed last_fits = Parse({"--first-run", std::to_string(largest), "--runs", "1"});
    ASSERT_TRUE(last_fits.ok) << last_fits.error;
    EXPECT_EQ(eval::LastRunNumber(last_fits.options), largest);

    const Parsed overflows = Parse({"--first-run", std::to_string(largest), "--runs", "2"});
    EXPECT_FALSE(overflows.ok);
    EXPECT_NE(overflows.error.find("--first-run"), std::string::npos) << overflows.error;
}

TEST(SccgReviewEvalOptionsTest, RefusesAMalformedOrOutOfRangeNumber) {
    const std::vector<std::pair<std::string, std::string>> refused = {
        {"--runs", "2x"},
        {"--runs", "x"},
        {"--runs", "0"},
        {"--runs", "-1"},
        {"--runs", "4294967297"},
        {"--first-run", "0"},
        {"--consensus", "-1"},
        {"--timeout", "0"},
        {"--timeout", "abc"},
        {"--seed", "1.5"},
        {"--temperature", "warm"},
    };
    for (const auto& [option, value] : refused) {
        SCOPED_TRACE(option + " " + value);
        const Parsed parsed = Parse({option, value});
        EXPECT_FALSE(parsed.ok);
        EXPECT_NE(parsed.error.find(option), std::string::npos) << parsed.error;
    }
}

TEST(SccgReviewEvalOptionsTest, AcceptsTheValuesEachNumberAllows) {
    const Parsed parsed = Parse({"--consensus", "0", "--timeout", "1", "--seed", "-7", "--temperature", "0.5"});
    ASSERT_TRUE(parsed.ok) << parsed.error;
    EXPECT_EQ(parsed.options.consensus_minimum, 0);
    EXPECT_EQ(parsed.options.request_timeout_seconds, 1);
    EXPECT_EQ(parsed.options.seed, -7);
    EXPECT_EQ(parsed.options.temperature, 0.5);
}

TEST(SccgReviewEvalOptionsTest, RefusesAnUnknownArgumentOrAMissingValue) {
    const Parsed unknown = Parse({"--rnus", "3"});
    EXPECT_FALSE(unknown.ok);
    EXPECT_NE(unknown.error.find("--rnus"), std::string::npos) << unknown.error;

    const Parsed missing = Parse({"--runs"});
    EXPECT_FALSE(missing.ok);
    EXPECT_NE(missing.error.find("needs a value"), std::string::npos) << missing.error;
}

// A baseline review has no profile, no passes and no cited guidelines, so an
// option that would act on them is refused rather than silently ignored.
TEST(SccgReviewEvalOptionsTest, ParsesBaselineAndRefusesWhatOnlyAnSccgReviewHas) {
    const Parsed baseline = Parse({"--project", "case", "--baseline", "--runs", "5", "--consensus", "0"});
    ASSERT_TRUE(baseline.ok) << baseline.error;
    EXPECT_TRUE(baseline.options.baseline);
    EXPECT_FALSE(baseline.options.baseline_element_only);
    EXPECT_FALSE(Parse({"--project", "case"}).options.baseline);

    const Parsed element_only = Parse({"--project", "case", "--baseline-element-only"});
    ASSERT_TRUE(element_only.ok) << element_only.error;
    EXPECT_TRUE(element_only.options.baseline);
    EXPECT_TRUE(element_only.options.baseline_element_only);
    EXPECT_FALSE(Parse({"--baseline-element-only", "--single-request"}).ok);

    const std::vector<std::vector<std::string>> conflicting = {
        {"--baseline", "--profile", "claim_review"},
        {"--baseline", "--single-request"},
        {"--baseline", "--consensus", "3"},
    };
    for (const std::vector<std::string>& arguments : conflicting) {
        SCOPED_TRACE(arguments[1]);
        const Parsed parsed = Parse(arguments);
        EXPECT_FALSE(parsed.ok);
        EXPECT_NE(parsed.error.find("--baseline"), std::string::npos) << parsed.error;
    }
}

// Help is a result the caller acts on, not an exit from inside the parser.
TEST(SccgReviewEvalOptionsTest, ReportsARequestForHelp) {
    const Parsed parsed = Parse({"--help"});
    ASSERT_TRUE(parsed.ok) << parsed.error;
    EXPECT_TRUE(parsed.options.show_help);
}
