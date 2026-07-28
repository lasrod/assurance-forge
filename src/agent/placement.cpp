#include "agent/operations.h"

#include "core/assurance_tree.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace agent {
namespace {

constexpr int kDefaultSuggestions = 5;
constexpr int kMaxSuggestions     = 20;

std::string Lowercased(const std::string& text) {
    std::string lowered = text;
    for (char& character : lowered) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return lowered;
}

// Words worth matching on. Dropping the short ones keeps "of" and "the" from
// scoring every claim in the argument equally, which is the failure that makes
// a ranking useless.
std::vector<std::string> Keywords(const std::string& topic) {
    std::vector<std::string> words;
    std::istringstream       stream(Lowercased(topic));
    std::string              word;
    while (stream >> word) {
        std::string cleaned;
        for (char character : word) {
            if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
                cleaned.push_back(character);
            }
        }
        if (cleaned.size() >= 4) {
            words.push_back(cleaned);
        }
    }
    return words;
}

std::string ElementText(const parser::SacmElement& element) {
    return Lowercased(element.name + " " + element.content + " " + element.description);
}

// The path from the root down to a node, which is the single most useful thing
// to hand an agent choosing where to attach: it says what the branch is about
// without needing the whole tree.
std::vector<std::string> PathToRoot(const core::TreeNode& node) {
    std::vector<std::string> path;
    for (const core::TreeNode* walk = &node; walk != nullptr; walk = walk->parent) {
        path.push_back(walk->label.empty() ? walk->id : walk->label);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

const char* RoleName(core::NodeRole role) {
    switch (role) {
    case core::NodeRole::Claim:
        return "Goal";
    case core::NodeRole::Strategy:
        return "Strategy";
    case core::NodeRole::Solution:
        return "Solution";
    case core::NodeRole::Context:
        return "Context";
    case core::NodeRole::Assumption:
        return "Assumption";
    case core::NodeRole::Justification:
        return "Justification";
    case core::NodeRole::Other:
        break;
    }
    return "Other";
}

struct Candidate {
    const core::TreeNode*      node = nullptr;
    const parser::SacmElement* element = nullptr;
    int                        score = 0;
    std::vector<std::string>   matched;
};

} // namespace

Result SuggestPlacement(const ReadContext& context, const nlohmann::json& arguments) {
    if (!context.state.loaded_case.has_value()) {
        return Result::Error("No assurance case is loaded for this project.");
    }

    const nlohmann::json::const_iterator topic_argument = arguments.find("topic");
    if (topic_argument == arguments.end() || !topic_argument->is_string() ||
        topic_argument->get<std::string>().empty()) {
        return Result::Error("Argument \"topic\" is required: describe what the new argument would "
                             "establish, and this reports where it would fit.");
    }
    const std::string              topic    = topic_argument->get<std::string>();
    const std::vector<std::string> keywords = Keywords(topic);

    int limit = kDefaultSuggestions;
    const nlohmann::json::const_iterator limit_argument = arguments.find("limit");
    if (limit_argument != arguments.end() && limit_argument->is_number_integer()) {
        limit = std::clamp(limit_argument->get<int>(), 1, kMaxSuggestions);
    }

    const parser::AssuranceCase& model = context.state.loaded_case.value();
    const core::AssuranceTree    tree  = core::AssuranceTree::Build(model);

    std::map<std::string, const parser::SacmElement*> by_id;
    for (const parser::SacmElement& element : model.elements) {
        by_id[element.id] = &element;
    }

    // Only goals and strategies: those are the things new argument hangs from.
    // Offering a solution or a context as an anchor would invite exactly the
    // structural mistake the SCCG checks then report.
    std::vector<Candidate> candidates;
    for (const std::pair<const std::string, const parser::SacmElement*>& entry : by_id) {
        const core::TreeNode* node = core::FindTreeNode(tree, entry.first);
        if (node == nullptr ||
            (node->role != core::NodeRole::Claim && node->role != core::NodeRole::Strategy)) {
            continue;
        }

        Candidate candidate;
        candidate.node    = node;
        candidate.element = entry.second;

        const std::string text = ElementText(*entry.second);
        for (const std::string& keyword : keywords) {
            if (text.find(keyword) != std::string::npos) {
                candidate.score += 10;
                candidate.matched.push_back(keyword);
            }
        }
        // A goal already being developed is a better host than a leaf: attaching
        // beside existing sub-claims extends an argument, where attaching to a
        // leaf silently changes what that leaf was going to be.
        if (!node->group1_children.empty()) {
            candidate.score += 2;
        }
        // A claim explicitly marked undeveloped is asking for exactly this.
        if (entry.second->undeveloped) {
            candidate.score += 6;
        }
        if (candidate.score > 0) {
            candidates.push_back(std::move(candidate));
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& left, const Candidate& right) {
                         return left.score > right.score;
                     });
    if (static_cast<int>(candidates.size()) > limit) {
        candidates.resize(static_cast<std::size_t>(limit));
    }

    nlohmann::json suggestions = nlohmann::json::array();
    for (const Candidate& candidate : candidates) {
        nlohmann::json siblings = nlohmann::json::array();
        for (const core::TreeNode* child : candidate.node->group1_children) {
            if (child != nullptr) {
                siblings.push_back(nlohmann::json{{"id", child->id}, {"label", child->label}});
            }
        }
        nlohmann::json in_scope = nlohmann::json::array();
        for (const core::TreeNode* attachment : candidate.node->group2_attachments) {
            if (attachment != nullptr) {
                in_scope.push_back(
                    nlohmann::json{{"id", attachment->id},
                                   {"role", RoleName(attachment->role)},
                                   {"label", attachment->label}});
            }
        }

        suggestions.push_back(nlohmann::json{
            {"id", candidate.element->id},
            {"role", RoleName(candidate.node->role)},
            {"label", candidate.node->label},
            {"content", candidate.element->content},
            {"undeveloped", candidate.element->undeveloped},
            {"path_from_root", PathToRoot(*candidate.node)},
            {"existing_sub_claims", std::move(siblings)},
            {"context_in_scope", std::move(in_scope)},
            {"matched_terms", candidate.matched},
            {"score", candidate.score},
        });
    }

    return Result::Ok(nlohmann::json{
        {"topic", topic},
        {"suggestions", std::move(suggestions)},
        {"note",
         "Ranked by term overlap and structural fit, not by understanding. Read the paths and the "
         "sibling claims and decide for yourself; if nothing here is the right home, say so rather "
         "than attaching to the best of a bad set."},
    });
}

} // namespace agent
