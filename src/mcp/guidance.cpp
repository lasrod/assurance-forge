#include "mcp/guidance.h"

#include "core/guideline_catalog.h"

#include <sstream>
#include <string>

namespace mcp {
namespace {

constexpr const char* kAllGuidelines = "sccg://guidelines";
constexpr const char* kGuidelinePrefix = "sccg://guideline/";

// Loaded once. The catalog is a few thousand lines of YAML and does not change
// while the process runs.
const core::GuidelineCatalog* Catalog(std::string& error) {
    static core::GuidelineCatalog catalog;
    static bool loaded = false;
    static std::string load_error;
    static bool attempted = false;

    if (!attempted) {
        attempted = true;
        loaded = core::LoadGuidelineCatalog(catalog, load_error);
    }
    if (!loaded) {
        error = load_error.empty() ? "The SCCG catalog could not be loaded." : load_error;
        return nullptr;
    }
    return &catalog;
}

void AppendGuideline(std::ostringstream& out, const parser::Guideline& guideline) {
    out << "## " << guideline.id << " " << guideline.title << "\n\n";
    out << guideline.statement << "\n\n";
    if (!guideline.rationale.empty()) {
        out << "Why: " << guideline.rationale << "\n\n";
    }
    if (!guideline.examples.bad.empty()) {
        out << "Weak: " << guideline.examples.bad << "\n";
        if (!guideline.examples.problem.empty()) {
            out << "Problem: " << guideline.examples.problem << "\n";
        }
        if (!guideline.examples.good.empty()) {
            out << "Better: " << guideline.examples.good << "\n";
        }
        out << "\n";
    }
    if (!guideline.review_prompts.empty()) {
        out << "Ask:\n";
        for (const std::string& prompt : guideline.review_prompts) {
            out << "- " << prompt << "\n";
        }
        out << "\n";
    }
}

// The guidance that belongs with a particular job. Quoting the catalog rather
// than paraphrasing it keeps the prompt and the published guideline the same
// text, so one cannot drift from the other.
std::string GuidelinesFor(const std::vector<std::string>& ids) {
    std::string error;
    const core::GuidelineCatalog* catalog = Catalog(error);
    if (catalog == nullptr) {
        return "(The SCCG catalog is unavailable: " + error + ")";
    }

    std::ostringstream out;
    for (const std::string& id : ids) {
        const parser::Guideline* guideline = catalog->document.FindGuidelineById(id);
        if (guideline != nullptr) {
            AppendGuideline(out, *guideline);
        }
    }
    return out.str();
}

std::string Argument(const nlohmann::json& arguments, const char* key, const std::string& fallback) {
    const nlohmann::json::const_iterator found = arguments.find(key);
    if (found == arguments.end() || !found->is_string() || found->get<std::string>().empty()) {
        return fallback;
    }
    return found->get<std::string>();
}

// Repeated in every prompt because it is the thing an agent most needs to know
// and most easily forgets: it is not editing the safety case.
constexpr const char* kWorkflow =
    "How to work here:\n"
    "1. Read before writing. `get_case_overview`, `get_argument_tree` and `find_elements` show "
    "what already exists in the integrated working draft and report its `working_revision`; do not "
    "duplicate or contradict it.\n"
    "2. `begin_change_group` with a clear title, rationale and the revision you read. From that "
    "moment the user watches your contribution appear on their canvas.\n"
    "3. `stage_operations` in small steps, always carrying the latest revision. Each call returns "
    "stable ids, the next revision and findings against the combined draft -- act on those before "
    "continuing. If a revision is stale, reread because a user, SCCG review or another client changed "
    "the shared draft.\n"
    "4. Use `replace_change_group` when feedback changes the proposal, rather than layering a "
    "contradictory second version over it.\n"
    "5. `submit_change_group` when finished, then tell the user it is waiting for them in Assurance "
    "Forge.\n\n"
    "Terminology: `list_terms` shows the case's defined terms with their definitions. When a claim "
    "leans on a broad or evaluative term (CL.5), prefer defining it once with a `CreateTerm` "
    "operation over restating a bound in every claim that uses it.\n\n"
    "You are not editing the accepted safety case. Nothing you stage takes effect until a person promotes "
    "it. Never tell the user you have changed their argument.\n";

// A case maintained in two languages is read by two sets of reviewers, and an
// element added in only one of them is invisible to one of those sets. Which
// languages a case actually uses is a property of the case, so this says how to
// find out rather than naming any.
constexpr const char* kLanguages =
    "\nLanguages: reads report `translated_languages` on each element and the text itself under "
    "`translations`. If the case is maintained in more than one language, write every new element in "
    "all of them -- put the English in `text`/`new_value` and the rest in the same operation's "
    "`translations` map, so a reviewer never sees a claim that exists in only one language. Do not "
    "translate by transliterating terms: reuse the wording the existing translated claims already "
    "use for the same concept. Say plainly which languages you wrote, and that a human still has to "
    "check the translation.\n";

} // namespace

const std::vector<ResourceDefinition>& BuiltinResources() {
    static const std::vector<ResourceDefinition> resources{
        ResourceDefinition{kAllGuidelines,
                           "Safety Case Core Guidelines",
                           "The full SCCG catalog: the claim, argument, evidence, sufficiency and "
                           "fallacy rules this project's reviews are held to. Read this before "
                           "proposing argument structure. One guideline at a time is available as "
                           "sccg://guideline/<id>, e.g. sccg://guideline/CL.1.",
                           "text/markdown"},
    };
    return resources;
}

const std::vector<ResourceTemplateDefinition>& BuiltinResourceTemplates() {
    static const std::vector<ResourceTemplateDefinition> templates{
        ResourceTemplateDefinition{"sccg://guideline/{id}",
                                   "One SCCG guideline",
                                   "A single guideline by id (e.g. CL.1, AR.2, EV.1): its statement, "
                                   "rationale, examples and review prompts. Cheaper to re-read while "
                                   "working than the whole catalog.",
                                   "text/markdown"},
    };
    return templates;
}

const std::vector<PromptDefinition>& BuiltinPrompts() {
    static const std::vector<PromptDefinition> prompts{
        PromptDefinition{
            "draft_argument_from_standard",
            "Draft safety-case argument structure that applies a named standard, following SCCG.",
            {PromptArgument{"standard", "The standard or standards to apply, e.g. \"ISO 26262 part 6\".", true},
             PromptArgument{"scope", "What the argument should cover.", false}}},
        PromptDefinition{"add_argumentation",
                         "Add argument about a topic where it best fits in the existing structure.",
                         {PromptArgument{"topic", "What the new argument should establish.", true}}},
        PromptDefinition{"restructure_case",
                         "Reorganize the argument so named categories become its main branches.",
                         {PromptArgument{"categories", "The top-level branches to organize under.", true}}},
        PromptDefinition{"translate_case",
                         "Add a second language to the existing argument, leaving what it asserts unchanged.",
                         {PromptArgument{"language", "The language code to translate into, e.g. \"ja\".", true},
                          PromptArgument{"scope", "Which branch to translate. Defaults to the whole case.", false}}},
    };
    return prompts;
}

std::string ReadResource(const std::string& uri, bool& found, std::string& error) {
    found = false;
    error.clear();

    std::string load_error;
    const core::GuidelineCatalog* catalog = Catalog(load_error);

    if (uri == kAllGuidelines) {
        found = true;
        if (catalog == nullptr) {
            error = load_error;
            return {};
        }
        // The dist loader -- the path every real build takes -- carries no
        // document metadata; only the YAML fallback does. Without these
        // fallbacks the resource opened with an empty heading and a blank
        // line, which is a poor first impression for the text that exists to
        // establish authority.
        const std::string& title = catalog->document.metadata.title;
        const std::string& purpose = catalog->document.metadata.purpose;
        std::ostringstream out;
        out << "# " << (title.empty() ? "Safety Case Core Guidelines (SCCG)" : title) << "\n\n";
        if (!purpose.empty()) {
            out << purpose << "\n\n";
        }
        for (const parser::Guideline& guideline : catalog->document.guidelines) {
            AppendGuideline(out, guideline);
        }
        return out.str();
    }

    if (uri.rfind(kGuidelinePrefix, 0) == 0) {
        found = true;
        const std::string id = uri.substr(std::string(kGuidelinePrefix).size());
        if (catalog == nullptr) {
            error = load_error;
            return {};
        }
        const parser::Guideline* guideline = catalog->document.FindGuidelineById(id);
        if (guideline == nullptr) {
            error = "No SCCG guideline has the id \"" + id + "\".";
            return {};
        }
        std::ostringstream out;
        AppendGuideline(out, *guideline);
        return out.str();
    }

    return {};
}

std::string BuildPrompt(const std::string& name, const nlohmann::json& arguments) {
    std::ostringstream out;

    if (name == "draft_argument_from_standard") {
        const std::string standard = Argument(arguments, "standard", "the applicable standard");
        const std::string scope = Argument(arguments, "scope", "the system this project covers");
        out << "Draft assurance argument structure for " << scope << ", applying " << standard << ".\n\n"
            << "Work top-down: state what the standard requires to be shown, make each requirement "
               "a claim that could in principle be falsified, and give each one a strategy saying "
               "how it will be argued. Leave leaves undeveloped rather than inventing evidence "
               "that does not exist -- an undeveloped goal is an honest statement of work "
               "remaining, and fabricated evidence is not.\n\n"
            << "Record which clause of " << standard
            << " each claim answers, in the claim's description. Assurance Forge has no dedicated "
               "citation field yet, so that text is the trace.\n\n"
            << kWorkflow << kLanguages << "\n"
            << "Follow these guidelines, which are what this project's reviews apply:\n\n"
            << GuidelinesFor({"CL.1", "CL.2", "CL.5", "CL.6", "AR.1", "AR.2", "AR.4", "EV.1"});
        return out.str();
    }

    if (name == "add_argumentation") {
        const std::string topic = Argument(arguments, "topic", "the topic the user named");
        out << "Add argument establishing " << topic << " to this safety case.\n\n"
            << "Find where it belongs before writing anything. Read the argument tree, look for "
               "claims already covering nearby ground, and attach to the branch whose scope this "
               "extends -- do not open a new top-level branch unless nothing existing fits, and say "
               "so if that is your conclusion. Keep the vocabulary of the branch you join; a "
               "sub-claim that redefines its parent's terms is a break in the argument, not an "
               "addition to it.\n\n"
            << kWorkflow << kLanguages << "\n"
            << "Follow these guidelines:\n\n"
            << GuidelinesFor({"CL.1", "CL.5", "AR.2", "AR.5", "AR.6", "EV.1"});
        return out.str();
    }

    if (name == "restructure_case") {
        const std::string categories = Argument(arguments, "categories", "the categories the user named");
        out << "Reorganize this safety case so " << categories << " become its main branches.\n\n"
            << "Restructuring moves existing argument; it does not rewrite it. Preserve each "
               "claim's wording and its evidence unless the user asked otherwise -- a restructure "
               "that quietly reworded claims would change what the case asserts while appearing to "
               "be a tidy-up.\n\n"
            << "Re-parent by removing the old support relationship and adding the new one. Report "
               "anything that does not fit the proposed categories rather than forcing it: a claim "
               "with no home is a finding about the proposed structure, and the user needs to know "
               "before they accept it.\n\n"
            << "Stage this in small groups and check the canvas preview as you go. A restructure "
               "touches many elements at once, and one that goes wrong is hard to read as a single "
               "diff.\n\n"
            << kWorkflow << kLanguages << "\n"
            << "Follow these guidelines:\n\n"
            << GuidelinesFor({"AR.1", "AR.2", "AR.4", "AR.5", "CL.2"});
        return out.str();
    }

    if (name == "translate_case") {
        const std::string language = Argument(arguments, "language", "the language the user named");
        const std::string scope = Argument(arguments, "scope", "the whole case");
        out << "Translate " << scope << " into " << language << ".\n\n"
            << "Translating a safety case is not translating prose. A claim states something that "
               "can be judged true or false, and the translation has to be judged the same way: "
               "keep its scope, its qualifiers and its hedging exactly. If the English says "
               "\"under normal operating conditions\", the translation says so too -- dropping a "
               "qualifier makes the claim broader than the one the evidence supports.\n\n"
            << "Do not change the English. Stage `UpdateElementText` operations carrying only a "
               "`translations` entry for "
            << language
            << ", which revises that language and leaves the primary text alone. Reuse the "
               "terminology already established in the case rather than inventing a second rendering "
               "of a term that has one.\n\n"
            << "Read each element before translating it. `translated_languages` tells you which "
               "already have this language, and `translations` gives you the wording a human "
               "translator chose -- do not replace it unless the English it renders has changed, "
               "and say which ones you replaced.\n\n"
            << "Leave anything you are unsure of untranslated and list it. An untranslated claim is "
               "a visible gap; a confidently wrong translation of a safety claim is not.\n\n"
            << kWorkflow << kLanguages
            << "\n"
            // CL.5 is the qualifier rule the paragraph above states informally;
            // quoting it makes the prompt and the review apply the same text.
            // The other guidelines govern writing argument, and a translation
            // that needs them has stopped being a translation.
            << "The review this case is held to applies this guideline to every claim, translated "
               "text included:\n\n"
            << GuidelinesFor({"CL.5"});
        return out.str();
    }

    return {};
}

} // namespace mcp
