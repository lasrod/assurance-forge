#pragma once

// The command line of af-sccg-review-eval.
//
// Separate from the harness's `main` so the tests can reach it. The harness
// calls a paid provider and is never a CTest, but what its arguments mean is
// plain parsing -- and a sweep started with a misread argument is a sweep paid
// for and wrong.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace eval {

struct Options {
    std::filesystem::path project;
    std::vector<std::string> element_ids;
    // Empty means: every element the loaded case offers that SCCG review
    // supports. That is what a coverage run wants, and typing seven ids is what
    // it would otherwise cost.
    bool all_elements = false;
    std::string review_profile_id;
    std::string model;
    // Unset means the provider decides, which is a real setting and not a
    // missing one -- a reasoning model rejects a temperature outright.
    std::optional<double> temperature;
    std::optional<long long> seed;
    int runs = 1;
    // The number the first run of this invocation gets. Runs are files named
    // by number, so adding runs 4 and 5 to a sweep that has 1-3 needs the new
    // ones numbered on from there rather than overwriting the first two.
    int first_run = 1;
    std::filesystem::path out_dir;
    // Assemble and record the request without sending it. The prompt, the
    // profile, the packages and the pre-checks are all decided before the
    // provider is involved, so checking them costs nothing and should not
    // require a paid call.
    bool dry_run = false;
    bool list_models = false;
    // Findings must be cited by at least this many runs to reach the consensus
    // list. 0 disables the consensus pass entirely and only per-run records are
    // written.
    int consensus_minimum = 0;
    // Send the whole profile in one request even where SCCG publishes review
    // passes. The app never does this; the harness can, so the two can be
    // compared on the same material.
    bool single_request = false;
    // The provider's processing tier. "flex" costs about half, and may queue:
    // a sweep can wait, a user cannot, so only the harness offers it.
    std::optional<std::string> service_tier;
    // Seconds one request may take. Unset: 120, or 900 on the flex tier.
    std::optional<int> request_timeout_seconds;
    // Send the prompt as one uncached string, as before prompt caching, so a
    // sweep can measure what caching changes.
    bool no_prompt_cache = false;
    std::string tag;
    // --help was given. Parsing stops there and the caller prints the usage.
    bool show_help = false;
};

// Reads the harness's arguments, without the program name. False, with `error`
// naming the argument, for anything the harness would otherwise have to guess
// at: an unknown option, a missing value, a number that is malformed or out of
// range, or run numbers that do not fit an int.
bool ParseArgs(const std::vector<std::string>& arguments, Options& options, std::string& error);

// The number the last run of this invocation gets. ParseArgs refuses a range
// whose last number would not fit, so on parsed options this cannot overflow.
int LastRunNumber(const Options& options);

} // namespace eval
