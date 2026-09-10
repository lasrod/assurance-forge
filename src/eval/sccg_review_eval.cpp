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
#include "review/sccg/sccg_review.h"
#include "review/sccg/sccg_review_consensus.h"
#include "review/sccg/sccg_review_preparation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using nlohmann::json;

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
    std::string tag;
};

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
  --out <dir>          Directory for the run records. Default: ./sccg-eval-out
  --tag <text>         Free text stored in every record of this invocation.
  --consensus <m>      After the runs, write a consensus record per element: findings grouped
                       by guideline with the number of runs citing each. m is the floor for
                       the main list; findings below it are kept separately, not dropped.
                       Needs --runs > 1 to mean anything.
  --dry-run            Assemble and record the request; do not call the provider.
  --list-models        List the models the configured account offers, newest first, then exit.
  --help
)";
}

bool ParseArgs(int argc, char** argv, Options& options, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                error = std::string(name) + " needs a value.";
                return {};
            }
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") {
            PrintUsage();
            std::exit(0);
        } else if (arg == "--project") {
            options.project = value("--project");
        } else if (arg == "--element") {
            options.element_ids.push_back(value("--element"));
        } else if (arg == "--profile") {
            options.review_profile_id = value("--profile");
        } else if (arg == "--model") {
            options.model = value("--model");
        } else if (arg == "--temperature") {
            options.temperature = std::atof(value("--temperature").c_str());
        } else if (arg == "--seed") {
            options.seed = std::atoll(value("--seed").c_str());
        } else if (arg == "--runs") {
            options.runs = std::atoi(value("--runs").c_str());
        } else if (arg == "--out") {
            options.out_dir = value("--out");
        } else if (arg == "--tag") {
            options.tag = value("--tag");
        } else if (arg == "--consensus") {
            options.consensus_minimum = std::atoi(value("--consensus").c_str());
        } else if (arg == "--dry-run") {
            options.dry_run = true;
        } else if (arg == "--list-models") {
            options.list_models = true;
        } else {
            error = "Unknown argument: " + arg;
            return false;
        }
        if (!error.empty())
            return false;
    }
    options.all_elements = options.element_ids.empty();
    if (options.runs < 1)
        options.runs = 1;
    if (options.out_dir.empty())
        options.out_dir = std::filesystem::path("sccg-eval-out");
    return true;
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
            {"severity", SeverityName(problem.severity)},
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

} // namespace

int main(int argc, char** argv) {
    Options options;
    std::string error;
    if (!ParseArgs(argc, argv, options, error)) {
        std::cerr << error << "\n\n";
        PrintUsage();
        return 2;
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
    if (!core::LoadGuidelineCatalog(catalog, error)) {
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

    std::filesystem::create_directories(options.out_dir);
    const std::string session_started = NowUtcIso();

    int failures = 0;
    int records = 0;
    for (const std::string& element_id : element_ids) {
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
            std::ofstream(path) << record.dump(2);
            std::cerr << element_id << ": preparation failed: " << preparation.error_message << "\n";
            ++failures;
            continue;
        }

        record["prompt"] = json{{"system_instruction", preparation.request.systemInstruction},
                                {"user_prompt", preparation.request.prompt},
                                {"user_prompt_sha256", core::Sha256::HexDigest(preparation.request.prompt)},
                                {"user_prompt_bytes", preparation.request.prompt.size()}};

        std::vector<review::AiReviewParseResult> run_results;
        for (int run = 1; run <= options.runs; ++run) {
            json run_record = record;
            run_record["run"] = run;
            run_record["runs_requested"] = options.runs;
            run_record["started_utc"] = NowUtcIso();

            if (options.dry_run) {
                run_record["outcome"] = "dry-run";
                run_record["model"] = json{
                    {"name", settings.model},
                    {"provider", ai::ToString(settings.provider)},
                    {"temperature", settings.temperature.has_value() ? json(*settings.temperature) : json(nullptr)},
                    {"seed", settings.seed.has_value() ? json(*settings.seed) : json(nullptr)}};
            } else {
                ai::AiRequest request;
                request.systemInstruction = preparation.request.systemInstruction;
                request.userPrompt = preparation.request.prompt;

                const auto started = std::chrono::steady_clock::now();
                const ai::AiResponse response = service->Generate(request, settings);
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);

                run_record["model"] = json{
                    {"name", settings.model},
                    {"provider", ai::ToString(settings.provider)},
                    {"temperature", settings.temperature.has_value() ? json(*settings.temperature) : json(nullptr)},
                    {"seed", settings.seed.has_value() ? json(*settings.seed) : json(nullptr)}};
                run_record["elapsed_ms"] = elapsed.count();
                run_record["http_status"] = response.httpStatus;

                if (!response.success) {
                    run_record["outcome"] = "request-failed";
                    run_record["error"] =
                        response.errorMessage.empty() ? ai::ToString(response.errorCode) : response.errorMessage;
                    run_record["error_code"] = ai::ToString(response.errorCode);
                    // The provider's own words. Without them a failed run says
                    // only that it failed, which is the one thing already known
                    // -- and a rejected parameter is indistinguishable from an
                    // expired key.
                    run_record["raw_response"] = response.rawJson;
                    ++failures;
                } else {
                    const std::string raw = response.text.empty() ? response.rawJson : response.text;
                    run_record["raw_response"] = raw;
                    const review::AiReviewParseResult parsed =
                        review::ParseAiReviewResponse(raw, preparation.element_id, preparation.guideline_ids);
                    run_results.push_back(parsed);
                    if (!parsed.errorMessage.empty()) {
                        run_record["outcome"] = "parse-failed";
                        run_record["error"] = parsed.errorMessage;
                        ++failures;
                    } else {
                        run_record["outcome"] = "ok";
                        run_record["findings"] = FindingsJson(parsed);
                        run_record["suggested_element_texts"] = parsed.suggestedElementTexts;
                        run_record["rejected_operation_reasons"] = parsed.rejectedOperationReasons;
                    }
                }
            }

            run_record["finished_utc"] = NowUtcIso();
            const std::string file_name = SanitizeForFileName(element_id) + "--run" + std::to_string(run) + ".json";
            const std::filesystem::path path = options.out_dir / file_name;
            std::ofstream(path) << run_record.dump(2);
            ++records;

            std::cout << element_id << " [" << preparation.review_profile_id << "] run " << run << "/" << options.runs
                      << ": " << run_record.value("outcome", "") << " -> " << path.string() << "\n";
        }

        if (options.consensus_minimum > 0 && !run_results.empty()) {
            const review::ConsensusReviewResult consensus = review::BuildConsensusReview(
                run_results, options.runs, options.consensus_minimum, preparation.precheck_results);

            json consensus_record = record;
            consensus_record["outcome"] = "consensus";
            consensus_record["runs_requested"] = consensus.runs_requested;
            consensus_record["runs_succeeded"] = consensus.runs_succeeded;
            consensus_record["consensus_minimum"] = options.consensus_minimum;
            consensus_record["run_errors"] = consensus.run_errors;
            consensus_record["findings"] = ConsensusFindingsJson(consensus.findings);
            consensus_record["below_threshold"] = ConsensusFindingsJson(consensus.below_threshold);
            consensus_record["model"] =
                json{{"name", settings.model},
                     {"provider", ai::ToString(settings.provider)},
                     {"temperature", settings.temperature.has_value() ? json(*settings.temperature) : json(nullptr)},
                     {"seed", settings.seed.has_value() ? json(*settings.seed) : json(nullptr)}};
            consensus_record["finished_utc"] = NowUtcIso();

            const std::filesystem::path path = options.out_dir / (SanitizeForFileName(element_id) + "--consensus.json");
            std::ofstream(path) << consensus_record.dump(2);
            ++records;

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
