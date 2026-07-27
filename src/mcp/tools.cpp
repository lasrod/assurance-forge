#include "mcp/tools.h"

#include "mcp/session.h"

#include "core/assurance_tree.h"
#include "core/reviews/review_proposal.h"
#include "core/reviews/review_proposal_factory.h"
#include "core/reviews/review_proposal_manager.h"
#include "core/reviews/review_proposal_patch_service.h"
#include "core/time_utils.h"
#include "parser/model_utils.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>

namespace mcp {
namespace {

// A chat client pays for every token a tool returns, and a large safety case
// does not fit in a context window. Limits here are a correctness requirement,
// not a nicety: an unbounded find_elements on a real case would exhaust the
// conversation on one call.
constexpr int kDefaultResultLimit = 50;
constexpr int kMaxResultLimit     = 200;
constexpr int kDefaultTreeDepth   = 4;
constexpr int kMaxTreeDepth       = 12;

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
    if (!element.name_langs.empty()) {
        detail["name_languages"] = element.name_langs;
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

ToolResult RequireCase(Session& session) {
    if (!session.has_case()) {
        return ToolResult::Error("No assurance case is loaded for this project.");
    }
    return ToolResult::Ok(nlohmann::json::object());
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

ToolResult GetCaseOverview(Session& session, const nlohmann::json&) {
    const ToolResult guard = RequireCase(session);
    if (guard.is_error) {
        return guard;
    }
    const parser::AssuranceCase& model = session.assurance_case();

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
        {"project_path", session.project_path().string()},
        // Which file the case came from. A project can hold several argument
        // files and the session opens the first, so an agent reasoning about
        // "the case" should be able to see which one that was.
        {"loaded_file", session.state().loaded_file_path.generic_string()},
    };
    if (!model.description.empty()) {
        overview["case_description"] = model.description;
    }
    if (!session.state().load_warnings.empty()) {
        // Surfaced rather than swallowed: a case can load successfully and still
        // not be a conformant interchange document, and an agent reasoning about
        // the argument should know that before it proposes anything.
        overview["load_warnings"] = session.state().load_warnings;
    }
    return ToolResult::Ok(std::move(overview));
}

ToolResult FindElements(Session& session, const nlohmann::json& arguments) {
    const ToolResult guard = RequireCase(session);
    if (guard.is_error) {
        return guard;
    }

    const std::string query = Lowercased(StringArgument(arguments, "query"));
    const std::string type  = Lowercased(StringArgument(arguments, "type"));
    const int         limit = ClampedInt(arguments, "limit", kDefaultResultLimit, kMaxResultLimit);

    nlohmann::json matches = nlohmann::json::array();
    int            total   = 0;
    for (const parser::SacmElement& element : session.assurance_case().elements) {
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
    return ToolResult::Ok(nlohmann::json{
        {"matches", std::move(matches)},
        {"returned", returned},
        {"total_matches", total},
        {"limit", limit},
        {"truncated", total > returned},
    });
}

ToolResult GetElement(Session& session, const nlohmann::json& arguments) {
    const ToolResult guard = RequireCase(session);
    if (guard.is_error) {
        return guard;
    }

    const std::string id = StringArgument(arguments, "id");
    if (id.empty()) {
        return ToolResult::Error("Argument \"id\" is required.");
    }

    const parser::AssuranceCase&  model   = session.assurance_case();
    const parser::SacmElement*    element = parser::FindElementById(model, id);
    if (element == nullptr) {
        return ToolResult::Error("No element with id \"" + id + "\".");
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

    return ToolResult::Ok(std::move(result));
}

ToolResult GetArgumentTree(Session& session, const nlohmann::json& arguments) {
    const ToolResult guard = RequireCase(session);
    if (guard.is_error) {
        return guard;
    }

    const std::string root_id = StringArgument(arguments, "root_id");
    const int         depth   = ClampedInt(arguments, "depth", kDefaultTreeDepth, kMaxTreeDepth);

    const core::AssuranceTree tree = core::AssuranceTree::Build(session.assurance_case());

    const core::TreeNode* root = root_id.empty() ? tree.root : core::FindTreeNode(tree, root_id);
    if (root == nullptr) {
        return ToolResult::Error(root_id.empty()
                                     ? "The case has no root goal."
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
    return ToolResult::Ok(std::move(result));
}

// ---------------------------------------------------------------------------
// Proposing changes
//
// The MCP server never edits the case. It writes ReviewProposal drafts, which a
// human accepts in the GUI -- the same patch format, staleness detection and
// apply path the in-app review flow already uses. An agent therefore cannot make
// a change the user did not look at, and cannot make one the GUI could not.
// ---------------------------------------------------------------------------

// `{"id": "G1"}` names an element already in the case; `{"ref": "$goal"}` names
// one this same proposal creates. Exactly one: a reference carrying both is
// ambiguous, and one carrying neither cannot be resolved.
bool ParseElementRef(const nlohmann::json& source, const char* field,
                     std::optional<core::reviews::ElementRef>& out, std::string& error) {
    const nlohmann::json::const_iterator found = source.find(field);
    if (found == source.end() || found->is_null()) {
        return true; // Absent is fine; the operation type decides what it needs.
    }
    if (!found->is_object()) {
        error = std::string(field) + " must be an object like {\"id\": \"G1\"} or {\"ref\": \"$goal\"}.";
        return false;
    }

    const nlohmann::json::const_iterator id  = found->find("id");
    const nlohmann::json::const_iterator ref = found->find("ref");
    const bool has_id  = id != found->end() && id->is_string() && !id->get<std::string>().empty();
    const bool has_ref = ref != found->end() && ref->is_string() && !ref->get<std::string>().empty();
    if (has_id == has_ref) {
        error = std::string(field) + " must carry exactly one of \"id\" (an existing element) or "
                                     "\"ref\" (one this proposal creates).";
        return false;
    }

    core::reviews::ElementRef parsed;
    if (has_id) {
        parsed.existing_id = id->get<std::string>();
    } else {
        parsed.create_ref = ref->get<std::string>();
    }
    out = parsed;
    return true;
}

bool ParsePatchOperation(const nlohmann::json& source, core::reviews::PatchOperation& out,
                         std::string& error) {
    if (!source.is_object()) {
        error = "Each operation must be an object.";
        return false;
    }
    const std::string type = StringArgument(source, "type");
    if (type.empty()) {
        error = "Each operation needs a \"type\".";
        return false;
    }
    if (!core::reviews::PatchOperationTypeFromString(type, out.type)) {
        error = "Unknown operation type \"" + type + "\".";
        return false;
    }

    if (!ParseElementRef(source, "element", out.element, error) ||
        !ParseElementRef(source, "source", out.source, error) ||
        !ParseElementRef(source, "target", out.target, error)) {
        return false;
    }

    const std::string create_ref = StringArgument(source, "create_ref");
    if (!create_ref.empty()) {
        out.create_ref = create_ref;
    }
    out.field     = StringArgument(source, "field");
    out.old_value = StringArgument(source, "old_value");
    out.new_value = StringArgument(source, "new_value");
    out.text      = StringArgument(source, "text");
    return true;
}

struct ProposalBuild {
    core::reviews::ReviewProposal proposal;
    std::string                   error;
};

ProposalBuild BuildProposal(Session& session, const nlohmann::json& arguments) {
    ProposalBuild build;

    const std::string title = StringArgument(arguments, "title");
    if (title.empty()) {
        build.error = "Argument \"title\" is required.";
        return build;
    }
    const nlohmann::json::const_iterator operations = arguments.find("operations");
    if (operations == arguments.end() || !operations->is_array() || operations->empty()) {
        build.error = "Argument \"operations\" must be a non-empty array.";
        return build;
    }

    core::reviews::ReviewProposal& proposal = build.proposal;
    proposal.id                = core::reviews::GenerateReviewProposalId();
    proposal.title             = title;
    proposal.summary           = StringArgument(arguments, "summary");
    proposal.created_utc       = core::NowUtcString();
    proposal.anchor_element_id = StringArgument(arguments, "anchor_element_id");
    // Attribution the GUI shows and the audit trail keeps, so an accepted change
    // is traceable to the client that proposed it rather than to "the AI".
    proposal.author_name = "MCP: " + session.client_label();

    for (const nlohmann::json& operation : *operations) {
        core::reviews::PatchOperation parsed;
        if (!ParsePatchOperation(operation, parsed, build.error)) {
            return build;
        }
        proposal.operations.push_back(std::move(parsed));
    }

    // Every existing element the patch reaches is declared as affected and gets a
    // base hash. Collected here rather than trusted from the caller: an agent
    // that under-declares would be handed weaker staleness checking than it
    // deserves, which is the failure that lets a stale patch apply cleanly.
    std::set<std::string> affected;
    if (!proposal.anchor_element_id.empty()) {
        affected.insert(proposal.anchor_element_id);
    }
    for (const core::reviews::PatchOperation& operation : proposal.operations) {
        for (const std::optional<core::reviews::ElementRef>* ref :
             {&operation.element, &operation.source, &operation.target}) {
            if (ref->has_value() && (*ref)->existing_id.has_value() &&
                !(*ref)->existing_id->empty()) {
                affected.insert(*(*ref)->existing_id);
            }
        }
    }

    const parser::AssuranceCase& model = session.assurance_case();
    proposal.base_model_hash           = core::reviews::ComputeModelSemanticHash(model);
    for (const std::string& id : affected) {
        const parser::SacmElement* element = parser::FindElementById(model, id);
        if (element == nullptr) {
            build.error = "Operation references unknown element \"" + id + "\".";
            return build;
        }
        proposal.affected_existing_element_ids.push_back(id);
        proposal.base_element_hashes[id] = core::reviews::ComputeElementSemanticHash(*element);
    }
    return build;
}

struct ProposalCheck {
    bool           ok = false;
    std::string    error;
    nlohmann::json effects;
};

// Validity plus a dry run. Both the preview and the create path use this, so a
// proposal that cannot apply is refused rather than written to disk for a human
// to discover later.
ProposalCheck CheckProposal(Session& session, const core::reviews::ReviewProposal& proposal) {
    ProposalCheck check;
    const parser::AssuranceCase& model = session.assurance_case();

    const core::reviews::ProposalValidityResult validity =
        core::reviews::EvaluateReviewProposalValidity(proposal, model);
    if (validity.validity != core::reviews::ProposalValidity::Valid) {
        check.error = "Proposal is not valid: " + validity.reason;
        return check;
    }

    const core::reviews::ReviewProposalPatchService service;
    const core::reviews::ProposalPreviewResult      preview =
        service.BuildPreviewModel(proposal, model);
    if (!preview.success) {
        check.error = "Proposal could not be applied: " + preview.error;
        return check;
    }

    nlohmann::json created = nlohmann::json::object();
    for (const std::pair<const std::string, std::string>& entry : preview.generated_ids) {
        created[entry.first] = entry.second;
    }

    check.ok      = true;
    check.effects = nlohmann::json{
        {"created_element_ids", std::move(created)},
        {"element_count_before", static_cast<int>(model.elements.size())},
        {"element_count_after", static_cast<int>(preview.preview_model.elements.size())},
        {"operation_count", static_cast<int>(proposal.operations.size())},
    };
    return check;
}

ToolResult PreviewReviewProposal(Session& session, const nlohmann::json& arguments) {
    const ToolResult guard = RequireCase(session);
    if (guard.is_error) {
        return guard;
    }

    ProposalBuild build = BuildProposal(session, arguments);
    if (!build.error.empty()) {
        return ToolResult::Error(build.error);
    }
    const ProposalCheck check = CheckProposal(session, build.proposal);
    if (!check.ok) {
        return ToolResult::Error(check.error);
    }

    nlohmann::json result = check.effects;
    result["would_apply"]  = true;
    result["saved"]        = false;
    result["note"]         = "Nothing was written. Call create_review_proposal to save this as a "
                             "draft for the user to review.";
    return ToolResult::Ok(std::move(result));
}

ToolResult CreateReviewProposal(Session& session, const nlohmann::json& arguments) {
    const ToolResult guard = RequireCase(session);
    if (guard.is_error) {
        return guard;
    }
    if (!session.has_project()) {
        return ToolResult::Error(
            "Proposals are stored in the project directory, but this session opened a standalone "
            "SACM file. Point --project at the project to propose changes.");
    }

    ProposalBuild build = BuildProposal(session, arguments);
    if (!build.error.empty()) {
        return ToolResult::Error(build.error);
    }
    const ProposalCheck check = CheckProposal(session, build.proposal);
    if (!check.ok) {
        return ToolResult::Error(check.error);
    }

    std::filesystem::path relative_path;
    std::string           error;
    if (!session.proposals().SaveProposal(build.proposal, &relative_path, error)) {
        return ToolResult::Error("Could not save the proposal: " + error);
    }
    // The GUI lists proposals from a cached directory scan; without this it would
    // not show the new draft until its poll interval elapsed.
    session.proposals().InvalidateProposalCache();

    nlohmann::json result = check.effects;
    result["proposal_id"]  = build.proposal.id;
    result["path"]         = relative_path.generic_string();
    result["author"]       = build.proposal.author_name;
    result["saved"]        = true;
    result["note"]         = "Saved as a draft. It changes nothing until the user reviews and "
                             "accepts it in Assurance Forge.";
    return ToolResult::Ok(std::move(result));
}

ToolResult ListReviewProposals(Session& session, const nlohmann::json&) {
    const ToolResult guard = RequireCase(session);
    if (guard.is_error) {
        return guard;
    }
    if (!session.has_project()) {
        return ToolResult::Error("This session opened a standalone SACM file, which has no "
                                 "project directory to hold proposals.");
    }

    nlohmann::json listed = nlohmann::json::array();
    for (const core::reviews::ReviewProposalSummary& summary :
         session.proposals().ListProposals(&session.assurance_case())) {
        nlohmann::json entry{
            {"id", summary.id},
            {"title", summary.title},
            {"path", summary.relative_path.generic_string()},
            {"valid", summary.validity.validity == core::reviews::ProposalValidity::Valid},
        };
        if (!summary.summary.empty()) {
            entry["summary"] = summary.summary;
        }
        if (!summary.anchor_element_id.empty()) {
            entry["anchor_element_id"] = summary.anchor_element_id;
        }
        // A proposal goes stale when the case moves under it. Reported so an
        // agent revises rather than re-proposing something already broken.
        if (!summary.validity.reason.empty()) {
            entry["invalid_reason"] = summary.validity.reason;
        }
        listed.push_back(std::move(entry));
    }
    // Counted before the move, for the same reason as in find_elements: braced
    // initializers evaluate left to right, so reading `listed.size()` after
    // `std::move(listed)` reliably reports zero.
    const int count = static_cast<int>(listed.size());
    return ToolResult::Ok(nlohmann::json{{"proposals", std::move(listed)}, {"count", count}});
}

ToolResult GetReviewProposal(Session& session, const nlohmann::json& arguments) {
    const ToolResult guard = RequireCase(session);
    if (guard.is_error) {
        return guard;
    }
    if (!session.has_project()) {
        return ToolResult::Error("This session opened a standalone SACM file, which has no "
                                 "project directory to hold proposals.");
    }
    const std::string id = StringArgument(arguments, "id");
    if (id.empty()) {
        return ToolResult::Error("Argument \"id\" is required.");
    }

    std::string                                         error;
    const std::optional<core::reviews::ReviewProposal> loaded =
        session.proposals().LoadProposal(id, error);
    if (!loaded.has_value()) {
        return ToolResult::Error("Could not load proposal \"" + id + "\": " + error);
    }

    const core::reviews::ProposalValidityResult validity =
        core::reviews::EvaluateReviewProposalValidity(*loaded, session.assurance_case());

    nlohmann::json result = nlohmann::json::parse(core::reviews::SerializeReviewProposal(*loaded),
                                                  nullptr, /*allow_exceptions=*/false);
    return ToolResult::Ok(nlohmann::json{
        {"proposal", result.is_discarded() ? nlohmann::json::object() : std::move(result)},
        {"valid", validity.validity == core::reviews::ProposalValidity::Valid},
        {"invalid_reason", validity.reason},
    });
}

std::vector<ToolDefinition> BuildTools() {
    std::vector<ToolDefinition> tools;

    tools.push_back(ToolDefinition{
        "get_case_overview",
        "Summarize the loaded assurance case: name, element counts by SACM type, "
        "assurance claim points, and any warnings raised when the file was loaded.",
        nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}},
        true,
        &GetCaseOverview,
    });

    tools.push_back(ToolDefinition{
        "find_elements",
        "Search the assurance case for elements whose id, name, content or description "
        "contains a substring. Case-insensitive. Optionally filter by SACM type.",
        nlohmann::json{
            {"type", "object"},
            {"properties",
             {{"query", {{"type", "string"}, {"description", "Case-insensitive substring."}}},
              {"type",
               {{"type", "string"},
                {"description", "SACM type filter, e.g. \"claim\", \"argumentreasoning\"."}}},
              {"limit",
               {{"type", "integer"},
                {"description", "Maximum elements to return (default 50, maximum 200)."}}}}}},
        true,
        &FindElements,
    });

    tools.push_back(ToolDefinition{
        "get_element",
        "Fetch one element by id with its full fields, its incoming and outgoing SACM "
        "relationships, and its GSN role and immediate neighbours.",
        nlohmann::json{{"type", "object"},
                       {"properties",
                        {{"id", {{"type", "string"}, {"description", "Element id."}}}}},
                       {"required", nlohmann::json::array({"id"})}},
        true,
        &GetElement,
    });

    tools.push_back(ToolDefinition{
        "get_argument_tree",
        "Return the GSN argument structure as a nested tree of supported-by and "
        "in-context-of links. Truncated branches are marked so a partial tree is never "
        "mistaken for a complete one.",
        nlohmann::json{
            {"type", "object"},
            {"properties",
             {{"root_id",
               {{"type", "string"},
                {"description", "Element to root the tree at. Defaults to the case's top goal."}}},
              {"depth",
               {{"type", "integer"},
                {"description", "Levels to descend (default 4, maximum 12)."}}}}}},
        true,
        &GetArgumentTree,
    });

    // Shared by preview and create so the two can never drift apart on what they
    // accept -- an agent that got a clean preview must be able to save it.
    const nlohmann::json proposal_schema{
        {"type", "object"},
        {"properties",
         {{"title", {{"type", "string"}, {"description", "Short title shown to the user."}}},
          {"summary", {{"type", "string"}, {"description", "What the change does and why."}}},
          {"anchor_element_id",
           {{"type", "string"},
            {"description", "Element the proposal is about. Omit only when the proposal creates "
                            "elements and touches nothing existing, such as a first top goal."}}},
          {"operations",
           {{"type", "array"},
            {"description", "Patch operations applied in order."},
            {"items",
             {{"type", "object"},
              {"properties",
               {{"type",
                 {{"type", "string"},
                  {"enum", nlohmann::json::array({"CreateClaim", "CreateStrategy", "CreateSolution",
                                                  "CreateContext", "CreateAssumption",
                                                  "CreateJustification", "UpdateElementText",
                                                  "UpdateElementName", "SetUndeveloped",
                                                  "ClearUndeveloped", "AddSupportedBy",
                                                  "RemoveSupportedBy", "AddInContextOf",
                                                  "RemoveInContextOf", "RemoveElement"})}}},
                {"create_ref",
                 {{"type", "string"},
                  {"description",
                   "For Create* operations: a patch-local name starting with '$', which later "
                   "operations use as {\"ref\": \"$name\"}."}}},
                {"element",
                 {{"type", "object"},
                  {"description",
                   "Target of an update or removal: {\"id\": \"G1\"} or {\"ref\": \"$goal\"}."}}},
                {"source", {{"type", "object"}, {"description", "Relationship source."}}},
                {"target", {{"type", "object"}, {"description", "Relationship target."}}},
                {"field",
                 {{"type", "string"},
                  {"description", "For UpdateElementText: which field, e.g. \"content\"."}}},
                {"old_value", {{"type", "string"}}},
                {"new_value", {{"type", "string"}}},
                {"text",
                 {{"type", "string"},
                  {"description", "Initial text for a Create* operation."}}}}}}}}}}},
        {"required", nlohmann::json::array({"title", "operations"})}};

    tools.push_back(ToolDefinition{
        "preview_review_proposal",
        "Dry-run a set of patch operations against the case and report what they would do, "
        "writing nothing. Use this to check a patch before saving it.",
        proposal_schema,
        true,
        &PreviewReviewProposal,
    });

    tools.push_back(ToolDefinition{
        "create_review_proposal",
        "Save a set of patch operations as a review proposal for the user to accept or reject in "
        "Assurance Forge. This does NOT change the safety case: a proposal is a draft, and only "
        "the user can apply it. Refuses anything that would not apply cleanly.",
        proposal_schema,
        true,
        &CreateReviewProposal,
    });

    tools.push_back(ToolDefinition{
        "list_review_proposals",
        "List saved review proposals for this project, including whether each still applies "
        "cleanly to the current case.",
        nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}},
        true,
        &ListReviewProposals,
    });

    tools.push_back(ToolDefinition{
        "get_review_proposal",
        "Fetch one saved review proposal in full by id, with its current validity.",
        nlohmann::json{{"type", "object"},
                       {"properties",
                        {{"id", {{"type", "string"}, {"description", "Proposal id."}}}}},
                       {"required", nlohmann::json::array({"id"})}},
        true,
        &GetReviewProposal,
    });

    return tools;
}

} // namespace

ToolResult ToolResult::Ok(nlohmann::json payload) {
    return ToolResult{std::move(payload), false};
}

ToolResult ToolResult::Error(std::string message) {
    return ToolResult{nlohmann::json{{"error", std::move(message)}}, true};
}

const std::vector<ToolDefinition>& BuiltinTools() {
    static const std::vector<ToolDefinition> tools = BuildTools();
    return tools;
}

const ToolDefinition* FindTool(std::string_view name) {
    for (const ToolDefinition& tool : BuiltinTools()) {
        if (tool.name == name) {
            return &tool;
        }
    }
    return nullptr;
}

} // namespace mcp
