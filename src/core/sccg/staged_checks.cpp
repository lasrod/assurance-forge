#include "core/sccg/staged_checks.h"

#include "core/assurance_tree.h"
#include "core/problems/argument_cycles.h"

#include <algorithm>
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

// The terms CL.5 names. Taken from the guideline text rather than invented, so
// the check cannot drift from what SCCG actually asks for.
const std::vector<std::string>& UnboundedQualifiers() {
    static const std::vector<std::string> terms{
        "safe", "timely", "effective", "normal", "robust", "all", "every", "never"};
    return terms;
}

// The property words CL.2's conjunction check pairs. CL.2's own detection
// hints name "safe and secure" and "complete and correct"; the rest are the
// evaluative terms CL.5's statement lists, which are the properties a bundled
// goal most often bundles. A generic "and" would fire on every legitimate
// compound noun, and a check that cries wolf gets ignored.
const std::vector<std::string>& PropertyWords() {
    static const std::vector<std::string> words{
        "safe", "secure", "complete", "correct", "timely", "effective", "normal", "robust"};
    return words;
}

// The lifecycle-step verbs CL.6 forbids chaining. From the statement's own
// step names (identification, implementation, validation) plus the verb its
// bad example chains ("mitigated and validated").
const std::vector<std::string>& StepVerbs() {
    static const std::vector<std::string> verbs{"identified", "implemented", "validated", "mitigated", "verified"};
    return verbs;
}

// RD.1: reasoning smuggled into claim text. "Because" is the connective the
// guideline's own bad example uses; "therefore" is the same defect running the
// other direction. "Since" is deliberately absent -- it is temporal more often
// than inferential, and a wolf-crying check gets ignored.
const std::vector<std::string>& ReasoningConnectives() {
    static const std::vector<std::string> connectives{"because", "therefore"};
    return connectives;
}

// The promotional adjectives RD.4's detection hints name.
const std::vector<std::string>& PromotionalWords() {
    static const std::vector<std::string> words{"world-class", "cutting-edge", "outstanding", "best-in-class"};
    return words;
}

// EV.7: any of these reads as the evidence being under document control. The
// attribute names come from the guideline's statement; "rev" and "approved"
// are how its own good example writes two of them.
const std::vector<std::string>& ControlMarkers() {
    static const std::vector<std::string> markers{
        "owner", "version", "rev", "revision", "date", "status", "approved", "baseline"};
    return markers;
}

// EV.8: words that read as live mutable content. "Wiki" and the shared-doc
// platforms are from the guideline's detection hints and bad example;
// "latest" is the mutable reference in prose form.
const std::vector<std::string>& MutableSourceWords() {
    static const std::vector<std::string> words{"wiki", "confluence", "sharepoint", "latest"};
    return words;
}

// EV.8: any of these fixes the cited state, which is exactly what its good
// examples add to an otherwise mutable reference.
const std::vector<std::string>& FixedStateMarkers() {
    static const std::vector<std::string> markers{
        "version", "rev", "revision", "snapshot", "archived", "approved", "captured"};
    return markers;
}

// LF.3: absence-of-finding verbs. Fires only together with a whole-word "no",
// which is the shape of its bad example ("no further hazards were found").
const std::vector<std::string>& AbsenceVerbs() {
    static const std::vector<std::string> verbs{"found", "observed", "identified", "detected", "reported"};
    return verbs;
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

// A "<first> and <second>" pair with both words from `words`, whole-word. The
// shape of CL.2's "safe and secure" and CL.6's "mitigated and validated": the
// conjunction is what bundles two judgements into one sentence.
bool FindConjunctionPair(const std::string& haystack_lower,
                         const std::vector<std::string>& words,
                         std::string& first,
                         std::string& second) {
    for (const std::string& left : words) {
        for (const std::string& right : words) {
            if (left == right) {
                continue;
            }
            if (ContainsWord(haystack_lower, left + " and " + right)) {
                first = left;
                second = right;
                return true;
            }
        }
    }
    return false;
}

// A four-digit year with non-digit boundaries. EV.7's good example carries its
// date as "approved 2026-03-14"; the year alone is the robust part to detect.
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
            // CL.5: an unbounded evaluative or universal qualifier.
            for (const std::string& term : UnboundedQualifiers()) {
                if (!ContainsWord(text, term)) {
                    continue;
                }
                Add(findings,
                    kBoundQualifiers,
                    "This claim uses \"" + term +
                        "\", which SCCG names as a term needing bounds. Say what it means here -- "
                        "against which hazards, in which operating conditions, to what standard -- "
                        "in the claim or in attached context.",
                    id,
                    FindingSeverity::Advisory,
                    {term});
                break;
            }

            // CL.2: two distinct properties bundled by a conjunction.
            std::string first;
            std::string second;
            if (FindConjunctionPair(text, PropertyWords(), first, second)) {
                Add(findings,
                    kSingleProperty,
                    "This claim joins \"" + first + " and " + second +
                        "\" -- two distinct properties needing different evidence and review. Give "
                        "each its own goal, so one can fail without hiding the other.",
                    id,
                    FindingSeverity::Advisory,
                    {first, second});
            }

            // CL.6: lifecycle steps chained into one judgement.
            if (FindConjunctionPair(text, StepVerbs(), first, second)) {
                Add(findings,
                    kStepMixing,
                    "This claim chains \"" + first + " and " + second +
                        "\" -- different logical steps answering different review questions. Give "
                        "each step its own claim, and let the structure show the decomposition.",
                    id,
                    FindingSeverity::Advisory,
                    {first, second});
            }

            // RD.1: reasoning smuggled into the claim sentence.
            const std::string connective = FirstWordIn(text, ReasoningConnectives());
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
            if (ContainsWord(text, "no") && !FirstWordIn(text, AbsenceVerbs()).empty()) {
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
            const std::string promotional = FirstWordIn(text, PromotionalWords());
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
            const bool has_control_marker = !FirstWordIn(text, ControlMarkers()).empty() || ContainsYear(text);
            if (!has_control_marker) {
                Add(findings,
                    kEvidenceControl,
                    "This evidence reference carries no owner, version, date, or status, so a "
                    "reviewer cannot tell which artifact was assessed or whether it has changed "
                    "since. Cite the controlled version.",
                    id,
                    FindingSeverity::Advisory);
            }

            // EV.8: a mutable source cited without anything fixing its state.
            const std::string mutable_word = FirstWordIn(text, MutableSourceWords());
            const bool state_fixed = !FirstWordIn(text, FixedStateMarkers()).empty() || ContainsYear(text);
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
