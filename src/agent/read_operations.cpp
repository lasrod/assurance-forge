#include "agent/operations.h"

#include "core/assurance_tree.h"
#include "parser/model_utils.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <string_view>

namespace agent {
namespace {

std::string Lowercased(std::string_view text) {
    std::string lowered(text);
    for (char& character : lowered) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return lowered;
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

int ClampedInt(const nlohmann::json& arguments, const char* key, int fallback, int maximum) {
    const nlohmann::json::const_iterator found = arguments.find(key);
    if (found == arguments.end() || !found->is_number_integer()) {
        return fallback;
    }
    return std::clamp(found->get<int>(), 1, maximum);
}

std::string StringArgument(const nlohmann::json& arguments, const char* key) {
    const nlohmann::json::const_iterator found = arguments.find(key);
    if (found == arguments.end() || !found->is_string()) {
        return {};
    }
    return found->get<std::string>();
}

// The SACM relationship element kinds the POD projection carries. A relationship
// is an element like any other here, which is why membership is tested by type
// rather than by a flag.
bool IsRelationship(const parser::SacmElement& element) {
    return element.type.rfind("asserted", 0) == 0;
}

nlohmann::json ElementSummary(const parser::SacmElement& element) {
    nlohmann::json summary{{"id", element.id}, {"type", element.type}};
    if (!element.name.empty()) {
        summary["name"] = element.name;
    }
    if (!element.content.empty()) {
        summary["content"] = element.content;
    }
    if (element.undeveloped) {
        summary["undeveloped"] = true;
    }
    return summary;
}

nlohmann::json ElementDetail(const parser::SacmElement& element) {
    nlohmann::json detail = ElementSummary(element);
    if (!element.gid.empty()) {
        detail["gid"] = element.gid;
    }
    if (!element.description.empty()) {
        detail["description"] = element.description;
    }
    if (!element.assertion_declaration.empty()) {
        detail["assertion_declaration"] = element.assertion_declaration;
    }
    if (element.is_counter) {
        detail["is_counter"] = true;
    }
    if (!element.source_refs.empty()) {
        detail["source_refs"] = element.source_refs;
    }
    if (!element.target_refs.empty()) {
        detail["target_refs"] = element.target_refs;
    }
    if (!element.reasoning_ref.empty()) {
        detail["reasoning_ref"] = element.reasoning_ref;
    }
    return detail;
}

nlohmann::json TreeNodeJson(const core::TreeNode& node, int remaining_depth) {
    nlohmann::json serialized{{"id", node.id}, {"role", RoleName(node.role)}};
    if (!node.label.empty()) {
        serialized["label"] = node.label;
    }
    if (node.undeveloped) {
        serialized["undeveloped"] = true;
    }
    if (node.is_counter_source) {
        serialized["challenges"] = node.challenge_target_id;
    }

    const bool has_children = !node.group1_children.empty() || !node.group2_attachments.empty();
    if (!has_children) {
        return serialized;
    }
    if (remaining_depth <= 0) {
        // Say so rather than returning a leaf: a truncated tree that looks
        // complete would have an agent conclude a goal has no support.
        serialized["truncated"] = true;
        serialized["child_count"] =
            static_cast<int>(node.group1_children.size() + node.group2_attachments.size());
        return serialized;
    }

    for (const core::TreeNode* child : node.group1_children) {
        if (child != nullptr) {
            serialized["supported_by"].push_back(TreeNodeJson(*child, remaining_depth - 1));
        }
    }
    for (const core::TreeNode* attachment : node.group2_attachments) {
        if (attachment != nullptr) {
            serialized["in_context_of"].push_back(TreeNodeJson(*attachment, remaining_depth - 1));
        }
    }
    return serialized;
}

bool HasCase(const ReadContext& context) {
    return context.state.loaded_case.has_value();
}

Result NoCase() {
    return Result::Error("No assurance case is loaded for this project.");
}

} // namespace

Result Result::Ok(nlohmann::json payload) {
    return Result{std::move(payload), false};
}

Result Result::Error(std::string message) {
    return Result{nlohmann::json{{"error", std::move(message)}}, true};
}

Result GetCaseOverview(const ReadContext& context) {
    if (!HasCase(context)) {
        return NoCase();
    }
    const parser::AssuranceCase& model = context.state.loaded_case.value();

    std::map<std::string, int> counts_by_type;
    for (const parser::SacmElement& element : model.elements) {
        ++counts_by_type[element.type];
    }

    nlohmann::json overview{
        {"case_id", model.id},
        {"case_name", model.name},
        {"element_count", static_cast<int>(model.elements.size())},
        {"element_counts_by_type", counts_by_type},
        {"assurance_claim_point_count", static_cast<int>(model.acps.size())},
        {"project_path", context.project_path},
        // Which file the case came from. A project can hold several argument
        // files, so an agent reasoning about "the case" should be able to see
        // which one that was.
        {"loaded_file", context.state.loaded_file_path.generic_string()},
    };
    if (!model.description.empty()) {
        overview["case_description"] = model.description;
    }
    if (!context.state.load_warnings.empty()) {
        // Surfaced rather than swallowed: a case can load successfully and still
        // not be a conformant interchange document, and an agent reasoning about
        // the argument should know that before it proposes anything.
        overview["load_warnings"] = context.state.load_warnings;
    }
    return Result::Ok(std::move(overview));
}

Result FindElements(const ReadContext& context, const nlohmann::json& arguments) {
    if (!HasCase(context)) {
        return NoCase();
    }

    const std::string query = Lowercased(StringArgument(arguments, "query"));
    const std::string type  = Lowercased(StringArgument(arguments, "type"));
    const int         limit = ClampedInt(arguments, "limit", kDefaultResultLimit, kMaxResultLimit);

    nlohmann::json matches = nlohmann::json::array();
    int            total   = 0;
    for (const parser::SacmElement& element : context.state.loaded_case->elements) {
        if (!type.empty() && Lowercased(element.type) != type) {
            continue;
        }
        if (!query.empty()) {
            const bool hit = Lowercased(element.id).find(query) != std::string::npos ||
                             Lowercased(element.name).find(query) != std::string::npos ||
                             Lowercased(element.content).find(query) != std::string::npos ||
                             Lowercased(element.description).find(query) != std::string::npos;
            if (!hit) {
                continue;
            }
        }
        ++total;
        if (static_cast<int>(matches.size()) < limit) {
            matches.push_back(ElementSummary(element));
        }
    }

    // Counted before the move: reading `matches.size()` after `std::move(matches)`
    // in the same initializer list reads the moved-from value, and braced-init
    // evaluation is left-to-right, so it would reliably report zero.
    const int returned = static_cast<int>(matches.size());
    return Result::Ok(nlohmann::json{
        {"matches", std::move(matches)},
        {"returned", returned},
        {"total_matches", total},
        {"limit", limit},
        {"truncated", total > returned},
    });
}

Result GetElement(const ReadContext& context, const nlohmann::json& arguments) {
    if (!HasCase(context)) {
        return NoCase();
    }

    const std::string id = StringArgument(arguments, "id");
    if (id.empty()) {
        return Result::Error("Argument \"id\" is required.");
    }

    const parser::AssuranceCase& model   = context.state.loaded_case.value();
    const parser::SacmElement*   element = parser::FindElementById(model, id);
    if (element == nullptr) {
        return Result::Error("No element with id \"" + id + "\".");
    }

    nlohmann::json result{{"element", ElementDetail(*element)}};

    // Relationships are elements in this projection, so the neighbourhood is
    // assembled by scanning them rather than by following pointers.
    nlohmann::json incoming = nlohmann::json::array();
    nlohmann::json outgoing = nlohmann::json::array();
    for (const parser::SacmElement& candidate : model.elements) {
        if (!IsRelationship(candidate)) {
            continue;
        }
        const bool is_source = std::find(candidate.source_refs.begin(), candidate.source_refs.end(),
                                         id) != candidate.source_refs.end();
        const bool is_target = std::find(candidate.target_refs.begin(), candidate.target_refs.end(),
                                         id) != candidate.target_refs.end();
        if (is_source) {
            outgoing.push_back(ElementDetail(candidate));
        }
        if (is_target) {
            incoming.push_back(ElementDetail(candidate));
        }
    }
    result["relationships_from_here"] = std::move(outgoing);
    result["relationships_to_here"]   = std::move(incoming);

    const core::AssuranceTree tree = core::AssuranceTree::Build(model);
    if (const core::TreeNode* node = core::FindTreeNode(tree, id)) {
        result["gsn_role"] = RoleName(node->role);
        if (node->parent != nullptr) {
            result["parent_id"] = node->parent->id;
        }
        nlohmann::json children = nlohmann::json::array();
        for (const core::TreeNode* child : node->group1_children) {
            if (child != nullptr) {
                children.push_back(child->id);
            }
        }
        nlohmann::json attachments = nlohmann::json::array();
        for (const core::TreeNode* attachment : node->group2_attachments) {
            if (attachment != nullptr) {
                attachments.push_back(attachment->id);
            }
        }
        result["supported_by_ids"]  = std::move(children);
        result["in_context_of_ids"] = std::move(attachments);
    }

    return Result::Ok(std::move(result));
}

Result GetArgumentTree(const ReadContext& context, const nlohmann::json& arguments) {
    if (!HasCase(context)) {
        return NoCase();
    }

    const std::string root_id = StringArgument(arguments, "root_id");
    const int         depth   = ClampedInt(arguments, "depth", kDefaultTreeDepth, kMaxTreeDepth);

    const core::AssuranceTree tree = core::AssuranceTree::Build(context.state.loaded_case.value());

    const core::TreeNode* root = root_id.empty() ? tree.root : core::FindTreeNode(tree, root_id);
    if (root == nullptr) {
        return Result::Error(root_id.empty() ? "The case has no root goal."
                                             : ("No element with id \"" + root_id + "\"."));
    }

    nlohmann::json result{{"depth", depth}, {"tree", TreeNodeJson(*root, depth)}};
    if (root_id.empty() && !tree.orphans.empty()) {
        nlohmann::json orphans = nlohmann::json::array();
        for (const core::TreeNode* orphan : tree.orphans) {
            if (orphan != nullptr) {
                orphans.push_back(orphan->id);
            }
        }
        result["orphan_ids"] = std::move(orphans);
    }
    return Result::Ok(std::move(result));
}

Result ListCaseFiles(const ReadContext& context) {
    if (!context.state.current_project.has_value()) {
        return Result::Error("This is a standalone SACM file, so there is no project holding "
                             "other argument files.");
    }

    const core::AssuranceProject& project = context.state.current_project.value();
    const std::string             loaded  = context.state.loaded_file_path.generic_string();

    nlohmann::json files = nlohmann::json::array();
    for (const core::ProjectFileEntry& entry : project.files) {
        if (entry.role != core::ProjectFileRole::SacmArgument) {
            continue;
        }
        const std::filesystem::path absolute = project.rootPath / entry.relativePath;
        files.push_back(nlohmann::json{
            {"path", entry.relativePath.generic_string()},
            {"loaded", absolute.generic_string() == loaded},
        });
    }
    return Result::Ok(nlohmann::json{
        {"case_files", std::move(files)},
        {"note", "Use open_case_file to switch which one the other tools read."}});
}

} // namespace agent
