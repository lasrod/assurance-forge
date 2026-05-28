// Tests for the timeline model builder (core::audit::BuildTimelineModel).
//
// Phase 2 unified rail: the builder always emits baselines + snapshots +
// one change marker per transaction + a synthetic Now marker. Ordering is
// ascending `transaction_sequence`, with kind priority
// `InitialSnapshot < Baseline < Snapshot < Change` breaking ties at a
// shared sequence and stable id breaking remaining ties. `Now` is always
// appended last.

#include "core/audit/timeline_model_builder.h"

#include <gtest/gtest.h>

namespace {

using core::audit::AuditTransaction;
using core::audit::BaselineMetadata;
using core::audit::SnapshotMetadata;
using core::audit::TimelineQuery;
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

TEST(TimelineModelBuilder, UnifiedRailEmitsAllKinds) {
    // 3 transactions, 1 baseline at seq 2, 1 snapshot at seq 1.
    std::vector<AuditTransaction> txs{MakeTx(1), MakeTx(2), MakeTx(3)};
    std::vector<BaselineMetadata> baselines{MakeBaseline("b1", 2)};
    std::vector<SnapshotMetadata> snapshots{MakeSnapshot("s1", 1)};

    TimelineQuery q;
    auto model = core::audit::BuildTimelineModel(txs, baselines, snapshots, q);
    EXPECT_EQ(model.latest_sequence, 3u);

    // 1 baseline + 1 snapshot + 3 changes + Now = 6 points.
    ASSERT_EQ(model.points.size(), 6u);

    // seq 1: snapshot before change.
    EXPECT_EQ(model.points[0].transaction_sequence, 1u);
    EXPECT_EQ(model.points[0].type, TimelinePointType::Snapshot);
    EXPECT_EQ(model.points[0].id, "s1");
    EXPECT_EQ(model.points[1].transaction_sequence, 1u);
    EXPECT_EQ(model.points[1].type, TimelinePointType::Change);

    // seq 2: baseline before change.
    EXPECT_EQ(model.points[2].transaction_sequence, 2u);
    EXPECT_EQ(model.points[2].type, TimelinePointType::Baseline);
    EXPECT_EQ(model.points[2].id, "b1");
    EXPECT_EQ(model.points[3].transaction_sequence, 2u);
    EXPECT_EQ(model.points[3].type, TimelinePointType::Change);

    // seq 3: change only.
    EXPECT_EQ(model.points[4].transaction_sequence, 3u);
    EXPECT_EQ(model.points[4].type, TimelinePointType::Change);

    // Now appended last.
    EXPECT_EQ(model.points[5].type, TimelinePointType::Now);
    EXPECT_EQ(model.points[5].transaction_sequence, 3u);
}

TEST(TimelineModelBuilder, InitialSnapshotSortsBeforeBaselineAtSameSequence) {
    // Initial snapshot at seq 0 (convention), one baseline also at seq 0.
    std::vector<SnapshotMetadata> snapshots{MakeSnapshot("snapshot_000000", 0)};
    std::vector<BaselineMetadata> baselines{MakeBaseline("b_zero", 0)};

    TimelineQuery q;
    q.initial_snapshot_id = "snapshot_000000";
    auto model = core::audit::BuildTimelineModel({}, baselines, snapshots, q);

    // Initial snapshot + baseline + Now = 3 points.
    ASSERT_EQ(model.points.size(), 3u);
    EXPECT_EQ(model.points[0].type, TimelinePointType::InitialSnapshot);
    EXPECT_EQ(model.points[0].id, "snapshot_000000");
    EXPECT_EQ(model.points[0].label, "S0");
    EXPECT_EQ(model.points[1].type, TimelinePointType::Baseline);
    EXPECT_EQ(model.points[1].id, "b_zero");
    EXPECT_EQ(model.points[2].type, TimelinePointType::Now);
}

TEST(TimelineModelBuilder, InitialFlagOptionalWhenIdUnknown) {
    // No initial_snapshot_id supplied — snapshot is treated as regular.
    std::vector<SnapshotMetadata> snapshots{MakeSnapshot("snapshot_000000", 0)};
    TimelineQuery q;  // initial_snapshot_id empty
    auto model = core::audit::BuildTimelineModel({}, {}, snapshots, q);

    ASSERT_EQ(model.points.size(), 2u);
    EXPECT_EQ(model.points[0].type, TimelinePointType::Snapshot);
    EXPECT_NE(model.points[0].label, "S0");
}

TEST(TimelineModelBuilder, TwoBaselinesAtSameSequenceAreDistinct) {
    std::vector<BaselineMetadata> baselines{
        MakeBaseline("b_alpha", 5),
        MakeBaseline("b_beta", 5),  // same seq as b_alpha
    };
    TimelineQuery q;
    auto model = core::audit::BuildTimelineModel({MakeTx(5)}, baselines, {}, q);

    // 2 baselines + 1 change + Now = 4 points.
    ASSERT_EQ(model.points.size(), 4u);
    EXPECT_EQ(model.points[0].type, TimelinePointType::Baseline);
    EXPECT_EQ(model.points[0].id, "b_alpha");
    EXPECT_EQ(model.points[0].label, "B0");
    EXPECT_EQ(model.points[1].type, TimelinePointType::Baseline);
    EXPECT_EQ(model.points[1].id, "b_beta");
    EXPECT_EQ(model.points[1].label, "B1");
    EXPECT_EQ(model.points[2].type, TimelinePointType::Change);
    EXPECT_EQ(model.points[3].type, TimelinePointType::Now);
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
    // 3 baselines + 1 change + Now = 5 points.
    ASSERT_EQ(model.points.size(), 5u);
    EXPECT_EQ(model.points[0].id, "b_early");
    EXPECT_EQ(model.points[1].id, "b_mid");
    EXPECT_EQ(model.points[2].id, "b_late");
    EXPECT_EQ(model.points[3].type, TimelinePointType::Change);
    EXPECT_EQ(model.points[4].type, TimelinePointType::Now);
}

TEST(TimelineModelBuilder, NowAlwaysLast) {
    // Even when the latest transaction has the same sequence as a baseline
    // / snapshot, Now must be the final point.
    std::vector<AuditTransaction> txs{MakeTx(7)};
    std::vector<BaselineMetadata> baselines{MakeBaseline("b1", 7)};
    std::vector<SnapshotMetadata> snapshots{MakeSnapshot("s1", 7)};
    TimelineQuery q;
    auto model = core::audit::BuildTimelineModel(txs, baselines, snapshots, q);

    ASSERT_GE(model.points.size(), 1u);
    EXPECT_EQ(model.points.back().type, TimelinePointType::Now);
    EXPECT_EQ(model.points.back().transaction_sequence, 7u);
}

TEST(TimelineModelBuilder, ChangeMarkerPropagatesTransactionMetadata) {
    AuditTransaction tx = MakeTx(7);
    tx.command_name = "CreateClaim";
    tx.author = "alice";
    TimelineQuery q;
    auto model = core::audit::BuildTimelineModel({tx}, {}, {}, q);

    // 1 change + Now = 2.
    ASSERT_EQ(model.points.size(), 2u);
    EXPECT_EQ(model.points[0].type, TimelinePointType::Change);
    EXPECT_EQ(model.points[0].id, "tx_7");
    EXPECT_NE(model.points[0].tooltip.find("CreateClaim"), std::string::npos);
    EXPECT_NE(model.points[0].tooltip.find("alice"), std::string::npos);
}

} // namespace
