#include "core/sccg/staged_checks.h"

#include "core/assurance_tree.h"
#include "core/guideline_catalog.h"
#include "core/problems/argument_cycles.h"

#include <algorithm>
#include <format>
#include <cctype>
#include <set>
#include <string>
#include <vector>

namespace core::sccg {
namespace {

// The guidelines these checks serve, with SCCG's own wording. Held here rather
// than read from the catalog so a finding is self-describing in a tool result,
// and so a check cannot silently outlive the guideline it claims to enforce --
// changing one means changing the other in the same place. A test holds each
// embedded statement against the loaded catalog, so the constants cannot
// quietly diverge from the guidelines they quote.
//
// `check_id` is the catalog's own name for the rule (`tool.suggested_checks`),
// carried on every finding: the stable key agents deduplicate on and the
// panels translate by.
struct Guideline {
    const char* id;
    const char* check_id;
    const char* statement;
};

constexpr Guideline kEvidencePath{
    "EV.1",
    "check-evidence-trace",
    "Show, for each claim, the evidence path that supports it, either directly or through "
    "stated sub-claims and intermediate arguments."};
// `check-explicit-strategy` is the catalog's check on a *decomposition*:
// "Check whether each decomposition has an explicit reasoning step stating how
// children support the parent." It therefore fires on the parent claim, not on
// the strategy -- AR.2's own bad example is a goal whose several children carry
// no stated rule. A check that instead reported a strategy developing into
// nothing while wearing this id would answer a different question under SCCG's
// name, and a clean result would read as SCCG's check having passed. That
// check still exists below; it cites AR.1, which is the guideline it serves.
constexpr Guideline kInferenceStep{"AR.2",
                                   "check-explicit-strategy",
                                   "State how the parent claim is being decomposed or argued; do not make the reviewer "
                                   "infer the decomposition rule from wording alone."};
constexpr Guideline kStructureCarriesArgument{
    "AR.1", "check-element-role-misuse", "Use the assurance case structure to make each element's role clear."};
// The cycle finding quotes AR.1 -- structure carrying the argument is what a
// cycle breaks -- but its catalog check id is LF.1's, because a support cycle
// IS the catalog's circular-reasoning check. When phase 2 of the authoring
// plan adds LF.1's textual near-duplication check, the attribution is
// revisited together with it.
constexpr Guideline kCircularSupport{
    "AR.1", "check-circular-support", "Use the assurance case structure to make each element's role clear."};
constexpr Guideline kBoundQualifiers{
    "CL.5",
    "check-bounded-qualifiers",
    "Do not leave broad evaluative terms or universal qualifiers unbounded. This includes "
    "terms such as safe, timely, effective, normal, robust, all, every, and never."};
constexpr Guideline kSingleProperty{"CL.2",
                                    "check-single-property",
                                    "Do not bundle multiple distinct properties, conclusions, or obligations into "
                                    "one goal."};
// CL.3 and CL.4 were both deferred for want of a published number and a
// published word list. SCCG 0.7.0 supplies each: `claim_word_count` under CL.3's
// `tool.thresholds`, and CL.4's `ambiguous_qualifier` and `hedging_adverb`
// markers.
constexpr Guideline kClaimLength{
    "CL.3",
    "check-claim-length-and-role-mixing",
    "Do not pack argument structure, scope, decomposition topics, and caveats into a single claim. Keep the "
    "claim short, and express the rest through context, strategy, assumptions, and sub-claims."};
constexpr Guideline kClaimAmbiguity{"CL.4",
                                    "check-claim-ambiguity",
                                    "Write claims so that their meaning is clear enough that competent reviewers "
                                    "are likely to understand them in the same way."};
// The catalog's CL.6 statement carries no trailing period; the drift test
// compares byte for byte, so neither does this quote.
constexpr Guideline kStepMixing{
    "CL.6",
    "check-claim-step-mixing",
    "Do not combine identification, adequacy, implementation, validation, and sufficiency in one "
    "claim unless the claim is intentionally kept at a higher level and the need for later "
    "decomposition is explicit"};
constexpr Guideline kSignposting{"RD.1",
                                 "check-element-signposting",
                                 "Make it clear whether a statement is functioning as a claim, evidence reference, "
                                 "context, assumption, or justification."};
constexpr Guideline kPromotionalLanguage{"RD.4",
                                         "check-promotional-language",
                                         "Do not use promotional, boastful, or image-enhancing language in the "
                                         "safety case."};
// The first sentence of EV.7's statement; the rest restates EV.8's rule.
constexpr Guideline kEvidenceControl{
    "EV.7",
    "check-evidence-control-attributes",
    "Evidence cited in the safety case should be under configuration or document control, with "
    "clear ownership, version or revision, date, status, and a stable retrieval location."};
constexpr Guideline kEvidenceCitationPrecision{
    "EV.4",
    "check-evidence-citation-precision",
    "Reference the exact section, figure, table, dataset, or artifact portion that supports the "
    "claim when practical."};
constexpr Guideline kEvidenceFixed{
    "EV.8",
    "check-evidence-state-fixed",
    "Do not cite live mutable content as evidence unless the cited state is fixed, versioned, "
    "archived, or otherwise controlled so that the reviewed argument always refers to the same "
    "content."};
constexpr Guideline kArguingFromAbsence{
    "LF.3",
    "check-completeness-vs-absence",
    "Do not treat lack of discovered evidence against a claim as positive evidence for the claim."};

// The word lists these checks match on used to be written out here, each
// derived by hand from a guideline's statement, detection hints or examples.
// SCCG 0.7.0 publishes them: `tool.markers` gives 27 guidelines their terms and
// says what a hit means, and `tool.thresholds` gives CL.3 and LF.6 their
// numbers. Two tools matching different words are not running the same check,
// so the lists come from the catalog now and this file holds none.
//
// Loaded lazily here rather than passed in: `CheckStagedArgument` is called
// from four layers with no catalog of their own, and the catalog does not
// change while the process runs. A catalog that cannot be loaded yields empty
// lists, and `ImplementedCheckIds` then stops naming the checks that depend on
// them -- an empty findings array must never stand for a check that never ran.
const parser::GuidelinesDocument* CatalogDocument() {
    static core::GuidelineCatalog catalog;
    static const bool loaded = [] {
        std::string error;
        return core::LoadGuidelineCatalog(catalog, error);
    }();
    return loaded ? &catalog.document : nullptr;
}

// The terms published for one guideline under one marker kind. `effect` is not
// filtered here: each caller knows whether it is looking for a candidate marker,
// a suppressing one, or an expected one whose *absence* is the signal.
const std::vector<std::string>& MarkerTerms(const char* guideline_id, const char* kind) {
    static const std::vector<std::string> none;
    const parser::GuidelinesDocument* document = CatalogDocument();
    if (document == nullptr) {
        return none;
    }
    const parser::Guideline* guideline = document->FindGuidelineById(guideline_id);
    if (guideline == nullptr) {
        return none;
    }
    for (const parser::GuidelineMarker& marker : guideline->tool.markers) {
        if (marker.kind == kind) {
            return marker.terms;
        }
    }
    return none;
}

// A published threshold, or zero when the catalog does not supply it. Zero is
// the "cannot run" signal for the one check that uses one, the same way an
// empty marker list is.
double Threshold(const char* guideline_id, const char* threshold_id) {
    const parser::GuidelinesDocument* document = CatalogDocument();
    if (document == nullptr) {
        return 0.0;
    }
    const parser::Guideline* guideline = document->FindGuidelineById(guideline_id);
    if (guideline == nullptr) {
        return 0.0;
    }
    for (const parser::GuidelineThreshold& threshold : guideline->tool.thresholds) {
        if (threshold.id == threshold_id) {
            return threshold.value;
        }
    }
    return 0.0;
}

std::vector<std::string> ConcatenatedTerms(const std::vector<std::string>& first,
                                           const std::vector<std::string>& second) {
    std::vector<std::string> terms = first;
    terms.insert(terms.end(), second.begin(), second.end());
    return terms;
}

std::string Lowercased(const std::string& text) {
    std::string lowered = text;
    for (char& character : lowered) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return lowered;
}

// Whole-word match. Without it "all" fires on "install" and "safe" on
// "safety", and a check that cries wolf gets ignored, which is worse than not
// having it.
bool ContainsWord(const std::string& haystack_lower, const std::string& word) {
    std::size_t at = haystack_lower.find(word);
    while (at != std::string::npos) {
        const bool left_ok = at == 0 || std::isalpha(static_cast<unsigned char>(haystack_lower[at - 1])) == 0;
        const std::size_t after = at + word.size();
        const bool right_ok =
            after >= haystack_lower.size() || std::isalpha(static_cast<unsigned char>(haystack_lower[after])) == 0;
        if (left_ok && right_ok) {
            return true;
        }
        at = haystack_lower.find(word, at + 1);
    }
    return false;
}

std::string ElementText(const parser::SacmElement& element) {
    if (!element.content.empty()) {
        return element.content;
    }
    return element.name;
}

// The first word of `words` found whole in the text, or empty. Findings quote
// the term they objected to, so the reviewer judges the word and not the check.
std::string FirstWordIn(const std::string& haystack_lower, const std::vector<std::string>& words) {
    for (const std::string& word : words) {
        if (ContainsWord(haystack_lower, word)) {
            return word;
        }
    }
    return {};
}

// A "<first> <conjunction> <second>" pair with both words from `words`,
// whole-word. The shape CL.2's published check describes -- "two or more
// distinct property terms joined by a conjunction within one claim; one property
// term alone is not a signal" -- with the conjunctions themselves published as
// CL.2's `conjunction` marker rather than the bare " and " this used to assume.
bool FindConjunctionPair(const std::string& haystack_lower,
                         const std::vector<std::string>& words,
                         const std::vector<std::string>& conjunctions,
                         std::string& first,
                         std::string& second) {
    for (const std::string& left : words) {
        for (const std::string& right : words) {
            if (left == right) {
                continue;
            }
            for (const std::string& conjunction : conjunctions) {
                if (ContainsWord(haystack_lower, std::format("{} {} {}", left, conjunction, right))) {
                    first = left;
                    second = right;
                    return true;
                }
            }
        }
    }
    return false;
}

// Words in a stretch of text, counted the way CL.3's `claim_word_count`
// threshold means them: runs separated by whitespace.
std::size_t WordCount(const std::string& text) {
    std::size_t words = 0;
    bool in_word = false;
    for (const char character : text) {
        const bool is_space = std::isspace(static_cast<unsigned char>(character)) != 0;
        if (is_space) {
            in_word = false;
            continue;
        }
        if (!in_word) {
            ++words;
            in_word = true;
        }
    }
    return words;
}

// A four-digit year with non-digit boundaries. EV.7's good example carries its
// date as "approved 2026-03-14"; the year alone is the robust part to detect.
// A dotted number -- "6.3", "12.4.1" -- points inside an artifact just as well
// as the word "section" does, and a citation carrying one has already answered
// EV.4. A version ("rev 1.2") reads the same way and so silences the check:
// that is the safe direction to be wrong in, because a false complaint about a
// citation that is in fact precise is what teaches a reviewer to ignore
// findings.
bool ContainsSectionNumber(const std::string& text) {
    for (std::size_t at = 0; at + 2 < text.size(); ++at) {
        if (std::isdigit(static_cast<unsigned char>(text[at])) == 0) {
            continue;
        }
        std::size_t digits_end = at;
        while (digits_end < text.size() && std::isdigit(static_cast<unsigned char>(text[digits_end])) != 0) {
            ++digits_end;
        }
        const bool dotted = digits_end + 1 < text.size() && text[digits_end] == '.' &&
                            std::isdigit(static_cast<unsigned char>(text[digits_end + 1])) != 0;
        if (dotted) {
            return true;
        }
        at = digits_end;
    }
    return false;
}

bool ContainsYear(const std::string& text) {
    for (std::size_t at = 0; at + 4 <= text.size(); ++at) {
        const bool starts_19_or_20 = (text.compare(at, 2, "19") == 0 || text.compare(at, 2, "20") == 0);
        if (!starts_19_or_20) {
            continue;
        }
        const bool four_digits = std::isdigit(static_cast<unsigned char>(text[at + 2])) != 0 &&
                                 std::isdigit(static_cast<unsigned char>(text[at + 3])) != 0;
        if (!four_digits) {
            continue;
        }
        const bool left_ok = at == 0 || std::isdigit(static_cast<unsigned char>(text[at - 1])) == 0;
        const bool right_ok = at + 4 >= text.size() || std::isdigit(static_cast<unsigned char>(text[at + 4])) == 0;
        if (left_ok && right_ok) {
            return true;
        }
    }
    return false;
}

void Add(std::vector<StagedFinding>& findings,
         const Guideline& guideline,
         std::string detail,
         const std::string& element_id,
         FindingSeverity severity,
         std::vector<std::string> params = {}) {
    findings.push_back(StagedFinding{guideline.id,
                                     guideline.check_id,
                                     guideline.statement,
                                     std::move(detail),
                                     std::move(params),
                                     element_id,
                                     severity});
}

// AR.2's `check-explicit-strategy`, stated as the catalog states it: a
// decomposition whose children carry no reasoning step.
//
// Two deliberate narrowings, both toward silence. A single sub-claim is a
// refinement rather than a decomposition -- AR.2 asks why *these* children were
// chosen, a question one child does not raise -- and a claim supported directly
// by evidence is not decomposed at all. GSN permits goal-to-goal support
// without a strategy, so this is advisory: SCCG publishes the check as a
// `boolean_candidate`, a candidate finding a reviewer still has to judge, and a
// false structural complaint against an imported argument costs more than a
// missed one.
bool IsUnexplainedDecomposition(const TreeNode& claim) {
    size_t sub_claims = 0;
    for (const TreeNode* child : claim.group1_children) {
        if (child == nullptr) {
            continue;
        }
        if (child->role == NodeRole::Strategy) {
            return false;
        }
        if (child->role == NodeRole::Claim) {
            ++sub_claims;
        }
    }
    return sub_claims >= 2;
}

} // namespace

const char* FindingSeverityToString(FindingSeverity severity) {
    return severity == FindingSeverity::Problem ? "problem" : "advisory";
}

const std::vector<std::string>& ImplementedCheckIds() {
    // Written out rather than derived from a run: a check that happens not to
    // fire on one argument is still implemented, and a list built from findings
    // would shrink to whatever the last case tripped.
    //
    // The lexical checks are conditional on the catalog, because their word
    // lists and thresholds come from it. Naming one the catalog could not
    // supply would be the exact claim this list exists to prevent: an agent
    // reading an empty findings array has no way to tell "this check found
    // nothing" from "this check never ran".
    static const std::vector<std::string> ids = [] {
        std::vector<std::string> collected{
            // Structural. Decided from the argument graph, so they run whether
            // or not the catalog loaded.
            "check-evidence-trace",
            "check-explicit-strategy",
            "check-element-role-misuse",
            "check-circular-support",
        };
        struct LexicalCheck {
            const char* check_id;
            const char* guideline_id;
            const char* required_marker_kind;
        };
        static constexpr LexicalCheck kLexicalChecks[] = {
            {"check-bounded-qualifiers", "CL.5", "unbounded_evaluative_term"},
            {"check-single-property", "CL.2", "property_term"},
            {"check-claim-length-and-role-mixing", "CL.3", nullptr},
            {"check-claim-ambiguity", "CL.4", "ambiguous_qualifier"},
            {"check-claim-step-mixing", "CL.6", "lifecycle_step_verb"},
            {"check-element-signposting", "RD.1", "inference_connective"},
            {"check-promotional-language", "RD.4", "promotional_term"},
            {"check-completeness-vs-absence", "LF.3", "absence_phrase"},
            {"check-evidence-citation-precision", "EV.4", "citation_precision_marker"},
            {"check-evidence-control-attributes", "EV.7", "control_attribute_marker"},
            {"check-evidence-state-fixed", "EV.8", "mutable_source_marker"},
        };
        for (const LexicalCheck& check : kLexicalChecks) {
            const bool available = check.required_marker_kind == nullptr
                                       ? Threshold(check.guideline_id, "claim_word_count") > 0.0
                                       : !MarkerTerms(check.guideline_id, check.required_marker_kind).empty();
            if (available) {
                collected.emplace_back(check.check_id);
            }
        }
        return collected;
    }();
    return ids;
}

std::vector<StagedFinding> CheckStagedArgument(const parser::AssuranceCase& preview,
                                               const std::vector<std::string>& changed_element_ids) {
    std::vector<StagedFinding> findings;

    std::set<std::string> changed(changed_element_ids.begin(), changed_element_ids.end());
    if (changed.empty()) {
        return findings;
    }

    // A relationship is its own element, so attaching a child reports the new
    // relationship and the new child as changed and leaves the parent looking
    // untouched -- though its child set is exactly what changed. Relationship
    // ids are not tree nodes and would otherwise be dropped here, so resolving
    // them to the elements they connect both rescues an id that reports nothing
    // and gives the parent-side checks the parent they are about. Without this,
    // `check-explicit-strategy` could only ever fire when a decomposition was
    // created whole in one change set.
    for (const parser::SacmElement& element : preview.elements) {
        if (element.id.empty() || changed.count(element.id) == 0) {
            continue;
        }
        if (element.source_refs.empty() || element.target_refs.empty()) {
            continue;
        }
        changed.insert(element.source_refs.begin(), element.source_refs.end());
        changed.insert(element.target_refs.begin(), element.target_refs.end());
    }

    // Built rather than walked by hand: the tree already resolves which SACM
    // relationship direction means "supported by" and which means "in context
    // of", and duplicating that here would be a second answer to drift from.
    const AssuranceTree tree = AssuranceTree::Build(preview);

    for (const std::string& id : changed) {
        const TreeNode* node = FindTreeNode(tree, id);
        if (node == nullptr) {
            continue;
        }
        const parser::SacmElement* element = nullptr;
        for (const parser::SacmElement& candidate : preview.elements) {
            if (candidate.id == id) {
                element = &candidate;
                break;
            }
        }
        if (element == nullptr) {
            continue;
        }

        const bool has_support = !node->group1_children.empty();

        switch (node->role) {
        case NodeRole::Claim:
            if (!has_support && !element->undeveloped) {
                Add(findings,
                    kEvidencePath,
                    "This claim has no support and is not marked undeveloped, so a reviewer cannot "
                    "tell whether evidence is missing or still to come. Give it support, or mark "
                    "it undeveloped to say so deliberately.",
                    id,
                    FindingSeverity::Advisory);
            }
            if (IsUnexplainedDecomposition(*node)) {
                Add(findings,
                    kInferenceStep,
                    "This claim is broken into sub-claims with no reasoning step saying how they "
                    "were chosen or why together they support it. Add a strategy stating the "
                    "decomposition rule.",
                    id,
                    FindingSeverity::Advisory);
            }
            break;

        case NodeRole::Strategy:
            // AR.1, not AR.2: an argument element's role is to explain the
            // reasoning, and one that develops into nothing is not performing
            // it. AR.2's check is on the decomposition above, handled under
            // NodeRole::Claim.
            if (!has_support) {
                // The role word is a discriminator, not an interpolation:
                // `check-element-role-misuse` now backs two findings needing two
                // sentences, and check_id alone can no longer choose between
                // them at display.
                Add(findings,
                    kStructureCarriesArgument,
                    "This strategy develops into nothing. A decomposition step that produces no "
                    "sub-claims states an inference the argument never makes.",
                    id,
                    FindingSeverity::Problem,
                    {"strategy"});
            }
            break;

        case NodeRole::Solution:
            if (has_support) {
                Add(findings,
                    kStructureCarriesArgument,
                    "This is a solution -- the artefact or observation the argument rests on -- so "
                    "it should be a leaf. Elements hanging beneath it are not carrying the role "
                    "the structure says they are.",
                    id,
                    FindingSeverity::Problem,
                    {"solution"});
            }
            break;

        case NodeRole::Context:
        case NodeRole::Assumption:
        case NodeRole::Justification:
        case NodeRole::Other:
            break;
        }

        // The lexical checks. Lexical checks are the ones most likely to be
        // wrong, so every one of them is advisory, quotes what it matched, and
        // reports at most once per element. The reviewer judges the words; the
        // check only points at them.
        const std::string text = Lowercased(ElementText(*element));

        if (node->role == NodeRole::Claim) {
            // CL.5: an unbounded evaluative or universal qualifier, unless the
            // claim already carries a bounding marker. The suppression is
            // published as CL.5's `bounding_marker` and is new here: a claim
            // saying "safe as defined in the acceptance criterion" was reported
            // for the word "safe" while stating exactly the bound the guideline
            // asks for.
            const bool bounded = !FirstWordIn(text, MarkerTerms("CL.5", "bounding_marker")).empty();
            const std::string unbounded_term =
                bounded ? std::string()
                        : FirstWordIn(text,
                                      ConcatenatedTerms(MarkerTerms("CL.5", "unbounded_evaluative_term"),
                                                        MarkerTerms("CL.5", "universal_qualifier")));
            if (!unbounded_term.empty()) {
                Add(findings,
                    kBoundQualifiers,
                    "This claim uses \"" + unbounded_term +
                        "\", which SCCG names as a term needing bounds. Say what it means here -- "
                        "against which hazards, in which operating conditions, to what standard -- "
                        "in the claim or in attached context.",
                    id,
                    FindingSeverity::Advisory,
                    {unbounded_term});
            }

            // CL.4: a qualifier or hedge that leaves two competent reviewers
            // reading the claim differently. Skipped where CL.5 already
            // objected to the same word -- "adequate" and "appropriate" appear
            // in both lists, and two advisories quoting one word read as two
            // problems.
            const std::string ambiguous_term = FirstWordIn(
                text,
                ConcatenatedTerms(MarkerTerms("CL.4", "ambiguous_qualifier"), MarkerTerms("CL.4", "hedging_adverb")));
            if (!ambiguous_term.empty() && ambiguous_term != unbounded_term) {
                Add(findings,
                    kClaimAmbiguity,
                    "This claim uses \"" + ambiguous_term +
                        "\", which two competent reviewers can read differently. Say what the claim "
                        "asserts and of what, in terms a reviewer could check.",
                    id,
                    FindingSeverity::Advisory,
                    {ambiguous_term});
            }

            // CL.3, both halves of the condition the catalog publishes: a claim
            // past the word count, or one whose enumeration marker introduces a
            // list of topics. Reporting only the first under this check id would
            // let an agent deduplicating on the id read a silent result as the
            // whole check having run.
            const double word_limit = Threshold("CL.3", "claim_word_count");
            const std::size_t words = WordCount(text);
            const std::string enumeration = FirstWordIn(text, MarkerTerms("CL.3", "enumeration_marker"));
            if (word_limit > 0.0 && static_cast<double>(words) > word_limit) {
                Add(findings,
                    kClaimLength,
                    std::format("This claim runs to {} words, past the {} SCCG publishes as the point "
                                "where a claim is carrying more than a claim. Move scope into context, "
                                "reasoning into a strategy, and listed topics into sub-claims.",
                                words,
                                static_cast<long long>(word_limit)),
                    id,
                    FindingSeverity::Advisory,
                    {std::to_string(words), std::to_string(static_cast<long long>(word_limit))});
            } else if (!enumeration.empty()) {
                Add(findings,
                    kClaimLength,
                    "This claim uses \"" + enumeration +
                        "\" to introduce a list of topics, which is a decomposition written as prose. "
                        "Make each listed topic a sub-claim, so the structure carries the breakdown.",
                    id,
                    FindingSeverity::Advisory,
                    {enumeration});
            }

            // CL.2: two distinct properties bundled by a conjunction.
            const std::vector<std::string> conjunctions = MarkerTerms("CL.2", "conjunction");
            std::string first;
            std::string second;
            if (FindConjunctionPair(text, MarkerTerms("CL.2", "property_term"), conjunctions, first, second)) {
                Add(findings,
                    kSingleProperty,
                    std::format("This claim joins \"{}\" and \"{}\" -- two distinct properties needing "
                                "different evidence and review. Give each its own goal, so one can "
                                "fail without hiding the other.",
                                first,
                                second),
                    id,
                    FindingSeverity::Advisory,
                    {first, second});
            }

            // CL.6: lifecycle steps chained into one judgement. SCCG's own
            // check fires on "two or more lifecycle step verbs within one
            // claim"; requiring the conjunction is a narrowing calibration --
            // the shape of the guideline's own bad example, and what lets the
            // finding quote the chain it objected to rather than two words that
            // happen to share a sentence.
            if (FindConjunctionPair(text, MarkerTerms("CL.6", "lifecycle_step_verb"), conjunctions, first, second)) {
                Add(findings,
                    kStepMixing,
                    std::format("This claim chains \"{}\" and \"{}\" -- different logical steps answering "
                                "different review questions. Give each step its own claim, and let "
                                "the structure show the decomposition.",
                                first,
                                second),
                    id,
                    FindingSeverity::Advisory,
                    {first, second});
            }

            // RD.1: reasoning smuggled into the claim sentence.
            const std::string connective = FirstWordIn(text, MarkerTerms("RD.1", "inference_connective"));
            if (!connective.empty()) {
                Add(findings,
                    kSignposting,
                    "This claim carries its own reasoning (\"" + connective +
                        "\"), so a reviewer cannot tell the claim from the argument for it. State "
                        "the claim alone; the reasoning belongs in a strategy and the evidence in "
                        "a solution.",
                    id,
                    FindingSeverity::Advisory,
                    {connective});
            }
        }

        // LF.3: absence of discovered evidence offered as support. Claims and
        // justifications, because that is where its bad example writes it.
        if (node->role == NodeRole::Claim || node->role == NodeRole::Justification) {
            // The published `absence_phrase` markers are whole phrases -- "no
            // failures observed", "no known" -- rather than the "no" plus a
            // verb this matched before, so a sentence that merely contains both
            // words no longer fires.
            if (!FirstWordIn(text, MarkerTerms("LF.3", "absence_phrase")).empty()) {
                Add(findings,
                    kArguingFromAbsence,
                    "This text treats the absence of discovered evidence as support. Not finding "
                    "something does not establish the claim; argue from what the applied methods "
                    "can show.",
                    id,
                    FindingSeverity::Advisory);
            }
        }

        // RD.4: promotional language, anywhere argument text lives.
        if (node->role == NodeRole::Claim || node->role == NodeRole::Strategy || node->role == NodeRole::Solution) {
            const std::string promotional = FirstWordIn(text, MarkerTerms("RD.4", "promotional_term"));
            if (!promotional.empty()) {
                Add(findings,
                    kPromotionalLanguage,
                    "This text calls the work \"" + promotional +
                        "\". Promotional language persuades nobody reviewing a safety argument; "
                        "state what was shown, and under which assumptions.",
                    id,
                    FindingSeverity::Advisory,
                    {promotional});
            }
        }

        if (node->role == NodeRole::Solution) {
            // EV.7: nothing on this evidence reads as document control.
            // A four-digit year still counts as a control marker although
            // SCCG's list says "dated": its own good example writes the date as
            // "approved 2026-03-14", and complaining about a reference that
            // carries one is the false positive that teaches a reviewer to stop
            // reading findings.
            const bool has_control_marker =
                !FirstWordIn(text, MarkerTerms("EV.7", "control_attribute_marker")).empty() || ContainsYear(text);
            if (!has_control_marker) {
                Add(findings,
                    kEvidenceControl,
                    "This evidence reference carries no owner, version, date, or status, so a "
                    "reviewer cannot tell which artifact was assessed or whether it has changed "
                    "since. Cite the controlled version.",
                    id,
                    FindingSeverity::Advisory);
            }

            // EV.4: a whole-artifact citation with nothing pointing inside it.
            // Deliberately silent when the reference carries a section number in
            // any form -- "6.3", "§6" -- because the guideline asks for the
            // portion to be identified, not for a particular word to appear.
            if (FirstWordIn(text, MarkerTerms("EV.4", "citation_precision_marker")).empty() &&
                !ContainsSectionNumber(text)) {
                Add(findings,
                    kEvidenceCitationPrecision,
                    "This evidence names an artifact but no part of it, so a reviewer cannot find "
                    "the material that supports the claim. Cite the section, table, figure or "
                    "scenarios the argument rests on.",
                    id,
                    FindingSeverity::Advisory);
            }

            // EV.8: a mutable source cited without anything fixing its state.
            const std::string mutable_word = FirstWordIn(text, MarkerTerms("EV.8", "mutable_source_marker"));
            const bool state_fixed =
                !FirstWordIn(text, MarkerTerms("EV.8", "fixing_marker")).empty() || ContainsYear(text);
            if (!mutable_word.empty() && !state_fixed) {
                Add(findings,
                    kEvidenceFixed,
                    "This evidence cites \"" + mutable_word +
                        "\", which reads as live mutable content. Cite a fixed version, revision, "
                        "or archived snapshot, so the reviewed argument always refers to the same "
                        "content.",
                    id,
                    FindingSeverity::Advisory,
                    {mutable_word});
            }
        }
    }

    // A cycle is reported wherever it appears, not only on changed elements: a
    // change set that closes a loop through untouched elements still created it.
    for (const ArgumentCycle& cycle : FindSupportCycles(preview)) {
        bool touches_change = false;
        for (const std::string& id : cycle.element_ids) {
            if (changed.count(id) > 0) {
                touches_change = true;
                break;
            }
        }
        if (!touches_change) {
            continue;
        }
        Add(findings,
            kCircularSupport,
            "These operations put a claim in its own support chain, so the argument supports "
            "itself and establishes nothing.",
            cycle.element_ids.empty() ? std::string() : cycle.element_ids.front(),
            FindingSeverity::Problem);
    }

    return findings;
}

} // namespace core::sccg
