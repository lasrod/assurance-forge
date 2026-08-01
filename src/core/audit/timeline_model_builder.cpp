#include "core/audit/timeline_model_builder.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>

namespace core::audit {

namespace {

std::string MakeBaselineLabel(std::size_t index) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "B%zu", index);
    return buf;
}

std::string MakeSnapshotLabel(std::size_t index) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "S%zu", index);
    return buf;
}

std::string MakeBaselineTooltip(const BaselineMetadata& md) {
    std::string out;
    out.reserve(64);
    out += md.name.empty() ? md.baseline_id : md.name;
    if (!md.description.empty()) {
        out += '\n';
        out += md.description;
    }
    out += "\nSequence: ";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(md.transaction_sequence));
    out += buf;
    if (!md.created_at.empty()) {
        out += "\nCreated: ";
        out += md.created_at;
    }
    if (!md.created_by.empty()) {
        out += " by ";
        out += md.created_by;
    }
    return out;
}

std::string MakeSnapshotTooltip(const SnapshotMetadata& md) {
    std::string out;
    out.reserve(64);
    out += md.snapshot_id;
    if (!md.reason.empty()) {
        out += '\n';
        out += md.reason;
    }
    out += "\nSequence: ";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(md.transaction_sequence));
    out += buf;
    if (!md.created_at.empty()) {
        out += "\nCreated: ";
        out += md.created_at;
    }
    return out;
}

std::string MakeChangeTooltip(const AuditTransaction& tx) {
    std::string out;
    out.reserve(96);
    out += "Tx ";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(tx.transaction_sequence));
    out += buf;
    if (!tx.command_name.empty()) {
        out += " — ";
        out += tx.command_name;
    }
    if (!tx.author.empty()) {
        out += "\nby ";
        out += tx.author;
    }
    if (!tx.timestamp.empty()) {
        out += "\n";
        out += tx.timestamp;
    }
    if (!tx.events.empty()) {
        std::snprintf(buf, sizeof(buf), "\n%zu event%s", tx.events.size(), tx.events.size() == 1 ? "" : "s");
        out += buf;
    }
    return out;
}

} // namespace

TimelineModel BuildTimelineModel(const std::vector<AuditTransaction>& transactions,
                                 const std::vector<BaselineMetadata>& baselines,
                                 const std::vector<SnapshotMetadata>& snapshots,
                                 const TimelineQuery& query) {
    TimelineModel model;
    model.has_audit_store = true;
    model.latest_sequence = transactions.empty() ? 0 : transactions.back().transaction_sequence;

    // Baselines — labelled B0, B1, … in ascending-sequence order. Ties
    // broken by stable id for determinism when two baselines share a
    // sequence (rare but legal).
    std::vector<BaselineMetadata> sorted_baselines = baselines;
    std::sort(
        sorted_baselines.begin(), sorted_baselines.end(), [](const BaselineMetadata& a, const BaselineMetadata& b) {
            if (a.transaction_sequence != b.transaction_sequence)
                return a.transaction_sequence < b.transaction_sequence;
            return a.baseline_id < b.baseline_id;
        });
    for (std::size_t i = 0; i < sorted_baselines.size(); ++i) {
        const BaselineMetadata& md = sorted_baselines[i];
        TimelinePoint p;
        p.transaction_sequence = md.transaction_sequence;
        p.type = TimelinePointType::Baseline;
        p.id = md.baseline_id;
        p.label = MakeBaselineLabel(i);
        p.tooltip = MakeBaselineTooltip(md);
        p.is_major = true;
        model.points.push_back(std::move(p));
    }

    // Snapshots — always emitted now that the rail is unified. The
    // snapshot whose id matches `query.initial_snapshot_id` is tagged
    // `InitialSnapshot` (sorts first at its sequence) and gets the "S0"
    // label; regular snapshots get S1, S2, … in ascending-sequence order.
    std::vector<SnapshotMetadata> sorted_snapshots = snapshots;
    std::sort(
        sorted_snapshots.begin(), sorted_snapshots.end(), [](const SnapshotMetadata& a, const SnapshotMetadata& b) {
            if (a.transaction_sequence != b.transaction_sequence)
                return a.transaction_sequence < b.transaction_sequence;
            return a.snapshot_id < b.snapshot_id;
        });
    std::size_t regular_idx = 1;
    for (const SnapshotMetadata& md : sorted_snapshots) {
        const bool is_initial = !query.initial_snapshot_id.empty() && md.snapshot_id == query.initial_snapshot_id;
        TimelinePoint p;
        p.transaction_sequence = md.transaction_sequence;
        p.type = is_initial ? TimelinePointType::InitialSnapshot : TimelinePointType::Snapshot;
        p.id = md.snapshot_id;
        p.label = is_initial ? std::string("S0") : MakeSnapshotLabel(regular_idx++);
        p.tooltip = MakeSnapshotTooltip(md);
        p.is_major = is_initial;
        model.points.push_back(std::move(p));
    }

    // Changes — one marker per recorded transaction. Stable id is the
    // transaction id (or synthesized "tx-<seq>" when missing).
    {
        char id_buf[40];
        for (const AuditTransaction& tx : transactions) {
            TimelinePoint p;
            p.transaction_sequence = tx.transaction_sequence;
            p.type = TimelinePointType::Change;
            if (!tx.transaction_id.empty()) {
                p.id = tx.transaction_id;
            } else {
                std::snprintf(
                    id_buf, sizeof(id_buf), "tx-%llu", static_cast<unsigned long long>(tx.transaction_sequence));
                p.id = id_buf;
            }
            // Label intentionally empty — the rail would be too noisy if every
            // change drew a text label. The tooltip carries the detail.
            p.label.clear();
            p.tooltip = MakeChangeTooltip(tx);
            p.is_major = false;
            model.points.push_back(std::move(p));
        }
    }

    // Stable order: ascending sequence, then by kind priority
    // (InitialSnapshot < Baseline < Snapshot < Change), then by stable id
    // so equal-sequence ties (e.g. B0/B1 at the same baseline sequence)
    // come out in a fully deterministic order.
    std::stable_sort(model.points.begin(), model.points.end(), [](const TimelinePoint& a, const TimelinePoint& b) {
        if (a.transaction_sequence != b.transaction_sequence)
            return a.transaction_sequence < b.transaction_sequence;
        if (a.type != b.type)
            return static_cast<int>(a.type) < static_cast<int>(b.type);
        return a.id < b.id;
    });

    // Synthetic Now marker — always appended last regardless of its enum
    // value so it renders to the right of every same-sequence marker.
    TimelinePoint now;
    now.transaction_sequence = model.latest_sequence;
    now.type = TimelinePointType::Now;
    now.label = "NOW";
    now.tooltip = "Latest state";
    now.is_major = true;
    model.points.push_back(std::move(now));

    return model;
}

} // namespace core::audit
