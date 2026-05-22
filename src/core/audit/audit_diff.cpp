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
    if (type == "UpdateElementText") {
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

} // namespace core::audit
