#include "core/audit/audit_diff.h"

namespace core::audit {

namespace {

void AddAddedFromEvent(const AuditEvent& event, AuditChangeSet& out) {
    const std::string& type = event.event_type;
    const auto& payload = event.payload;

    if (type == "CreateTopGoal") {
        auto it = payload.find("generated_id");
        if (it != payload.end() && it->is_string())
            out.added.insert(it->get<std::string>());
        return;
    }

    if (type == "CreateChildElement") {
        auto element_it = payload.find("generated_id");
        if (element_it != payload.end() && element_it->is_string())
            out.added.insert(element_it->get<std::string>());
        auto rel_it = payload.find("generated_relationship_id");
        if (rel_it != payload.end() && rel_it->is_string())
            out.added.insert(rel_it->get<std::string>());
        return;
    }

    if (type == "RemoveElement") {
        auto deleted_it = payload.find("deleted_ids");
        if (deleted_it != payload.end() && deleted_it->is_array()) {
            for (const auto& v : *deleted_it) {
                if (v.is_string())
                    out.deleted.insert(v.get<std::string>());
            }
        } else {
            auto el_it = payload.find("element_id");
            if (el_it != payload.end() && el_it->is_string())
                out.deleted.insert(el_it->get<std::string>());
        }
        return;
    }
    if (type == "ImportEvidenceAssessments") {
        auto items_it = payload.find("items");
        if (items_it != payload.end() && items_it->is_array()) {
            for (const auto& item : *items_it) {
                if (item.is_object() && item.contains("element_id") && item["element_id"].is_string())
                    out.modified.insert(item["element_id"].get<std::string>());
            }
        }
        return;
    }
    if (type == "UpdateElementText" || type == "UpdateGsnIdentifier" || type == "SetEvidenceLocation" ||
        type == "SetEvidenceAttribute") {
        auto el_it = payload.find("element_id");
        if (el_it != payload.end() && el_it->is_string())
            out.modified.insert(el_it->get<std::string>());
        return;
    }
    // Unknown event types contribute nothing; tolerated for forward compat.
}

void MergeIntoChangeSet(const AuditChangeSet& src, AuditChangeSet& dst) {
    for (const auto& id : src.added) {
        dst.deleted.erase(id);
        dst.added.insert(id);
    }
    for (const auto& id : src.modified) {
        if (dst.added.find(id) == dst.added.end())
            dst.modified.insert(id);
    }
    for (const auto& id : src.deleted) {
        dst.added.erase(id);
        dst.modified.erase(id);
        dst.deleted.insert(id);
    }
}

} // namespace

AuditChangeSet ComputeChangeSet(const AuditTransaction& tx) {
    AuditChangeSet cs;
    for (const AuditEvent& e : tx.events)
        AddAddedFromEvent(e, cs);
    return cs;
}

AuditChangeSet ComputeChangeSet(const std::vector<AuditTransaction>& transactions) {
    AuditChangeSet agg;
    for (const AuditTransaction& tx : transactions) {
        const AuditChangeSet per_tx = ComputeChangeSet(tx);
        MergeIntoChangeSet(per_tx, agg);
    }
    return agg;
}

ElementHistorySummary SummarizeElementHistory(const std::string& element_id,
                                              const std::vector<AuditTransaction>& transactions,
                                              std::optional<std::uint64_t> baseline_sequence) {
    ElementHistorySummary summary;
    if (element_id.empty())
        return summary;

    for (const AuditTransaction& tx : transactions) {
        const AuditChangeSet cs = ComputeChangeSet(tx);
        const bool added = cs.added.find(element_id) != cs.added.end();
        const bool modified = cs.modified.find(element_id) != cs.modified.end();
        const bool deleted = cs.deleted.find(element_id) != cs.deleted.end();
        if (!added && !modified && !deleted)
            continue;

        if (added && !summary.ever_seen) {
            summary.first_sequence = tx.transaction_sequence;
            summary.created_at = tx.timestamp;
            summary.created_by = tx.author;
        }
        summary.ever_seen = true;
        summary.exists = deleted ? false : (added || modified || summary.exists);
        ++summary.change_count;
        summary.last_sequence = tx.transaction_sequence;
        summary.last_changed_at = tx.timestamp;
        summary.last_changed_by = tx.author;

        if (baseline_sequence.has_value() && tx.transaction_sequence > *baseline_sequence)
            summary.changed_since_baseline = true;
    }
    return summary;
}

} // namespace core::audit
