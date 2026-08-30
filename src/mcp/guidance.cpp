#include "mcp/guidance.h"

#include "core/guideline_catalog.h"

#include <algorithm>
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

// The guidelines a review of this output will actually apply.
//
// These sets used to be written by hand, which made the criteria an agent was
// given when it *wrote* a claim a different, separately maintained set from the
// ones applied when the same claim was *reviewed*. An agent could satisfy the
// prompt and still fail the review, and the two would drift further apart every
// time SCCG revised a profile, with nothing to notice.
//
// Naming the profiles instead means the catalog decides. A guideline added to
// `claim_review` upstream reaches the agent that writes claims, in the same
// release, without anybody remembering to copy an id.
std::vector<std::string> GuidelineIdsForProfiles(const core::GuidelineCatalog& catalog,
                                                 const std::vector<std::string>& profile_ids) {
    std::vector<std::string> ids;
    for (const std::string& profile_id : profile_ids) {
        const parser::ReviewProfile* profile = catalog.document.FindReviewProfileById(profile_id);
        if (profile == nullptr) {
            continue;
        }
        for (const std::string& guideline_id : profile->guideline_ids) {
            if (std::find(ids.begin(), ids.end(), guideline_id) == ids.end()) {
                ids.push_back(guideline_id);
            }
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::string GuidelinesForProfiles(const std::vector<std::string>& profile_ids) {
    std::string error;
    const core::GuidelineCatalog* catalog = Catalog(error);
    if (catalog == nullptr) {
        return "(The SCCG catalog is unavailable: " + error + ")";
    }
    const std::vector<std::string> ids = GuidelineIdsForProfiles(*catalog, profile_ids);
    if (ids.empty()) {
        return "(No SCCG review profile in this catalog matched: the guidelines below could not be resolved.)";
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

// A named set, for the one job whose guidance is deliberately narrower than any
// review profile. Quoting the catalog rather than paraphrasing it keeps the
// prompt and the published guideline the same text.
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
    "the shared draft. To polish unfinished operations without drawing them on the user's canvas, "
    "rehearse them with `check_operations` first; submitting is refused while problem-severity "
    "findings stand, so fix them as you go rather than at the end.\n"
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

// The doctrine's lines were maintained here by hand, including the property
// this comment used to assert on its own authority -- that every SCCG family is
// represented. SCCG 0.7.0 publishes the same thing as `authoring_guidance`: the
// subset a tool should deliver while an author is *writing* rather than when a
// review is run, each rule with a one-line `short_rule`, a recorded reason for
// inclusion, and every family covered. Rendering that file is what removes the
// drift, and the file's own `usage` says how: render `short_rule`, cite the id,
// do not paraphrase, and do not present the subset as conformance.
//
// One trailing full stop is trimmed so the id can carry it. That is punctuation,
// not paraphrase.
std::string RenderDoctrineRule(const std::string& short_rule) {
    if (!short_rule.empty() && short_rule.back() == '.') {
        return short_rule.substr(0, short_rule.size() - 1);
    }
    return short_rule;
}

const parser::AuthoringGuidance* AuthoringGuidance() {
    std::string error;
    const core::GuidelineCatalog* catalog = Catalog(error);
    if (catalog == nullptr || catalog->document.authoring_guidance.core_rules.empty()) {
        return nullptr;
    }
    return &catalog->document.authoring_guidance;
}

} // namespace

const std::string& AuthoringDoctrine() {
    static const std::string doctrine = [] {
        const parser::AuthoringGuidance* guidance = AuthoringGuidance();
        if (guidance == nullptr) {
            return std::string("This project's argument is written to the Safety Case Core Guidelines (SCCG). "
                               "The catalog's authoring subset could not be read here, so read the rules "
                               "directly: the full catalog is the resource sccg://guidelines, and one rule is "
                               "sccg://guideline/<id>.");
        }
        std::ostringstream out;
        out << "Writing assurance argument here follows the Safety Case Core Guidelines (SCCG):\n";
        for (const parser::AuthoringCoreRule& rule : guidance->core_rules) {
            out << "- " << RenderDoctrineRule(rule.short_rule) << " (" << rule.id << ").\n";
        }
        // SCCG's own caveat, carried so the subset cannot be read as the whole
        // standard by an agent that reads nothing else.
        out << "These are the rules most often broken while writing, not a reduced standard -- every SCCG "
               "guideline still applies. The full catalog is the resource sccg://guidelines; one rule is "
               "sccg://guideline/<id>.";
        return out.str();
    }();
    return doctrine;
}

const std::vector<std::string>& AuthoringDoctrineGuidelineIds() {
    static const std::vector<std::string> ids = [] {
        std::vector<std::string> collected;
        if (const parser::AuthoringGuidance* guidance = AuthoringGuidance()) {
            for (const parser::AuthoringCoreRule& rule : guidance->core_rules) {
                collected.push_back(rule.id);
            }
        }
        return collected;
    }();
    return ids;
}

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
        // Every tool-facing file has carried a `document` block since SCCG
        // 0.7.0, so the dist loader -- the path every real build takes --
        // supplies the same title and purpose the YAML fallback always did, and
        // the hardcoded title this used to substitute is gone. The version is
        // stated with them so a finding can cite what it was produced under.
        const parser::GuidelinesDocument& document = catalog->document;
        std::ostringstream out;
        out << "# " << document.metadata.title << "\n\n";
        if (!document.sccg_version.empty()) {
            out << "SCCG " << document.sccg_version;
            if (!document.metadata.license.id.empty()) {
                out << " -- " << document.metadata.license.id;
            }
            out << "\n\n";
        }
        if (!document.metadata.purpose.empty()) {
            out << document.metadata.purpose << "\n\n";
        }
        for (const parser::Guideline& guideline : document.guidelines) {
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

// What each prompt actually produces, in SCCG's element-role vocabulary. This
// much is our editorial judgement -- drafting from a standard writes claims,
// reasoning, scope and evidence stubs; restructuring only moves claims and the
// reasoning between them -- and it is the only part left in code. Which profile
// judges each role is SCCG's, read from `authoring_guidance.element_rules`, so
// a profile renamed or re-scoped upstream reaches these prompts without anyone
// copying an id.
std::vector<std::string> ElementRolesForPrompt(const std::string& name) {
    if (name == "draft_argument_from_standard") {
        return {"claim", "strategy", "assumption", "context", "evidence", "justification"};
    }
    if (name == "add_argumentation") {
        return {"claim", "strategy", "context"};
    }
    if (name == "restructure_case") {
        return {"claim", "strategy"};
    }
    // `translate_case` deliberately carries one guideline, not a profile: a
    // translation that needed the rest has stopped being a translation.
    return {};
}

std::vector<std::string> ReviewProfilesForPrompt(const std::string& name) {
    const std::vector<std::string> element_roles = ElementRolesForPrompt(name);
    if (element_roles.empty()) {
        return {};
    }
    std::string error;
    const core::GuidelineCatalog* catalog = Catalog(error);
    if (catalog == nullptr) {
        return {};
    }

    std::vector<std::string> profile_ids;
    for (const std::string& element_role : element_roles) {
        const parser::AuthoringElementRule* rule = catalog->document.FindAuthoringElementRule(element_role);
        if (rule == nullptr || rule->review_profile_id.empty()) {
            continue;
        }
        if (std::find(profile_ids.begin(), profile_ids.end(), rule->review_profile_id) == profile_ids.end()) {
            profile_ids.push_back(rule->review_profile_id);
        }
    }
    return profile_ids;
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
            // CL.3, SU.2, LF.3 and RD.1 joined the set when the doctrine work
            // found the SU, LF and RD families quoted in no prompt at all --
            // drafting from a standard is exactly where assumptions, arguing
            // from absence, and role signposting go wrong.
            // Drafting a structure produces claims, strategies, assumptions,
            // context and evidence stubs, and each is reviewed under its own
            // profile. Naming them all is what makes the criteria the agent
            // writes to the criteria it will be judged by.
            << GuidelinesForProfiles(ReviewProfilesForPrompt(name));
        return out.str();
    }

    if (name == "add_argumentation") {
        const std::string topic = Argument(arguments, "topic", "the topic the user named");
        // CL.2, CL.3 and LF.1 joined with the doctrine work: new argument
        // added mid-case is where bundled claims and support that quietly
        // restates its parent most often appear.
        out << "Add argument establishing " << topic << " to this safety case.\n\n"
            << "Find where it belongs before writing anything. Read the argument tree, look for "
               "claims already covering nearby ground, and attach to the branch whose scope this "
               "extends -- do not open a new top-level branch unless nothing existing fits, and say "
               "so if that is your conclusion. Keep the vocabulary of the branch you join; a "
               "sub-claim that redefines its parent's terms is a break in the argument, not an "
               "addition to it.\n\n"
            << kWorkflow << kLanguages << "\n"
            << "Follow these guidelines:\n\n"
            // Adding argument writes claims and the reasoning that connects
            // them, and usually the context that bounds them.
            << GuidelinesForProfiles(ReviewProfilesForPrompt(name));
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
            // RD.1 joined with the doctrine work: moving argument is where an
            // element's wording and its new place most easily fall out of step.
            << GuidelinesForProfiles(ReviewProfilesForPrompt(name));
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
