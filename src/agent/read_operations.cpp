#include "agent/operations.h"

#include "core/acp/assurance_claim_point.h"
#include "core/assurance_tree.h"
#include "core/reviews/review_proposal.h"
#include "parser/model_utils.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

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

// The non-primary languages this element actually carries text in. Returned so
// an agent asked to translate a case can tell which elements still need it
// instead of re-translating what a human already wrote. Absent when the element
// is monolingual, so a case with no translations costs nothing to read.
std::vector<std::string> TranslatedLanguages(const parser::SacmElement& element) {
    std::set<std::string> languages;
    auto collect = [&languages](const std::map<std::string, std::string>& texts) {
        for (const std::pair<const std::string, std::string>& entry : texts) {
            if (entry.first != core::reviews::kPatchPrimaryLanguage && !entry.second.empty())
                languages.insert(entry.first);
        }
    };
    collect(element.name_langs);
    collect(element.content_langs);
    collect(element.description_langs);
    return std::vector<std::string>(languages.begin(), languages.end());
}

// `query` is already lowercased by the caller. Lowercasing is ASCII-only and
// leaves Japanese untouched, which is harmless: Japanese has no case.
bool MatchesTranslation(const parser::SacmElement& element, const std::string& query) {
    auto contains = [&query](const std::map<std::string, std::string>& texts) {
        for (const std::pair<const std::string, std::string>& entry : texts) {
            if (entry.first == core::reviews::kPatchPrimaryLanguage)
                continue;
            if (Lowercased(entry.second).find(query) != std::string::npos)
                return true;
        }
        return false;
    };
    return contains(element.name_langs) || contains(element.content_langs) || contains(element.description_langs);
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
    const std::vector<std::string> languages = TranslatedLanguages(element);
    if (!languages.empty()) {
        summary["translated_languages"] = languages;
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
    // The secondary-language text itself, per field, so an agent revising a
    // translated claim can see what the other language currently says rather
    // than overwriting a human translator's wording sight unseen.
    nlohmann::json translations = nlohmann::json::object();
    auto add_field = [&translations](const char* field, const std::map<std::string, std::string>& texts) {
        nlohmann::json by_language = nlohmann::json::object();
        for (const std::pair<const std::string, std::string>& entry : texts) {
            if (entry.first != core::reviews::kPatchPrimaryLanguage && !entry.second.empty())
                by_language[entry.first] = entry.second;
        }
        if (!by_language.empty())
            translations[field] = std::move(by_language);
    };
    add_field("name", element.name_langs);
    add_field("content", element.content_langs);
    add_field("description", element.description_langs);
    if (!translations.empty()) {
        detail["translations"] = std::move(translations);
    }
    return detail;
}

// `instantiated` is core::acp::IsInstantiated's answer, computed through the
// same function rather than re-derived here, so this surface and the ACP panel
// cannot drift apart. The panel's further warnings -- a top-goal reference with
// no confidence package behind it, for instance -- are problem diagnostics
// layered on top of instantiation, not part of it.
bool RecordIsInstantiated(const parser::AcpRecord& record) {
    core::acp::Acp acp;
    acp.resolution.kind = core::acp::AcpResolutionKindFromString(record.resolution_kind);
    return core::acp::IsInstantiated(acp);
}

// One assurance claim point, in the vocabulary the ACP panel uses. Empty
// resolution fields are omitted rather than sent as "", so an agent reads
// "this ACP has no confidence argument yet" from absence, the same way a
// human reads the panel.
nlohmann::json AcpJson(const parser::AcpRecord& record) {
    nlohmann::json serialized{
        {"id", record.id},
        {"target_kind", record.target_kind},
        {"target_id", record.target_id},
        {"resolution_kind", record.resolution_kind},
        // GSN v3 requires an ACP to be developed somewhere; one without a
        // resolution is the uninstantiated state the ACP panel warns about.
        {"instantiated", RecordIsInstantiated(record)},
    };
    if (!record.name.empty()) {
        serialized["name"] = record.name;
    }
    if (!record.text.empty()) {
        serialized["text"] = record.text;
    }
    if (!record.confidence_claim_id.empty()) {
        serialized["confidence_claim_id"] = record.confidence_claim_id;
    }
    if (!record.argument_package_id.empty()) {
        serialized["confidence_argument_package_id"] = record.argument_package_id;
    }
    if (!record.top_goal_id.empty()) {
        serialized["confidence_top_goal_id"] = record.top_goal_id;
    }
    return serialized;
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
        serialized["child_count"] = static_cast<int>(node.group1_children.size() + node.group2_attachments.size());
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
    return context.argument() != nullptr;
}

Result NoCase() {
    return Result::Error("No assurance case is loaded for this project.");
}

std::string ArgumentFile(const ReadContext& context) {
    std::filesystem::path argument = context.state.loaded_file_path;
    if (context.draft_workspace != nullptr && !context.draft_workspace->argument_file.empty())
        argument = context.draft_workspace->argument_file;
    if (argument.is_relative())
        return argument.generic_string();
    if (context.state.current_project.has_value()) {
        std::error_code error;
        const std::filesystem::path relative =
            std::filesystem::relative(argument, context.state.current_project->rootPath, error);
        if (!error && !relative.empty())
            return relative.generic_string();
    }
    return argument.generic_string();
}

} // namespace

Result Result::Ok(nlohmann::json payload) {
    return Result{std::move(payload), false};
}

Result Result::Error(std::string message) {
    return Result{nlohmann::json{{"error", std::move(message)}}, true};
}

Result ReadResult(const ReadContext& context, nlohmann::json payload) {
    const core::drafts::DraftWorkspace* workspace = context.draft_workspace;
    payload["argument_file"] = ArgumentFile(context);
    if (context.context_generation > 0)
        payload["context_generation"] = context.context_generation;
    if (workspace != nullptr) {
        payload["view"] = context.is_working_draft ? "working_draft" : "accepted";
        payload["workspace_id"] = workspace->id;
        payload["working_revision"] = workspace->working_revision;
        payload["workspace_state"] = core::drafts::DraftWorkspaceStateToString(workspace->state);
    } else {
        payload["view"] = "accepted";
        if (context.argument_view != nullptr)
            payload["working_revision"] = 0;
    }
    return Result::Ok(std::move(payload));
}

Result GetCaseOverview(const ReadContext& context) {
    if (!HasCase(context)) {
        return NoCase();
    }
    const parser::AssuranceCase& model = *context.argument();

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
    return ReadResult(context, std::move(overview));
}

Result FindElements(const ReadContext& context, const nlohmann::json& arguments) {
    if (!HasCase(context)) {
        return NoCase();
    }

    const std::string query = Lowercased(StringArgument(arguments, "query"));
    const std::string type = Lowercased(StringArgument(arguments, "type"));
    const int limit = ClampedInt(arguments, "limit", kDefaultResultLimit, kMaxResultLimit);

    nlohmann::json matches = nlohmann::json::array();
    int total = 0;
    for (const parser::SacmElement& element : context.argument()->elements) {
        if (!type.empty() && Lowercased(element.type) != type) {
            continue;
        }
        if (!query.empty()) {
            const bool hit = Lowercased(element.id).find(query) != std::string::npos ||
                             Lowercased(element.name).find(query) != std::string::npos ||
                             Lowercased(element.content).find(query) != std::string::npos ||
                             Lowercased(element.description).find(query) != std::string::npos ||
                             // A bilingual case is searchable in either language.
                             // Searching only the primary would report that a
                             // claim does not exist because the user asked for it
                             // in the language the claim is not indexed in.
                             MatchesTranslation(element, query);
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
    return ReadResult(context,
                      nlohmann::json{
                          {"matches", std::move(matches)},
                          {"returned", returned},
                          {"total_matches", total},
                          {"limit", limit},
                          {"truncated", total > returned},
                      });
}

Result ListTerms(const ReadContext& context) {
    if (!HasCase(context)) {
        return NoCase();
    }

    nlohmann::json terms = nlohmann::json::array();
    for (const parser::SacmElement& element : context.argument()->elements) {
        if (element.type != "term") {
            continue;
        }
        // Term-domain names rather than the flat model's: `content` holds the
        // value and `description` the definition, which is projection detail an
        // agent should not need to know.
        nlohmann::json term{{"id", element.id}, {"value", element.content}};
        if (!element.name.empty()) {
            term["name"] = element.name;
        }
        if (!element.description.empty()) {
            term["definition"] = element.description;
        }
        nlohmann::json definition_translations = nlohmann::json::object();
        for (const std::pair<const std::string, std::string>& entry : element.description_langs) {
            if (entry.first != core::reviews::kPatchPrimaryLanguage && !entry.second.empty()) {
                definition_translations[entry.first] = entry.second;
            }
        }
        if (!definition_translations.empty()) {
            term["definition_translations"] = std::move(definition_translations);
        }
        terms.push_back(std::move(term));
    }

    const int count = static_cast<int>(terms.size());
    nlohmann::json payload{{"terms", std::move(terms)}, {"count", count}};
    if (count == 0) {
        payload["note"] = "This case defines no terminology yet. Terms bound the words a safety argument "
                          "relies on; stage a CreateTerm operation to define one.";
    }
    return ReadResult(context, std::move(payload));
}

Result ListAssuranceClaimPoints(const ReadContext& context) {
    if (!HasCase(context)) {
        return NoCase();
    }
    const parser::AssuranceCase& model = *context.argument();

    nlohmann::json points = nlohmann::json::array();
    for (const parser::AcpRecord& record : model.acps) {
        points.push_back(AcpJson(record));
    }
    const int count = static_cast<int>(points.size());
    return ReadResult(context,
                      nlohmann::json{
                          {"assurance_claim_points", std::move(points)},
                          {"count", count},
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

    const parser::AssuranceCase& model = *context.argument();
    const parser::SacmElement* element = parser::FindElementById(model, id);
    if (element == nullptr) {
        return Result::Error("No element with id \"" + id + "\".");
    }

    nlohmann::json result{{"element", ElementDetail(*element)}};

    // Relationships are elements in this projection, so the neighbourhood is
    // assembled by scanning them rather than by following pointers.
    nlohmann::json incoming = nlohmann::json::array();
    nlohmann::json outgoing = nlohmann::json::array();
    std::unordered_set<std::string> touching_relationship_ids;
    for (const parser::SacmElement& candidate : model.elements) {
        if (!IsRelationship(candidate)) {
            continue;
        }
        const bool is_source =
            std::find(candidate.source_refs.begin(), candidate.source_refs.end(), id) != candidate.source_refs.end();
        const bool is_target =
            std::find(candidate.target_refs.begin(), candidate.target_refs.end(), id) != candidate.target_refs.end();
        if (is_source) {
            outgoing.push_back(ElementDetail(candidate));
        }
        if (is_target) {
            incoming.push_back(ElementDetail(candidate));
        }
        if (is_source || is_target) {
            touching_relationship_ids.insert(candidate.id);
        }
    }
    result["relationships_from_here"] = std::move(outgoing);
    result["relationships_to_here"] = std::move(incoming);

    // Assurance claim points on this element, and on the relationships that
    // touch it. An agent asked "is this claim's support assured?" needs the
    // ACP on the SupportedBy as much as one on the element itself; a
    // relationship-borne entry names the relationship it rides on. The kind is
    // checked alongside the id, so a record can never attach through the wrong
    // kind of target even if an element and a relationship shared an id.
    nlohmann::json claim_points = nlohmann::json::array();
    for (const parser::AcpRecord& record : model.acps) {
        if (record.target_kind == "element" && record.target_id == id) {
            claim_points.push_back(AcpJson(record));
            continue;
        }
        if (record.target_kind == "relationship" && touching_relationship_ids.count(record.target_id) > 0) {
            nlohmann::json entry = AcpJson(record);
            entry["via_relationship_id"] = record.target_id;
            claim_points.push_back(std::move(entry));
        }
    }
    if (!claim_points.empty()) {
        result["assurance_claim_points"] = std::move(claim_points);
    }

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
        result["supported_by_ids"] = std::move(children);
        result["in_context_of_ids"] = std::move(attachments);
    }

    return ReadResult(context, std::move(result));
}

Result GetArgumentTree(const ReadContext& context, const nlohmann::json& arguments) {
    if (!HasCase(context)) {
        return NoCase();
    }

    const std::string root_id = StringArgument(arguments, "root_id");
    const int depth = ClampedInt(arguments, "depth", kDefaultTreeDepth, kMaxTreeDepth);

    const core::AssuranceTree tree = core::AssuranceTree::Build(*context.argument());

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
    return ReadResult(context, std::move(result));
}

Result ListCaseFiles(const ReadContext& context) {
    if (!context.state.current_project.has_value()) {
        return Result::Error("This is a standalone SACM file, so there is no project holding "
                             "other argument files.");
    }

    const core::AssuranceProject& project = context.state.current_project.value();
    const std::string loaded = context.state.loaded_file_path.generic_string();

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
    return Result::Ok(nlohmann::json{{"case_files", std::move(files)},
                                     {"note", "Use open_case_file to switch which one the other tools read."}});
}

} // namespace agent
