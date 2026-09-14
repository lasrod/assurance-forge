#include "eval/sccg_review_eval_options.h"

#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <format>
#include <limits>
#include <system_error>

namespace eval {
namespace {

// A whole number given on the command line. `atoi` and its family read "5x"
// as 5 and "x" as 0 without a word, so a sweep started with a typo in --runs
// ran once and reported nothing wrong.
bool ParseWholeNumber(const std::string& text, long long& out) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto [stopped, result] = std::from_chars(begin, end, out);
    return !text.empty() && result == std::errc() && stopped == end;
}

bool ParseRealNumber(const std::string& text, double& out) {
    if (text.empty())
        return false;
    char* stopped = nullptr;
    errno = 0;
    out = std::strtod(text.c_str(), &stopped);
    return errno == 0 && stopped == text.c_str() + text.size();
}

// Reads an option's value as a whole number no smaller than `minimum` that fits
// `Number`, or says which option was wrong. Narrowing without the range check
// read --runs 4294967297 as 1, and --timeout 0 switched the request deadline
// off rather than being refused.
template <typename Number>
void ReadWholeNumber(const char* option, const std::string& text, long long minimum, Number& out, std::string& error) {
    if (!error.empty())
        return; // the option had no value at all, which the caller already said
    const long long maximum = static_cast<long long>(std::numeric_limits<Number>::max());
    long long value = 0;
    if (!ParseWholeNumber(text, value)) {
        error = std::format("{} needs a whole number, not '{}'.", option, text);
        return;
    }
    if (value < minimum || value > maximum) {
        error = std::format("{} needs a whole number from {} to {}, not '{}'.", option, minimum, maximum, text);
        return;
    }
    out = static_cast<Number>(value);
}

} // namespace

bool ParseArgs(const std::vector<std::string>& arguments, Options& options, std::string& error) {
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string& arg = arguments[i];
        const auto value = [&](const char* name) -> std::string {
            if (i + 1 >= arguments.size()) {
                error = std::format("{} needs a value.", name);
                return {};
            }
            return arguments[++i];
        };
        if (arg == "--help" || arg == "-h") {
            options.show_help = true;
            return true;
        } else if (arg == "--project") {
            options.project = value("--project");
        } else if (arg == "--element") {
            options.element_ids.push_back(value("--element"));
        } else if (arg == "--profile") {
            options.review_profile_id = value("--profile");
        } else if (arg == "--model") {
            options.model = value("--model");
        } else if (arg == "--temperature") {
            const std::string text = value("--temperature");
            double temperature = 0.0;
            if (ParseRealNumber(text, temperature))
                options.temperature = temperature;
            else if (error.empty())
                error = std::format("--temperature needs a number, not '{}'.", text);
        } else if (arg == "--seed") {
            long long seed = 0;
            ReadWholeNumber("--seed", value("--seed"), std::numeric_limits<long long>::min(), seed, error);
            options.seed = seed;
        } else if (arg == "--runs") {
            ReadWholeNumber("--runs", value("--runs"), 1, options.runs, error);
        } else if (arg == "--first-run") {
            ReadWholeNumber("--first-run", value("--first-run"), 1, options.first_run, error);
        } else if (arg == "--out") {
            options.out_dir = value("--out");
        } else if (arg == "--tag") {
            options.tag = value("--tag");
        } else if (arg == "--consensus") {
            ReadWholeNumber("--consensus", value("--consensus"), 0, options.consensus_minimum, error);
        } else if (arg == "--single-request") {
            options.single_request = true;
        } else if (arg == "--service-tier") {
            options.service_tier = value("--service-tier");
        } else if (arg == "--timeout") {
            int timeout_seconds = 0;
            ReadWholeNumber("--timeout", value("--timeout"), 1, timeout_seconds, error);
            options.request_timeout_seconds = timeout_seconds;
        } else if (arg == "--no-prompt-cache") {
            options.no_prompt_cache = true;
        } else if (arg == "--dry-run") {
            options.dry_run = true;
        } else if (arg == "--list-models") {
            options.list_models = true;
        } else {
            error = std::format("Unknown argument: {}", arg);
            return false;
        }
        if (!error.empty())
            return false;
    }

    // Runs are numbered first_run .. first_run + runs - 1 in an int. Each fits
    // on its own and the sum need not: --first-run 2147483647 --runs 2 wrapped
    // the last number negative, and the harness ran nothing, successfully.
    if (static_cast<long long>(options.first_run) + options.runs - 1 > std::numeric_limits<int>::max()) {
        error = std::format("--first-run {} with --runs {} numbers a run past {}.",
                            options.first_run,
                            options.runs,
                            std::numeric_limits<int>::max());
        return false;
    }

    options.all_elements = options.element_ids.empty();
    if (options.out_dir.empty())
        options.out_dir = std::filesystem::path("sccg-eval-out");
    return true;
}

int LastRunNumber(const Options& options) {
    return options.first_run + options.runs - 1;
}

} // namespace eval
