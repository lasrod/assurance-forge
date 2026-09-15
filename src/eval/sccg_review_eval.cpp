// Offline SCCG review evaluation harness.
//
// Runs the application's own SCCG review method over a project without a
// window: it opens the project, builds the same request `AiReviewController`
// would build for a selected element, sends it to the configured provider, and
// parses the response with the same validator. What it adds is a record: one
// JSON file per run stating the SCCG version, the profile, the guidelines
// carried, the data packages supplied and the ones declared absent, what the
// pre-checks decided, the model and prompt that were used, and every finding
// with its cited guideline.
//
// It exists because the review method was only reachable by rendering a frame,
// which makes a claim like "this guideline fires on this element" impossible to
// check except by hand, one element at a time. Guideline coverage over a whole
// argument, and the run-to-run variation a non-deterministic model produces,
// are measurements, and a measurement needs a harness.
//
// It is NOT a test. It calls a paid external provider and its output depends on
// a model, so it must never join the CTest gates; `PrepareSccgReview` is what
// the tests cover, and this is what turns the prepared request into evidence.

#include "ai/ai_service.h"
#include "ai/ai_settings.h"
#include "ai/libcurl_http_client.h"
#include "ai/openai_provider.h"
#include "ai/secret_store.h"
#include "core/app_state.h"
#include "core/assurance_tree.h"
#include "core/guideline_catalog.h"
#include "core/project_service.h"
#include "core/sha256.h"
#include "eval/baseline_review.h"
#include "eval/sccg_review_eval_options.h"
#include "review/sccg/sccg_review.h"
#include "review/sccg/sccg_review_consensus.h"
#include "review/sccg/sccg_review_passes.h"
#include "review/sccg/sccg_review_preparation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <future>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// Stamped at BUILD time by cmake/write_build_id.cmake, so a commit or a
// checkout in the same build tree is reflected on the next build. The
// static-analysis job configures without building, so the header may not exist
// when a tool reads this file; the id is then unknown, which is what it is.
#if __has_include("af_build_id.h")
#include "af_build_id.h"
#endif
#ifndef AF_BUILD_ID
#define AF_BUILD_ID "unknown"
#endif

namespace {

using eval::Options;
using nlohmann::json;

constexpr const char* kBuildId = AF_BUILD_ID;

void PrintUsage() {
    std::cout << R"(af-sccg-review-eval - run SCCG-guided AI review over a project, offline from the GUI.

Usage:
  af-sccg-review-eval --project <af.proj|dir> [options]
  af-sccg-review-eval --list-models

Options:
  --project <path>     Project manifest (af.proj) or the directory holding one.
  --element <id>       Element to review. Repeatable. Default: every supported element.
  --profile <id>       Force an SCCG review profile instead of selecting on the element's role.
  --model <name>       Provider model. Default: the model in the saved AI settings.
  --runs <n>           Repeat each review n times. Default 1. A model is non-deterministic;
                       one run cannot tell a miss from sampling noise.
  --temperature <t>    Sampling temperature. Omitted entirely unless given, because some
                       models reject the parameter. 0 is the most repeatable a provider offers.
  --seed <n>           Sampling seed, where the provider honours one.
  --first-run <n>      Number this invocation's runs from n (default 1), to add runs to a sweep
                       already on disk without overwriting it. Consensus then covers only the
                       runs of this invocation.
  --out <dir>          Directory for the run records. Default: ./sccg-eval-out
  --tag <text>         Free text stored in every record of this invocation.
  --consensus <m>      After the runs, write a consensus record per element: findings grouped
                       by guideline with the number of runs citing each. m is the floor for
                       the main list; findings below it are kept separately, not dropped.
                       Needs --runs > 1 to mean anything.
  --single-request     Send the whole profile as one request even where it publishes review
                       passes, for comparison. The application always sends the passes.
  --service-tier <t>   Provider processing tier, e.g. flex: about half the price, and slower.
                       A request the tier has no capacity for is retried with backoff.
  --timeout <s>        Seconds one request may take. Default 120, or 900 with --service-tier flex.
  --no-prompt-cache    Send each prompt as one uncached string. By default the shared
                       instructions and each pass's rules are cached, and with --runs > 1 so
                       is each element's data, since every later run repeats it.
  --baseline           Review without SCCG, as a control: the same system instruction, element
                       and surrounding argument, with a generic review prompt and no catalogue,
                       profile, passes or pre-checks. Not with --profile, --single-request or
                       --consensus.
  --dry-run            Assemble and record the request; do not call the provider.
  --list-models        List the models the configured account offers, newest first, then exit.
  --help
)";
}

struct AiStack {
    std::shared_ptr<ai::ISecretStore> secret_store;
    std::shared_ptr<ai::AiService> service;
};

AiStack MakeAiStack() {
    auto settings_store = std::make_shared<ai::AiSettingsStore>();
    auto secret_store = ai::CreatePlatformSecretStore();
    auto http_client = std::make_shared<ai::LibCurlHttpClient>();
    auto provider = std::make_shared<ai::OpenAiProvider>(http_client);
    return AiStack{secret_store, std::make_shared<ai::AiService>(settings_store, secret_store, provider)};
}

// The provider request for one review request, cached the way the
// application caches it -- after the shared instructions and after the pass's
// rules -- and, when the element is reviewed more than once, after its data.
ai::AiRequest ReviewRequest(const review::AiReviewRequestArtifacts& artifacts, bool cache, bool cache_element_data) {
    ai::AiRequest request;
    request.systemInstruction = artifacts.systemInstruction;
    request.userPrompt = artifacts.prompt;
    if (!cache) {
        // Uncached on purpose, and said so: with no breakpoints the provider
        // would place its own at the end of the prompt, and the comparison this
        // exists for would compare caching with caching.
        request.promptCacheDisabled = true;
        return request;
    }
    for (std::size_t index = 0; index < artifacts.promptSegments.size(); ++index) {
        const bool last = index + 1 == artifacts.promptSegments.size();
        request.promptSegments.push_back({artifacts.promptSegments[index], !last || cache_element_data});
    }
    request.promptCacheKey = artifacts.promptCacheKey;
    return request;
}

struct GenerateResult {
    ai::AiResponse response;
    int attempts = 0;
    long long elapsed_ms = 0;
};

// Refusals the provider makes before doing any work: a rate limit, the flex
// tier having no capacity, or an overloaded server. None is charged, and each
// is worth waiting out in a sweep that was going to take an hour anyway. A
// timeout is not retried -- the provider may have done, and billed, the work --
// and neither is an exhausted account, which waiting does not fix.
bool IsWorthRetrying(const ai::AiResponse& response) {
    if (response.errorCode == ai::AiErrorCode::RateLimited)
        return true;
    return response.httpStatus == 500 || response.httpStatus == 502 || response.httpStatus == 503;
}

GenerateResult
GenerateWithRetry(ai::AiService& service, const ai::AiRequest& request, const ai::AiProviderSettings& settings) {
    constexpr int kMaxAttempts = 7;
    std::chrono::seconds wait(15);
    GenerateResult result;
    for (result.attempts = 1;; ++result.attempts) {
        result.response = service.Generate(request, settings);
        if (result.response.success || !IsWorthRetrying(result.response) || result.attempts == kMaxAttempts)
            return result;
        std::this_thread::sleep_for(wait);
        wait *= 2;
    }
}

void AddUsage(ai::AiUsage& total, const ai::AiUsage& usage) {
    if (!usage.reported)
        return;
    total.reported = true;
    total.inputTokens += usage.inputTokens;
    total.cachedInputTokens += usage.cachedInputTokens;
    total.cacheWriteTokens += usage.cacheWriteTokens;
    total.outputTokens += usage.outputTokens;
    total.reasoningTokens += usage.reasoningTokens;
}

json UsageJson(const ai::AiUsage& usage) {
    if (!usage.reported)
        return nullptr;
    return json{{"input_tokens", usage.inputTokens},
                {"cached_input_tokens", usage.cachedInputTokens},
                {"cache_write_tokens", usage.cacheWriteTokens},
                {"output_tokens", usage.outputTokens},
                {"reasoning_tokens", usage.reasoningTokens}};
}

// A run's usage is the sum of its passes', and the sum is the run's cost only
// when every pass reported usage. `complete` says whether it did: without it a
// run with one unreported pass read as cheaper than it was.
json RunUsageJson(const ai::AiUsage& total, std::size_t passes_reporting, std::size_t passes) {
    json usage = UsageJson(total);
    if (usage.is_object()) {
        usage["passes_reporting"] = passes_reporting;
        usage["complete"] = passes_reporting == passes;
    }
    return usage;
}

json ModelJson(const ai::AiProviderSettings& settings, const Options& options) {
    return json{{"name", settings.model},
                {"provider", ai::ToString(settings.provider)},
                {"temperature", settings.temperature.has_value() ? json(*settings.temperature) : json(nullptr)},
                {"seed", settings.seed.has_value() ? json(*settings.seed) : json(nullptr)},
                {"service_tier", settings.serviceTier.has_value() ? json(*settings.serviceTier) : json(nullptr)},
                {"prompt_cache", options.no_prompt_cache ? "none" : "explicit"}};
}

std::string NowUtcIso() {
    const std::time_t now = std::time(nullptr);
    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &now);
#else
    gmtime_r(&now, &tm_utc);
#endif
    std::ostringstream stream;
    stream << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

std::string SanitizeForFileName(const std::string& value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (char character : value) {
        const bool safe = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                          (character >= '0' && character <= '9') || character == '-' || character == '_';
        sanitized.push_back(safe ? character : '-');
    }
    return sanitized;
}

// Writes one record. A record that could not be written is a failure, not a
// line on stdout: a sweep that reports success over a directory it could not
// write has lost the evidence it was run to produce.
bool WriteRecord(const std::filesystem::path& path, const json& record) {
    std::ofstream stream(path);
    stream << record.dump(2);
    stream.close();
    if (stream)
        return true;
    std::cerr << "Could not write " << path.string() << "\n";
    return false;
}

const char* SeverityName(core::ProblemSeverity severity) {
    return core::ToString(severity);
}

json PackagesJson(const review::AiReviewDataPackageBundle& packages) {
    json available = json::array();
    for (const review::AiReviewDataPackage& package : packages.available)
        available.push_back(package.id);

    json unavailable = json::array();
    for (const review::AiReviewUnavailableDataPackage& package : packages.unavailable) {
        unavailable.push_back(json{
            {"id", package.id},
            {"required", package.required},
            {"absence", review::DataPackageAbsenceToString(package.absence)},
            {"reason", package.reason},
            {"when_absent_statement", package.when_absent_statement},
            {"unassessable_guideline_ids", package.unassessable_guideline_ids},
        });
    }
    return json{{"available", available}, {"unavailable", unavailable}};
}

json PrechecksJson(const std::vector<review::sccg::PrecheckResult>& results) {
    json array = json::array();
    for (const review::sccg::PrecheckResult& result : results) {
        array.push_back(json{
            {"precheck_id", result.precheck_id},
            {"display_name", result.display_name},
            {"guideline_ids", result.guideline_ids},
            {"result_type", result.result_type},
            {"candidate", result.candidate},
            {"unavailable", result.unavailable},
            {"detail", result.detail},
        });
    }
    return array;
}

json FindingsJson(const review::AiReviewParseResult& parsed) {
    json array = json::array();
    for (std::size_t index = 0; index < parsed.problems.size(); ++index) {
        const core::ProblemItem& problem = parsed.problems[index];
        json operations = json::array();
        if (index < parsed.proposedOperations.size()) {
            for (const core::reviews::PatchOperation& operation : parsed.proposedOperations[index])
                operations.push_back(json{{"type", core::reviews::PatchOperationTypeToString(operation.type)},
                                          {"element_id",
                                           operation.element.has_value() && operation.element->existing_id.has_value()
                                               ? *operation.element->existing_id
                                               : std::string{}},
                                          {"create_ref", operation.create_ref.value_or(std::string{})}});
        }
        array.push_back(json{
            {"guideline_id", problem.guideline_id},
            {"element_id", problem.element_id},
            // The severity is the tool's, and the same for every finding; the
            // model's confidence is the ranking signal, so every run record
            // carries it rather than only the consensus.
            {"severity", SeverityName(problem.severity)},
            {"confidence", index < parsed.findingConfidences.size() ? parsed.findingConfidences[index] : std::string{}},
            {"type", problem.type},
            {"message", problem.message},
            {"proposed_operations", operations},
        });
    }
    return array;
}

json ConsensusFindingsJson(const std::vector<review::ConsensusFinding>& findings) {
    json array = json::array();
    for (const review::ConsensusFinding& finding : findings) {
        json operations = json::array();
        for (const core::reviews::PatchOperation& operation : finding.proposedOperations) {
            operations.push_back(json{{"type", core::reviews::PatchOperationTypeToString(operation.type)},
                                      {"element_id",
                                       operation.element.has_value() && operation.element->existing_id.has_value()
                                           ? *operation.element->existing_id
                                           : std::string{}}});
        }
        array.push_back(json{
            {"guideline_id", finding.guideline_id},
            {"element_id", finding.problem.element_id},
            {"runs_citing", finding.runs_citing},
            {"runs_total", finding.runs_total},
            {"unanimous", finding.unanimous()},
            {"confidence", finding.confidence},
            {"corroborating_precheck_ids", finding.corroborating_precheck_ids},
            {"message", finding.problem.message},
            {"messages_per_run", finding.messages},
            {"suggested_element_text", finding.suggestedElementText},
            {"proposed_operations", operations},
        });
    }
    return array;
}

// Every element the loaded case offers that SCCG review supports, in document
// order. Relationships and unsupported types are skipped by the same predicate
// the application uses, so a coverage run reviews exactly the set a user could
// have selected.
std::vector<std::string> SupportedElementIds(const parser::AssuranceCase& assurance_case) {
    std::vector<std::string> ids;
    for (const parser::SacmElement& element : assurance_case.elements) {
        if (review::IsSupportedAiReviewElement(element))
            ids.push_back(element.id);
    }
    return ids;
}

// The provider's own catalogue, read rather than guessed. A model name typed
// from memory is a paid request that fails, or -- worse -- one that succeeds
// against something other than the model the record will claim was used.
//
// Deliberately not routed through IAiProvider: listing models is not inference,
// and giving that interface a second responsibility to serve one diagnostic
// would put it in every implementation forever.
int ListModels(const ai::AiService& service, const ai::ISecretStore& secret_store) {
    const ai::AiProviderSettings settings = service.LoadSettings();
    std::cout << "Configured provider: " << ai::ToString(settings.provider) << "\n";
    std::cout << "Configured model:    " << settings.model << "\n";
    std::cout << "Enabled:             " << (settings.enabled ? "yes" : "no") << "\n";
    std::cout << "Secret store:        " << ai::SecretStoreBackendName() << "\n";

    const ai::SecretLoadResult key =
        const_cast<ai::ISecretStore&>(secret_store).LoadSecret(ai::kSecretServiceName, ai::kOpenAiSecretAccount);
    if (!key.success || !key.secret.has_value() || key.secret->empty()) {
        std::cout << "\nNo API key is stored, so the model list cannot be fetched.\n";
        return 1;
    }

    ai::LibCurlHttpClient http_client;
    ai::HttpRequest request;
    request.url = "https://api.openai.com/v1/models";
    request.headers.push_back(ai::HttpHeader{"Authorization", "Bearer " + *key.secret});
    request.timeoutSeconds = 60;

    const ai::HttpResponse response = http_client.Get(request);
    if (response.statusCode != 200) {
        std::cerr << "\nModel list request failed (HTTP " << response.statusCode
                  << "): " << (response.errorMessage.empty() ? response.body : response.errorMessage) << "\n";
        return 1;
    }

    const json parsed = json::parse(response.body, nullptr, false);
    if (parsed.is_discarded() || !parsed.contains("data")) {
        std::cerr << "\nModel list response could not be parsed.\n";
        return 1;
    }

    std::vector<std::pair<long long, std::string>> models;
    for (const json& entry : parsed["data"]) {
        models.emplace_back(entry.value("created", 0LL), entry.value("id", std::string{}));
    }
    std::sort(
        models.begin(), models.end(), [](const auto& left, const auto& right) { return left.first > right.first; });

    std::cout << "\n" << models.size() << " model(s), newest first:\n";
    for (const auto& [created, id] : models)
        std::cout << "  " << id << "\n";
    return 0;
}

// One element reviewed with --baseline's generic, SCCG-free request. Apart from
// the SCCG loop in `main` so nothing SCCG-specific -- profile, passes, packages,
// pre-checks, consensus -- can reach a baseline record by accident. What the two
// share is the provider call, the retry policy and the shape of the record.
void RunBaselineElement(const Options& options,
                        const parser::AssuranceCase& assurance_case,
                        const core::AssuranceTree& tree,
                        const std::string& element_id,
                        const std::filesystem::path& argument_path,
                        const std::string& session_started,
                        ai::AiService& service,
                        const ai::AiProviderSettings& settings,
                        int& records,
                        int& failures) {
    eval::BaselineReviewRequest request;
    std::string error;
    const bool prepared = eval::BuildBaselineReviewRequest(assurance_case, tree, element_id, request, error);
    const parser::SacmElement* element = review::FindSacmElement(assurance_case, element_id);

    json record;
    record["schema"] = "assurance-forge.sccg-review-eval/1";
    record["mode"] = "baseline";
    record["baseline_prompt_version"] = eval::kBaselinePromptVersion;
    record["session_started_utc"] = session_started;
    record["tag"] = options.tag;
    record["project"] = std::filesystem::absolute(options.project).string();
    record["argument_file"] = argument_path.filename().string();
    // No catalogue went into the request, so none is named: a baseline record
    // stating an SCCG version would read as a review made under it.
    record["sccg_version"] = nullptr;
    record["tool_build"] = kBuildId;
    record["sccg_catalog_path"] = nullptr;
    // The text an SCCG record gives for the element, so a reader of both sets
    // compares like with like.
    record["element"] = json{
        {"id", element_id},
        {"type", element ? review::AiReviewElementType(*element, core::FindTreeNode(tree, element_id)) : std::string{}},
        {"sccg_role", nullptr},
        {"text", element ? (element->content.empty() ? element->description : element->content) : std::string{}},
    };
    record["review_profile"] = nullptr;
    record["guideline_ids"] = json::array();
    record["reviewed_element_ids"] = request.reviewed_element_ids;
    record["data_packages"] = nullptr;
    record["prechecks"] = json::array();
    record["carries_review_history"] = false;

    if (!prepared) {
        record["outcome"] = "preparation-failed";
        record["error"] = error;
        const std::filesystem::path path =
            options.out_dir / (SanitizeForFileName(element_id) + "--preparation-failed.json");
        if (WriteRecord(path, record))
            ++records;
        std::cerr << element_id << ": baseline preparation failed: " << error << "\n";
        ++failures;
        return;
    }

    const std::string prompt_sha256 = core::Sha256::HexDigest(request.prompt);
    record["review_passes"] = 0;
    record["prompt"] = json{{"system_instruction", request.system_instruction},
                            {"user_prompt", request.prompt},
                            {"user_prompt_sha256", prompt_sha256},
                            {"user_prompt_bytes", request.prompt.size()},
                            {"passes_prompt_bytes", request.prompt.size()},
                            {"passes",
                             json::array({json{{"pass_id", ""},
                                               {"guideline_ids", json::array()},
                                               {"user_prompt_sha256", prompt_sha256},
                                               {"user_prompt_bytes", request.prompt.size()}}})}};

    ai::AiRequest ai_request;
    ai_request.systemInstruction = request.system_instruction;
    ai_request.userPrompt = request.prompt;
    if (options.no_prompt_cache) {
        ai_request.promptCacheDisabled = true;
    } else {
        // Every baseline review shares the instruction, and every later run of
        // an element repeats its data, as an SCCG sweep caches its own.
        ai_request.promptSegments.push_back({request.prompt_segments[0], true});
        ai_request.promptSegments.push_back({request.prompt_segments[1], options.runs > 1});
        ai_request.promptCacheKey = eval::kBaselinePromptVersion;
    }

    const int last_run = eval::LastRunNumber(options);
    for (int run = options.first_run; run <= last_run; ++run) {
        json run_record = record;
        run_record["run"] = run;
        run_record["runs_requested"] = options.runs;
        run_record["started_utc"] = NowUtcIso();
        run_record["model"] = ModelJson(settings, options);

        if (options.dry_run) {
            run_record["outcome"] = "dry-run";
        } else {
            const auto started = std::chrono::steady_clock::now();
            const GenerateResult generated = GenerateWithRetry(service, ai_request, settings);
            const long long elapsed_ms = static_cast<long long>(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
                    .count());
            const ai::AiResponse& response = generated.response;
            json pass_record{{"pass_id", ""},
                             {"elapsed_ms", elapsed_ms},
                             {"http_status", response.httpStatus},
                             {"attempts", generated.attempts},
                             {"service_tier", response.serviceTier},
                             {"usage", UsageJson(response.usage)}};
            if (!response.success) {
                const std::string message =
                    response.errorMessage.empty() ? ai::ToString(response.errorCode) : response.errorMessage;
                pass_record["outcome"] = "request-failed";
                pass_record["error"] = message;
                pass_record["error_code"] = ai::ToString(response.errorCode);
                pass_record["provider_error_code"] = response.providerErrorCode;
                pass_record["raw_response"] = response.rawJson;
                run_record["outcome"] = "request-failed";
                run_record["error"] = message;
                run_record["raw_response"] = response.rawJson;
                ++failures;
            } else {
                const std::string raw_response = response.text.empty() ? response.rawJson : response.text;
                const eval::BaselineReviewParseResult parsed = eval::ParseBaselineReviewResponse(raw_response);
                pass_record["raw_response"] = raw_response;
                run_record["raw_response"] = raw_response;
                if (!parsed.error_message.empty()) {
                    pass_record["outcome"] = "parse-failed";
                    pass_record["error"] = parsed.error_message;
                    run_record["outcome"] = "parse-failed";
                    run_record["error"] = parsed.error_message;
                    ++failures;
                } else if (!parsed.reviewed_element_id.empty() && parsed.reviewed_element_id != element_id) {
                    // What an SCCG merge rejects too: a review of some other
                    // element is not a review of this one.
                    const std::string message =
                        "The response reviewed " + parsed.reviewed_element_id + ", not " + element_id + ".";
                    pass_record["outcome"] = "rejected";
                    pass_record["error"] = message;
                    run_record["outcome"] = "rejected";
                    run_record["error"] = message;
                    ++failures;
                } else {
                    json findings = json::array();
                    for (const eval::BaselineFinding& finding : parsed.findings) {
                        findings.push_back(json{{"guideline_id", ""},
                                                {"element_id", element_id},
                                                {"severity", ""},
                                                {"confidence", finding.confidence},
                                                {"type", "baseline"},
                                                {"message", finding.message},
                                                {"proposed_operations", json::array()}});
                    }
                    pass_record["outcome"] = "ok";
                    pass_record["findings"] = findings;
                    run_record["outcome"] = "ok";
                    run_record["findings"] = findings;
                }
            }
            ai::AiUsage usage;
            AddUsage(usage, response.usage);
            run_record["elapsed_ms"] = elapsed_ms;
            run_record["usage"] = RunUsageJson(usage, response.usage.reported ? 1 : 0, 1);
            run_record["passes"] = json::array({pass_record});
            run_record["discarded_findings"] = json::array();
        }

        run_record["finished_utc"] = NowUtcIso();
        const std::filesystem::path path =
            options.out_dir / (SanitizeForFileName(element_id) + "--run" + std::to_string(run) + ".json");
        if (WriteRecord(path, run_record))
            ++records;
        else
            ++failures;
        std::cout << element_id << " [baseline] run " << run << "/" << last_run << ": "
                  << run_record.value("outcome", "") << " -> " << path.string() << "\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    std::string error;
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i)
        arguments.emplace_back(argv[i]);
    if (!eval::ParseArgs(arguments, options, error)) {
        std::cerr << error << "\n\n";
        PrintUsage();
        return 2;
    }
    if (options.show_help) {
        PrintUsage();
        return 0;
    }

    const AiStack ai_stack = MakeAiStack();
    const std::shared_ptr<ai::AiService>& service = ai_stack.service;
    const std::shared_ptr<ai::ISecretStore>& secret_store = ai_stack.secret_store;
    if (options.list_models)
        return ListModels(*service, *secret_store);

    if (options.project.empty()) {
        std::cerr << "--project is required.\n\n";
        PrintUsage();
        return 2;
    }

    core::AssuranceProject project;
    core::ProjectLoadReport report;
    if (!core::ProjectService::OpenProject(options.project, project, report, error)) {
        std::cerr << "Could not open project: " << error << "\n";
        return 1;
    }

    core::AppState state;
    const std::filesystem::path argument_path = [&]() -> std::filesystem::path {
        for (const core::ProjectFileEntry& entry : project.files) {
            if (entry.role == core::ProjectFileRole::SacmArgument)
                return project.rootPath / entry.relativePath;
        }
        return {};
    }();
    if (argument_path.empty()) {
        std::cerr << "Project has no SACM argument file.\n";
        return 1;
    }
    // Through AppState so the case is loaded the way the application loads it:
    // the SACM library reads the file and the render model is projected from
    // the library document. A raw parse produces a different model, and a
    // review of a different model measures nothing.
    if (!state.load_file(argument_path.string())) {
        std::cerr << "Could not load " << argument_path.string() << ": " << state.status_message << "\n";
        return 1;
    }
    if (!state.loaded_case.has_value()) {
        std::cerr << "Loaded file produced no assurance case.\n";
        return 1;
    }

    const parser::AssuranceCase& assurance_case = *state.loaded_case;
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);

    core::GuidelineCatalog catalog;
    // A baseline review uses no SCCG, so it neither needs a catalogue nor may
    // fail for want of one.
    if (!options.baseline && !core::LoadGuidelineCatalog(catalog, error)) {
        std::cerr << "Could not load the SCCG catalog: " << error << "\n";
        return 1;
    }

    ai::AiProviderSettings settings = service->LoadSettings();
    if (!options.model.empty())
        settings.model = options.model;
    if (options.temperature.has_value())
        settings.temperature = options.temperature;
    if (options.seed.has_value())
        settings.seed = options.seed;
    if (options.service_tier.has_value())
        settings.serviceTier = options.service_tier;
    // A flex request can queue for minutes before it starts.
    settings.requestTimeoutSeconds = options.request_timeout_seconds.value_or(
        settings.serviceTier == std::optional<std::string>("flex") ? 900 : settings.requestTimeoutSeconds);

    if (!options.dry_run && !service->HasStoredApiKey()) {
        std::cerr << "No API key is stored for the configured provider. Add one in the application's AI "
                     "preferences, or use --dry-run.\n";
        return 1;
    }

    std::vector<std::string> element_ids =
        options.all_elements ? SupportedElementIds(assurance_case) : options.element_ids;
    if (element_ids.empty()) {
        std::cerr << "No elements to review.\n";
        return 1;
    }

    // Before any provider call: runs that cannot be recorded are runs paid for
    // and lost.
    std::error_code directory_error;
    std::filesystem::create_directories(options.out_dir, directory_error);
    std::error_code probe_error;
    if (!std::filesystem::is_directory(options.out_dir, probe_error)) {
        std::cerr << "Could not create the output directory " << options.out_dir.string()
                  << (directory_error ? ": " + directory_error.message() : std::string{}) << "\n";
        return 1;
    }
    const std::string session_started = NowUtcIso();

    int failures = 0;
    int records = 0;
    for (const std::string& element_id : element_ids) {
        if (options.baseline) {
            RunBaselineElement(options,
                               assurance_case,
                               tree,
                               element_id,
                               argument_path,
                               session_started,
                               *service,
                               settings,
                               records,
                               failures);
            continue;
        }

        // Prior findings are deliberately not carried: the harness reviews the
        // accepted case with no review history, so a run is reproducible from
        // the project files alone. The application passes its own review items
        // here, which is a difference a record has to state rather than hide.
        const review::AiReviewCaseContext case_context;
        const review::SccgReviewPreparation preparation = review::PrepareSccgReview(
            &assurance_case, tree, element_id, options.review_profile_id, case_context, &catalog);

        json record;
        record["schema"] = "assurance-forge.sccg-review-eval/1";
        record["session_started_utc"] = session_started;
        record["tag"] = options.tag;
        record["project"] = std::filesystem::absolute(options.project).string();
        record["argument_file"] = argument_path.filename().string();
        record["sccg_version"] = catalog.document.sccg_version;
        // Which build assembled this request. The prompt depends on the tool's
        // own code -- the response schema and the data-package rules live here,
        // not in the catalogue -- so a record naming only the case and the SCCG
        // version does not say what was sent. Learned the hard way: a prompt
        // regenerated from the same probe and the same SCCG version hashed
        // differently, because the response schema had changed in between.
        record["tool_build"] = kBuildId;
        record["sccg_catalog_path"] = catalog.source_path.string();
        record["element"] = json{
            {"id", preparation.element_id.empty() ? element_id : preparation.element_id},
            {"type", preparation.element_type},
            {"sccg_role", preparation.element_role},
            {"text",
             preparation.payload.selected.content.empty() ? preparation.payload.selected.description
                                                          : preparation.payload.selected.content},
        };
        record["review_profile"] = json{{"id", preparation.review_profile_id},
                                        {"display_name", preparation.review_profile_name},
                                        {"requested_id", options.review_profile_id}};
        record["guideline_ids"] = preparation.guideline_ids;
        record["reviewed_element_ids"] = preparation.reviewed_element_ids;
        record["data_packages"] = PackagesJson(preparation.data_packages);
        record["prechecks"] = PrechecksJson(preparation.precheck_results);
        record["carries_review_history"] = false;

        if (!preparation.ok()) {
            record["outcome"] = "preparation-failed";
            record["error"] = preparation.error_message;
            const std::filesystem::path path =
                options.out_dir / (SanitizeForFileName(element_id) + "--preparation-failed.json");
            if (WriteRecord(path, record))
                ++records;
            std::cerr << element_id << ": preparation failed: " << preparation.error_message << "\n";
            ++failures;
            continue;
        }

        // What will be sent: the profile's review passes, or the whole profile in
        // one request under --single-request or when it publishes none.
        std::vector<review::SccgReviewPassRequest> passes = preparation.passes;
        if (options.single_request || passes.empty()) {
            review::SccgReviewPassRequest whole;
            whole.guideline_ids = preparation.guideline_ids;
            whole.request = preparation.request;
            passes = {std::move(whole)};
        }

        // `user_prompt` is every pass's prompt under its separator, so one hash
        // covers everything sent and the prompt-stripping tool still finds it.
        const std::string combined_prompt =
            passes.size() > 1 ? review::CombinePassPrompts(passes) : passes.front().request.prompt;
        json pass_prompts = json::array();
        std::size_t prompt_bytes = 0;
        for (const review::SccgReviewPassRequest& pass : passes) {
            prompt_bytes += pass.request.prompt.size();
            pass_prompts.push_back(json{{"pass_id", pass.pass_id},
                                        {"guideline_ids", pass.guideline_ids},
                                        {"user_prompt_sha256", core::Sha256::HexDigest(pass.request.prompt)},
                                        {"user_prompt_bytes", pass.request.prompt.size()}});
        }
        record["review_passes"] = passes.size() > 1 ? static_cast<int>(passes.size()) : 0;
        // `user_prompt_bytes` is the size of the string `user_prompt_sha256`
        // hashes, separators included, so the two describe one text. What the
        // provider is sent is the passes' own prompts, which sum to
        // `passes_prompt_bytes`.
        record["prompt"] = json{{"system_instruction", passes.front().request.systemInstruction},
                                {"user_prompt", combined_prompt},
                                {"user_prompt_sha256", core::Sha256::HexDigest(combined_prompt)},
                                {"user_prompt_bytes", combined_prompt.size()},
                                {"passes_prompt_bytes", prompt_bytes},
                                {"passes", pass_prompts}};

        std::vector<review::AiReviewParseResult> run_results;
        const int last_run = eval::LastRunNumber(options);
        for (int run = options.first_run; run <= last_run; ++run) {
            json run_record = record;
            run_record["run"] = run;
            run_record["runs_requested"] = options.runs;
            run_record["started_utc"] = NowUtcIso();

            if (options.dry_run) {
                run_record["outcome"] = "dry-run";
                run_record["model"] = ModelJson(settings, options);
            } else {
                // Concurrently, as the application sends them.
                const auto started = std::chrono::steady_clock::now();
                std::vector<std::future<GenerateResult>> pending;
                // Every later run of this element repeats its data, so a sweep
                // caches that segment too; the application does not.
                const bool cache_element_data = options.runs > 1;
                for (const review::SccgReviewPassRequest& pass : passes) {
                    const ai::AiRequest request =
                        ReviewRequest(pass.request, !options.no_prompt_cache, cache_element_data);
                    pending.push_back(std::async(std::launch::async, [service, request, settings]() {
                        const auto pass_started = std::chrono::steady_clock::now();
                        GenerateResult result = GenerateWithRetry(*service, request, settings);
                        const auto pass_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - pass_started);
                        result.elapsed_ms = static_cast<long long>(pass_elapsed.count());
                        return result;
                    }));
                }

                std::vector<review::ReviewPassOutcome> outcomes;
                json pass_records = json::array();
                ai::AiUsage run_usage;
                std::size_t passes_reporting_usage = 0;
                for (std::size_t index = 0; index < passes.size(); ++index) {
                    GenerateResult generated = pending[index].get();
                    ai::AiResponse& response = generated.response;
                    const long long pass_elapsed_ms = generated.elapsed_ms;
                    AddUsage(run_usage, response.usage);
                    if (response.usage.reported)
                        ++passes_reporting_usage;
                    review::ReviewPassOutcome outcome;
                    outcome.pass_id = passes[index].pass_id;
                    json pass_record{{"pass_id", passes[index].pass_id},
                                     {"elapsed_ms", pass_elapsed_ms},
                                     {"http_status", response.httpStatus},
                                     {"attempts", generated.attempts},
                                     {"service_tier", response.serviceTier},
                                     {"usage", UsageJson(response.usage)}};
                    if (!response.success) {
                        outcome.error =
                            response.errorMessage.empty() ? ai::ToString(response.errorCode) : response.errorMessage;
                        // The provider's own words: without them a rejected
                        // parameter is indistinguishable from an expired key.
                        pass_record["outcome"] = "request-failed";
                        pass_record["error"] = outcome.error;
                        pass_record["error_code"] = ai::ToString(response.errorCode);
                        pass_record["provider_error_code"] = response.providerErrorCode;
                        pass_record["raw_response"] = response.rawJson;
                    } else {
                        outcome.raw_response = response.text.empty() ? response.rawJson : response.text;
                        outcome.result = review::ParseAiReviewResponse(
                            outcome.raw_response, preparation.element_id, passes[index].guideline_ids);
                        pass_record["raw_response"] = outcome.raw_response;
                        if (!outcome.result.errorMessage.empty()) {
                            pass_record["outcome"] = "parse-failed";
                            pass_record["error"] = outcome.result.errorMessage;
                        } else {
                            pass_record["outcome"] = "ok";
                            pass_record["findings"] = FindingsJson(outcome.result);
                        }
                    }
                    pass_records.push_back(std::move(pass_record));
                    outcomes.push_back(std::move(outcome));
                }
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);

                const review::MergedReviewPasses merged =
                    review::MergeReviewPasses(outcomes, passes, preparation.element_id);
                // A pass that parsed but the merge failed -- one that reviewed a
                // different element -- must not still read "ok" in its record.
                for (json& pass_record : pass_records) {
                    const std::string pass_id = pass_record.value("pass_id", std::string{});
                    const bool failed_by_merge =
                        std::find(merged.failed_pass_ids.begin(), merged.failed_pass_ids.end(), pass_id) !=
                        merged.failed_pass_ids.end();
                    if (failed_by_merge && pass_record.value("outcome", std::string{}) == "ok")
                        pass_record["outcome"] = "rejected";
                }

                run_record["model"] = ModelJson(settings, options);
                run_record["elapsed_ms"] = elapsed.count();
                run_record["usage"] = RunUsageJson(run_usage, passes_reporting_usage, passes.size());
                run_record["passes"] = pass_records;
                run_record["discarded_findings"] = merged.discarded_findings;
                if (passes.size() == 1)
                    run_record["raw_response"] = outcomes.front().raw_response;

                if (!merged.any_succeeded()) {
                    // Every pass failed. A single request keeps the outcome
                    // names the records have always used.
                    const std::string single_outcome =
                        passes.size() == 1 ? pass_records.front().value("outcome", "request-failed") : "request-failed";
                    run_record["outcome"] = single_outcome;
                    run_record["error"] = merged.merged.errorMessage;
                    if (passes.size() == 1 && pass_records.front().contains("raw_response"))
                        run_record["raw_response"] = pass_records.front()["raw_response"];
                    review::AiReviewParseResult failed;
                    failed.errorMessage = merged.merged.errorMessage;
                    run_results.push_back(failed);
                    ++failures;
                } else if (!merged.complete()) {
                    // Some passes ran, some did not. Reported, and kept out of
                    // any consensus: a run missing a pass would count every
                    // guideline in it as "not cited", which it was not -- it was
                    // not asked.
                    run_record["outcome"] = "incomplete";
                    run_record["error"] = merged.pass_errors;
                    run_record["findings"] = FindingsJson(merged.merged);
                    review::AiReviewParseResult incomplete;
                    incomplete.errorMessage =
                        "incomplete: " + std::to_string(merged.failed_pass_ids.size()) + " pass(es) failed";
                    run_results.push_back(incomplete);
                    ++failures;
                } else {
                    run_record["outcome"] = "ok";
                    run_record["findings"] = FindingsJson(merged.merged);
                    run_record["suggested_element_texts"] = merged.merged.suggestedElementTexts;
                    run_record["rejected_operation_reasons"] = merged.merged.rejectedOperationReasons;
                    run_results.push_back(merged.merged);
                }
            }

            run_record["finished_utc"] = NowUtcIso();
            const std::string file_name = SanitizeForFileName(element_id) + "--run" + std::to_string(run) + ".json";
            const std::filesystem::path path = options.out_dir / file_name;
            if (WriteRecord(path, run_record))
                ++records;
            else
                ++failures;

            std::cout << element_id << " [" << preparation.review_profile_id << "] run " << run << "/" << last_run
                      << ": " << run_record.value("outcome", "") << " -> " << path.string() << "\n";
        }

        if (options.consensus_minimum > 0 && !run_results.empty()) {
            const review::ConsensusReviewResult consensus = review::BuildConsensusReview(
                run_results, options.runs, options.consensus_minimum, preparation.precheck_results);

            json consensus_record = record;
            consensus_record["outcome"] = "consensus";
            // Which runs this consensus is over. With --first-run a directory
            // can hold runs from several invocations, and the counts alone do
            // not say which of them were counted.
            consensus_record["first_run"] = options.first_run;
            consensus_record["last_run"] = last_run;
            consensus_record["runs_requested"] = consensus.runs_requested;
            consensus_record["runs_succeeded"] = consensus.runs_succeeded;
            consensus_record["consensus_minimum"] = options.consensus_minimum;
            consensus_record["run_errors"] = consensus.run_errors;
            consensus_record["findings"] = ConsensusFindingsJson(consensus.findings);
            consensus_record["below_threshold"] = ConsensusFindingsJson(consensus.below_threshold);
            consensus_record["model"] = ModelJson(settings, options);
            consensus_record["finished_utc"] = NowUtcIso();

            const std::filesystem::path path = options.out_dir / (SanitizeForFileName(element_id) + "--consensus.json");
            if (WriteRecord(path, consensus_record))
                ++records;
            else
                ++failures;

            int unanimous = 0;
            for (const review::ConsensusFinding& finding : consensus.findings) {
                if (finding.unanimous())
                    ++unanimous;
            }
            std::cout << element_id << " [" << preparation.review_profile_id
                      << "] consensus: " << consensus.findings.size()
                      << " finding(s) at >=" << options.consensus_minimum << "/" << consensus.runs_succeeded << " ("
                      << unanimous << " unanimous), " << consensus.below_threshold.size() << " below -> "
                      << path.string() << "\n";
        }
    }

    std::cout << "\n" << records << " record(s) written to " << options.out_dir.string() << "\n";
    if (failures > 0)
        std::cout << failures << " failure(s).\n";
    return failures > 0 ? 1 : 0;
}
