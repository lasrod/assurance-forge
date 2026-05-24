// Tests for the timeline model builder (core::audit::BuildTimelineModel).
//
// The builder is pure-data and UI-free: given transactions, baselines, and
// snapshots, it produces a TimelineModel whose points are ordered by
// transaction_sequence, with baselines preceding snapshots when they share
// a sequence, and the synthetic Now marker appended last.

#include "core/audit/timeline_model_builder.h"

#include <gtest/gtest.h>

namespace {

using core::audit::AuditTransaction;
using core::audit::BaselineMetadata;
using core::audit::SnapshotMetadata;
using core::audit::TimelineQuery;
using core::audit::TimelineViewMode;
using core::audit::TimelinePointType;

AuditTransaction MakeTx(std::uint64_t seq) {
    AuditTransaction tx;
    tx.transaction_sequence = seq;
    tx.transaction_id = "tx_" + std::to_string(seq);
    return tx;
}

BaselineMetadata MakeBaseline(const std::string& id, std::uint64_t seq) {
    BaselineMetadata b;
    b.baseline_id = id;
    b.name = id;
    b.transaction_sequence = seq;
    return b;
}

SnapshotMetadata MakeSnapshot(const std::string& id, std::uint64_t seq) {
    SnapshotMetadata s;
    s.snapshot_id = id;
    s.transaction_sequence = seq;
    return s;
}

TEST(TimelineModelBuilder, EmptyInputsStillEmitsNow) {
    TimelineQuery q;
    auto model = core::audit::BuildTimelineModel({}, {}, {}, q);
    EXPECT_EQ(model.latest_sequence, 0u);
    ASSERT_EQ(model.points.size(), 1u);
    EXPECT_EQ(model.points[0].type, TimelinePointType::Now);
}

TEST(TimelineModelBuilder, BaselinesModeOmitsSnapshots) {
    std::vector<AuditTransaction> txs{MakeTx(1), MakeTx(2), MakeTx(3)};
    std::vector<BaselineMetadata> baselines{MakeBaseline("b1", 2)};
    std::vector<SnapshotMetadata> snapshots{MakeSnapshot("s1", 1)};

    TimelineQuery q;
    q.view_mode = TimelineViewMode::Baselines;
    auto model = core::audit::BuildTimelineModel(txs, baselines, snapshots, q);
    EXPECT_EQ(model.latest_sequence, 3u);

    // Baseline + Now only.
    ASSERT_EQ(model.points.size(), 2u);
    EXPECT_EQ(model.points[0].type, TimelinePointType::Baseline);
    EXPECT_EQ(model.points[0].id, "b1");
    EXPECT_EQ(model.points[1].type, TimelinePointType::Now);
}

TEST(TimelineModelBuilder, SnapshotsModeMergesAndOrders) {
    std::vector<AuditTransaction> txs{MakeTx(1), MakeTx(2), MakeTx(3), MakeTx(4)};
    std::vector<BaselineMetadata> baselines{MakeBaseline("b1", 3)};
    std::vector<SnapshotMetadata> snapshots{
        MakeSnapshot("s1", 1),
        MakeSnapshot("s2", 3),  // same seq as baseline
        MakeSnapshot("s3", 4),
    };

    TimelineQuery q;
    q.view_mode = TimelineViewMode::Snapshots;
    auto model = core::audit::BuildTimelineModel(txs, baselines, snapshots, q);

    // 1 baseline + 3 snapshots + Now = 5 points, sequence-ordered, with
    // baseline preceding snapshot at seq 3.
    ASSERT_EQ(model.points.size(), 5u);
    EXPECT_EQ(model.points[0].type, TimelinePointType::Snapshot);
    EXPECT_EQ(model.points[0].id, "s1");
    EXPECT_EQ(model.points[1].type, TimelinePointType::Baseline);
    EXPECT_EQ(model.points[1].id, "b1");
    EXPECT_EQ(model.points[2].type, TimelinePointType::Snapshot);
    EXPECT_EQ(model.points[2].id, "s2");
    EXPECT_EQ(model.points[3].type, TimelinePointType::Snapshot);
    EXPECT_EQ(model.points[3].id, "s3");
    EXPECT_EQ(model.points[4].type, TimelinePointType::Now);
}

TEST(TimelineModelBuilder, BaselinesSortedAscending) {
    std::vector<AuditTransaction> txs{MakeTx(10)};
    std::vector<BaselineMetadata> baselines{
        MakeBaseline("b_late", 9),
        MakeBaseline("b_early", 2),
        MakeBaseline("b_mid", 5),
    };
    TimelineQuery q;
    auto model = core::audit::BuildTimelineModel(txs, baselines, {}, q);
    ASSERT_EQ(model.points.size(), 4u);
    EXPECT_EQ(model.points[0].id, "b_early");
    EXPECT_EQ(model.points[1].id, "b_mid");
    EXPECT_EQ(model.points[2].id, "b_late");
    EXPECT_EQ(model.points[3].type, TimelinePointType::Now);
}

TEST(TimelineModelBuilder, ChangesModeEmitsOneMarkerPerTransaction) {
    std::vector<AuditTransaction> txs{MakeTx(1), MakeTx(2), MakeTx(3)};
    std::vector<BaselineMetadata> baselines{MakeBaseline("b1", 2)};
    TimelineQuery q;
    q.view_mode = TimelineViewMode::Changes;
    auto model = core::audit::BuildTimelineModel(txs, baselines, {}, q);

    // 1 baseline + 3 change markers + Now = 5 points, sequence-ordered.
    // Baseline at seq 2 should precede the change marker at seq 2 because
    // Baseline < Change in the TimelinePointType enum.
    ASSERT_EQ(model.points.size(), 5u);
    EXPECT_EQ(model.points[0].type, TimelinePointType::Change);
    EXPECT_EQ(model.points[0].transaction_sequence, 1u);
    EXPECT_EQ(model.points[1].type, TimelinePointType::Baseline);
    EXPECT_EQ(model.points[2].type, TimelinePointType::Change);
    EXPECT_EQ(model.points[2].transaction_sequence, 2u);
    EXPECT_EQ(model.points[3].type, TimelinePointType::Change);
    EXPECT_EQ(model.points[3].transaction_sequence, 3u);
    EXPECT_EQ(model.points[4].type, TimelinePointType::Now);
    // Tooltip carries the transaction id reference.
    EXPECT_NE(model.points[0].tooltip.find("Tx 1"), std::string::npos);
}

TEST(TimelineModelBuilder, ChangesModePropagatesTransactionId) {
    std::vector<AuditTransaction> txs;
    AuditTransaction tx = MakeTx(7);
    tx.command_name = "CreateClaim";
    tx.author = "alice";
    txs.push_back(tx);
    TimelineQuery q;
    q.view_mode = TimelineViewMode::Changes;
    auto model = core::audit::BuildTimelineModel(txs, {}, {}, q);
    ASSERT_EQ(model.points.size(), 2u);
    EXPECT_EQ(model.points[0].type, TimelinePointType::Change);
    EXPECT_EQ(model.points[0].id, "tx_7");
    EXPECT_NE(model.points[0].tooltip.find("CreateClaim"), std::string::npos);
    EXPECT_NE(model.points[0].tooltip.find("alice"), std::string::npos);
}

} // namespace
