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
// changing one means changing the other in the same place.
struct Guideline {
    const char* id;
    const char* statement;
};

constexpr Guideline kEvidencePath{
    "EV.1",
    "Show, for each claim, the evidence path that supports it, either directly or through "
    "stated sub-claims and intermediate arguments."};
constexpr Guideline kInferenceStep{"AR.2",
                                   "State how the parent claim is being decomposed or argued; do not make the reviewer "
                                   "infer the decomposition rule from wording alone."};
constexpr Guideline kStructureCarriesArgument{"AR.1",
                                              "Use the assurance case structure to make each element's role clear."};
constexpr Guideline kBoundQualifiers{
    "CL.5",
    "Do not leave broad evaluative terms or universal qualifiers unbounded. This includes "
    "terms such as safe, timely, effective, normal, robust, all, every, and never."};

// The terms CL.5 names. Taken from the guideline text rather than invented, so
// the check cannot drift from what SCCG actually asks for.
const std::vector<std::string>& UnboundedQualifiers() {
    static const std::vector<std::string> terms{
        "safe", "timely", "effective", "normal", "robust", "all", "every", "never"};
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

void Add(std::vector<StagedFinding>& findings,
         const Guideline& guideline,
         std::string detail,
         const std::string& element_id,
         FindingSeverity severity) {
    findings.push_back(StagedFinding{guideline.id, guideline.statement, std::move(detail), element_id, severity});
}

} // namespace

const char* FindingSeverityToString(FindingSeverity severity) {
    return severity == FindingSeverity::Problem ? "problem" : "advisory";
}

std::vector<StagedFinding> CheckStagedArgument(const parser::AssuranceCase& preview,
                                               const std::vector<std::string>& changed_element_ids) {
    std::vector<StagedFinding> findings;

    const std::set<std::string> changed(changed_element_ids.begin(), changed_element_ids.end());
    if (changed.empty()) {
        return findings;
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
            break;

        case NodeRole::Strategy:
            if (!has_support) {
                Add(findings,
                    kInferenceStep,
                    "This strategy develops into nothing. A decomposition step that produces no "
                    "sub-claims states an inference the argument never makes.",
                    id,
                    FindingSeverity::Problem);
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
                    FindingSeverity::Problem);
            }
            break;

        case NodeRole::Context:
        case NodeRole::Assumption:
        case NodeRole::Justification:
        case NodeRole::Other:
            break;
        }

        // CL.5 is lexical, and lexical checks are the ones most likely to be
        // wrong, so it is advisory and quotes the term it objected to. The
        // reviewer decides whether the term is in fact bounded elsewhere.
        if (node->role == NodeRole::Claim) {
            const std::string text = Lowercased(ElementText(*element));
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
                    FindingSeverity::Advisory);
                break;
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
            kStructureCarriesArgument,
            "These operations put a claim in its own support chain, so the argument supports "
            "itself and establishes nothing.",
            cycle.element_ids.empty() ? std::string() : cycle.element_ids.front(),
            FindingSeverity::Problem);
    }

    return findings;
}

} // namespace core::sccg
