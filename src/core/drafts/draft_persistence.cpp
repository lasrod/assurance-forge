#include "core/drafts/draft_persistence.h"

#include "core/project_file_io.h"
#include "core/reviews/review_proposal.h"

#include <nlohmann/json.hpp>

#include <sstream>

namespace core::drafts {
namespace {

// Operations are written through the review-proposal serializer rather than a
// second implementation of the same format. The vocabulary is shared with
// proposals and with the audit log, and two writers of one format drift.
nlohmann::json OperationsToJson(const DraftChangeGroup& group) {
    reviews::ReviewProposal proposal;
    proposal.id = group.id;
    proposal.operations = group.operations;
    nlohmann::json serialized = nlohmann::json::parse(reviews::SerializeReviewProposal(proposal));
    return serialized.value("operations", nlohmann::json::array());
}

bool OperationsFromJson(const nlohmann::json& operations,
                        std::vector<reviews::PatchOperation>& out,
                        std::string& error) {
    nlohmann::json envelope = nlohmann::json::object();
    envelope["schema"] = reviews::kReviewProposalSchema;
    envelope["operations"] = operations;

    reviews::ReviewProposal proposal;
    if (!reviews::DeserializeReviewProposal(envelope.dump(), proposal, error))
        return false;
    out = std::move(proposal.operations);
    return true;
}

nlohmann::json GroupToJson(const DraftChangeGroup& group) {
    nlohmann::json object;
    object["id"] = group.id;
    object["sequence"] = group.sequence;
    object["title"] = group.title;
    object["summary"] = group.summary;
    object["rationale"] = group.rationale;
    object["source"] = DraftSourceToString(group.source);
    object["source_label"] = group.source_label;
    object["source_session_id"] = group.source_session_id;
    object["created_utc"] = group.created_utc;
    object["updated_utc"] = group.updated_utc;
    object["state"] = DraftGroupStateToString(group.state);
    object["operations"] = OperationsToJson(group);
    object["generated_ids"] = group.generated_ids;
    object["guideline_ids"] = group.guideline_ids;
    object["review_item_ids"] = group.review_item_ids;
    object["depends_on_group_ids"] = group.depends_on_group_ids;
    return object;
}

bool GroupFromJson(const nlohmann::json& object, DraftChangeGroup& group, std::string& error) {
    if (!object.is_object()) {
        error = "Draft change group is not an object.";
        return false;
    }
    group = DraftChangeGroup{};
    group.id = object.value("id", "");
    if (group.id.empty()) {
        error = "Draft change group is missing an id.";
        return false;
    }
    group.sequence = object.value("sequence", std::uint64_t{0});
    group.title = object.value("title", "");
    group.summary = object.value("summary", "");
    group.rationale = object.value("rationale", "");

    const std::string source = object.value("source", "");
    if (!source.empty() && !DraftSourceFromString(source, group.source)) {
        error = "Draft change group has an unknown source: " + source;
        return false;
    }
    group.source_label = object.value("source_label", "");
    group.source_session_id = object.value("source_session_id", "");
    group.created_utc = object.value("created_utc", "");
    group.updated_utc = object.value("updated_utc", "");

    const std::string state = object.value("state", "");
    if (!state.empty() && !DraftGroupStateFromString(state, group.state)) {
        error = "Draft change group has an unknown state: " + state;
        return false;
    }

    if (!OperationsFromJson(object.value("operations", nlohmann::json::array()), group.operations, error))
        return false;

    if (object.contains("generated_ids") && object["generated_ids"].is_object()) {
        for (auto it = object["generated_ids"].begin(); it != object["generated_ids"].end(); ++it) {
            if (it.value().is_string())
                group.generated_ids[it.key()] = it.value().get<std::string>();
        }
    }
    for (const auto& value : object.value("guideline_ids", nlohmann::json::array())) {
        if (value.is_string())
            group.guideline_ids.push_back(value.get<std::string>());
    }
    for (const auto& value : object.value("review_item_ids", nlohmann::json::array())) {
        if (value.is_string())
            group.review_item_ids.push_back(value.get<std::string>());
    }
    for (const auto& value : object.value("depends_on_group_ids", nlohmann::json::array())) {
        if (value.is_string())
            group.depends_on_group_ids.push_back(value.get<std::string>());
    }
    return true;
}

nlohmann::json EventToJson(const DraftEvent& event) {
    nlohmann::json object;
    object["revision"] = event.revision;
    object["type"] = event.type;
    object["group_id"] = event.group_id;
    object["detail"] = event.detail;
    object["created_utc"] = event.created_utc;
    return object;
}

} // namespace

std::string SerializeDraftWorkspace(const DraftWorkspace& workspace) {
    nlohmann::json root;
    root["schema"] = kDraftWorkspaceSchema;
    root["id"] = workspace.id;
    root["argument_file"] = workspace.argument_file.generic_string();
    root["base_model_hash"] = workspace.base_model_hash;
    root["working_revision"] = workspace.working_revision;
    root["next_sequence"] = workspace.next_sequence;
    root["state"] = DraftWorkspaceStateToString(workspace.state);
    if (workspace.pending_promotion.has_value()) {
        nlohmann::json pending;
        pending["group_ids"] = workspace.pending_promotion->group_ids;
        pending["expected_model_hash"] = workspace.pending_promotion->expected_model_hash;
        pending["started_utc"] = workspace.pending_promotion->started_utc;
        root["pending_promotion"] = std::move(pending);
    }
    root["groups"] = nlohmann::json::array();
    for (const DraftChangeGroup& group : workspace.groups)
        root["groups"].push_back(GroupToJson(group));
    return root.dump(2) + "\n";
}

bool DeserializeDraftWorkspace(const std::string& content, DraftWorkspace& workspace, std::string& error) {
    workspace = DraftWorkspace{};
    error.clear();
    try {
        const nlohmann::json root = nlohmann::json::parse(content);
        if (!root.is_object()) {
            error = "Draft workspace root is not an object.";
            return false;
        }
        const std::string schema = root.value("schema", "");
        if (schema != kDraftWorkspaceSchema && schema != kDraftWorkspaceSchemaV1) {
            // Refused rather than read on a best-effort basis. A draft read
            // through the wrong schema is a draft quietly altered, and the whole
            // point of this file is that unaccepted work is not altered by
            // accident.
            error = "Draft workspace uses an unsupported schema: " + (schema.empty() ? "(none)" : schema);
            return false;
        }

        workspace.id = root.value("id", "");
        workspace.argument_file = std::filesystem::path(root.value("argument_file", ""));
        workspace.base_model_hash = root.value("base_model_hash", "");
        workspace.working_revision = root.value("working_revision", std::uint64_t{0});
        workspace.next_sequence = root.value("next_sequence", std::uint64_t{1});

        const std::string state = root.value("state", "");
        if (!state.empty() && !DraftWorkspaceStateFromString(state, workspace.state)) {
            error = "Draft workspace has an unknown state: " + state;
            return false;
        }

        if (root.contains("pending_promotion")) {
            const nlohmann::json& pending = root["pending_promotion"];
            if (!pending.is_object()) {
                error = "Draft workspace pending promotion is not an object.";
                return false;
            }
            DraftPendingPromotion promotion;
            for (const auto& value : pending.value("group_ids", nlohmann::json::array())) {
                if (value.is_string())
                    promotion.group_ids.push_back(value.get<std::string>());
            }
            promotion.expected_model_hash = pending.value("expected_model_hash", "");
            promotion.started_utc = pending.value("started_utc", "");
            if (promotion.group_ids.empty() || promotion.expected_model_hash.empty()) {
                error = "Draft workspace pending promotion is incomplete.";
                return false;
            }
            workspace.pending_promotion = std::move(promotion);
        }

        const nlohmann::json groups = root.value("groups", nlohmann::json::array());
        if (!groups.is_array()) {
            error = "Draft workspace groups field is not an array.";
            return false;
        }
        for (const auto& group_json : groups) {
            DraftChangeGroup group;
            if (!GroupFromJson(group_json, group, error))
                return false;
            if (group.sequence >= workspace.next_sequence)
                workspace.next_sequence = group.sequence + 1;
            workspace.groups.push_back(std::move(group));
        }
    } catch (const nlohmann::json::exception& e) {
        error = std::string("Draft workspace JSON parse failed: ") + e.what();
        return false;
    }
    return true;
}

std::filesystem::path DraftsDirectory(const std::filesystem::path& project_root) {
    return project_root / ".af" / "drafts";
}

std::filesystem::path DraftWorkspaceDirectory(const std::filesystem::path& project_root,
                                              const std::string& argument_key) {
    return DraftsDirectory(project_root) / argument_key;
}

std::filesystem::path DraftWorkspacePath(const std::filesystem::path& project_root, const std::string& argument_key) {
    return DraftWorkspaceDirectory(project_root, argument_key) / "workspace.json";
}

std::filesystem::path DraftEventsPath(const std::filesystem::path& project_root, const std::string& argument_key) {
    return DraftWorkspaceDirectory(project_root, argument_key) / "events.jsonl";
}

bool SaveDraftWorkspace(const std::filesystem::path& project_root,
                        const std::string& argument_key,
                        const DraftWorkspace& workspace,
                        std::string& error) {
    error.clear();
    if (project_root.empty()) {
        error = "Cannot save a draft workspace without a project root.";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(DraftWorkspaceDirectory(project_root, argument_key), ec);
    if (ec) {
        error = "Could not create the draft directory: " + ec.message();
        return false;
    }

    // The event log is secondary evidence; workspace.json is the recovery source
    // of truth. Write events first and the workspace last so a false return never
    // means the authoritative workspace was already advanced behind the caller's
    // back. A failed final workspace write may leave an extra event, but never
    // loses or prematurely accepts draft operations.
    std::ostringstream events;
    for (const DraftEvent& event : workspace.events)
        events << EventToJson(event).dump() << '\n';
    const std::expected<void, std::string> events_written =
        WriteTextFileAtomic(DraftEventsPath(project_root, argument_key), events.str());
    if (!events_written.has_value()) {
        error = events_written.error();
        return false;
    }

    const std::expected<void, std::string> written =
        WriteTextFileAtomic(DraftWorkspacePath(project_root, argument_key), SerializeDraftWorkspace(workspace));
    if (!written.has_value()) {
        error = written.error();
        return false;
    }
    return true;
}

bool LoadDraftWorkspace(const std::filesystem::path& project_root,
                        const std::string& argument_key,
                        DraftWorkspace& workspace,
                        std::string& error) {
    error.clear();
    const std::filesystem::path path = DraftWorkspacePath(project_root, argument_key);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        // Same distinction the promotion snapshot makes below: a project with no
        // draft is the ordinary case, but a draft directory that cannot be
        // interrogated is reported rather than read as "there is no draft". The
        // work may be hours of an agent's conversation and this file is the only
        // copy of it.
        if (ec) {
            error = "Could not determine whether this argument has a draft: " + ec.message();
            return false;
        }
        return false;
    }

    const std::expected<std::string, std::string> content = ReadTextFile(path);
    if (!content.has_value()) {
        error = content.error();
        return false;
    }
    if (!DeserializeDraftWorkspace(content.value(), workspace, error))
        return false;

    const std::expected<std::string, std::string> events = ReadTextFile(DraftEventsPath(project_root, argument_key));
    if (events.has_value()) {
        std::istringstream stream(events.value());
        std::string line;
        while (std::getline(stream, line)) {
            if (line.empty())
                continue;
            try {
                const nlohmann::json object = nlohmann::json::parse(line);
                DraftEvent event;
                event.revision = object.value("revision", std::uint64_t{0});
                event.type = object.value("type", "");
                event.group_id = object.value("group_id", "");
                event.detail = object.value("detail", "");
                event.created_utc = object.value("created_utc", "");
                workspace.events.push_back(std::move(event));
            } catch (const nlohmann::json::exception&) {
                // A torn or hand-edited event line loses that line's history and
                // nothing else. The workspace itself is the recoverable part;
                // refusing the whole draft over a damaged log entry would lose
                // the work to protect the record of it.
                continue;
            }
        }
    }
    return true;
}

bool DraftWorkspaceExists(const std::filesystem::path& project_root, const std::string& argument_key) {
    std::error_code ec;
    return std::filesystem::exists(DraftWorkspacePath(project_root, argument_key), ec);
}

bool DeleteDraftWorkspace(const std::filesystem::path& project_root,
                          const std::string& argument_key,
                          std::string& error) {
    error.clear();
    std::error_code ec;
    std::filesystem::remove_all(DraftWorkspaceDirectory(project_root, argument_key), ec);
    if (ec) {
        error = "Could not remove the draft directory: " + ec.message();
        return false;
    }
    return true;
}

std::filesystem::path DraftPromotionSnapshotsDirectory(const std::filesystem::path& project_root) {
    return project_root / ".af" / "draft-promotions";
}

std::filesystem::path DraftPromotionSnapshotPath(const std::filesystem::path& project_root,
                                                 std::uint64_t transaction_sequence) {
    return DraftPromotionSnapshotsDirectory(project_root) / (std::to_string(transaction_sequence) + ".json");
}

bool SaveDraftPromotionSnapshot(const std::filesystem::path& project_root,
                                std::uint64_t transaction_sequence,
                                const DraftWorkspace& workspace,
                                std::string& error) {
    error.clear();
    if (project_root.empty()) {
        error = "Cannot save a draft promotion snapshot without a project root.";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(DraftPromotionSnapshotsDirectory(project_root), ec);
    if (ec) {
        error = "Could not create the draft promotion directory: " + ec.message();
        return false;
    }

    // Events are not written beside it. A snapshot is not a second workspace a
    // user can open; it is the material one undo needs, and the live event log
    // keeps the history of what happened either way.
    const std::expected<void, std::string> written = WriteTextFileAtomic(
        DraftPromotionSnapshotPath(project_root, transaction_sequence), SerializeDraftWorkspace(workspace));
    if (!written.has_value()) {
        error = written.error();
        return false;
    }
    return true;
}

bool LoadDraftPromotionSnapshot(const std::filesystem::path& project_root,
                                std::uint64_t transaction_sequence,
                                DraftWorkspace& workspace,
                                std::string& error) {
    error.clear();
    const std::filesystem::path path = DraftPromotionSnapshotPath(project_root, transaction_sequence);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        // `exists` reports "no" for both a genuinely absent file and a path it
        // could not interrogate, and those must not mean the same thing here.
        // Absent is "this transaction was not a promotion"; unreadable is "there
        // may be unaccepted work behind this and it cannot be seen". Collapsing
        // the second into the first lets an undo proceed as an ordinary one and
        // destroy the only copy of that work.
        if (ec) {
            error = "Could not determine whether this acceptance has a draft snapshot: " + ec.message();
            return false;
        }
        return false;
    }

    const std::expected<std::string, std::string> content = ReadTextFile(path);
    if (!content.has_value()) {
        error = content.error();
        return false;
    }
    return DeserializeDraftWorkspace(content.value(), workspace, error);
}

bool DraftPromotionSnapshotExists(const std::filesystem::path& project_root, std::uint64_t transaction_sequence) {
    std::error_code ec;
    return std::filesystem::exists(DraftPromotionSnapshotPath(project_root, transaction_sequence), ec);
}

bool DeleteDraftPromotionSnapshot(const std::filesystem::path& project_root,
                                  std::uint64_t transaction_sequence,
                                  std::string& error) {
    error.clear();
    std::error_code ec;
    std::filesystem::remove(DraftPromotionSnapshotPath(project_root, transaction_sequence), ec);
    if (ec) {
        error = "Could not remove the draft promotion snapshot: " + ec.message();
        return false;
    }
    return true;
}

} // namespace core::drafts
