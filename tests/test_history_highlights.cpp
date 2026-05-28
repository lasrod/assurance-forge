#include "core/audit/audit_diff.h"
#include "core/audit/audit_transaction.h"
#include "core/audit/history_highlights.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

core::audit::AuditEvent MakeEvent(const char* type, nlohmann::ordered_json payload) {
    core::audit::AuditEvent e;
    e.event_type = type;
    e.payload = std::move(payload);
    return e;
}

core::audit::AuditTransaction MakeTx(std::uint64_t seq, std::vector<core::audit::AuditEvent> events) {
    core::audit::AuditTransaction tx;
    tx.transaction_sequence = seq;
    for (auto& e : events) {
        e.event_sequence = seq * 10 + tx.events.size() + 1;
        tx.events.push_back(std::move(e));
    }
    return tx;
}

} // namespace

TEST(HistoryHighlights, MapsAddedAndDeletedKinds) {
    core::audit::AuditChangeSet cs;
    cs.added.insert("G7");
    cs.deleted.insert("S1");
    cs.modified.insert("Sn1");
    const auto map = core::audit::BuildHistoryHighlights(cs);
    ASSERT_EQ(map.size(), 3u);
    EXPECT_EQ(map.at("G7"), core::audit::HistoryHighlightKind::Added);
    EXPECT_EQ(map.at("S1"), core::audit::HistoryHighlightKind::Deleted);
    EXPECT_EQ(map.at("Sn1"), core::audit::HistoryHighlightKind::Modified);
}

TEST(HistoryHighlights, DeletedTakesPrecedenceOverAdded) {
    core::audit::AuditChangeSet cs;
    cs.added.insert("X");
    cs.deleted.insert("X");
    const auto map = core::audit::BuildHistoryHighlights(cs);
    ASSERT_EQ(map.size(), 1u);
    EXPECT_EQ(map.at("X"), core::audit::HistoryHighlightKind::Deleted);
}

TEST(HistoryHighlights, SequenceZeroReturnsEmpty) {
    std::vector<core::audit::AuditTransaction> txs;
    txs.push_back(MakeTx(1, {MakeEvent("CreateTopGoal", {{"generated_id", "G1"}})}));
    const auto map = core::audit::BuildHistoryHighlightsForSequence(txs, 0);
    EXPECT_TRUE(map.empty());
}

TEST(HistoryHighlights, SequenceSelectsExactTransaction) {
    std::vector<core::audit::AuditTransaction> txs;
    txs.push_back(MakeTx(1, {MakeEvent("CreateTopGoal", {{"generated_id", "G1"}})}));
    txs.push_back(MakeTx(2, {MakeEvent("CreateChildElement",
                                       {{"parent_id", "G1"},
                                        {"kind", "Strategy"},
                                        {"generated_id", "S1"},
                                        {"generated_relationship_id", "AI1"}})}));
    txs.push_back(MakeTx(3, {MakeEvent("RemoveElement",
                                       {{"element_id", "S1"},
                                        {"mode", "NodeAndDescendants"},
                                        {"deleted_ids", nlohmann::ordered_json::array({"S1", "AI1"})}})}));

    const auto h1 = core::audit::BuildHistoryHighlightsForSequence(txs, 1);
    ASSERT_EQ(h1.size(), 1u);
    EXPECT_EQ(h1.at("G1"), core::audit::HistoryHighlightKind::Added);

    const auto h2 = core::audit::BuildHistoryHighlightsForSequence(txs, 2);
    ASSERT_EQ(h2.size(), 2u);
    EXPECT_EQ(h2.at("S1"), core::audit::HistoryHighlightKind::Added);
    EXPECT_EQ(h2.at("AI1"), core::audit::HistoryHighlightKind::Added);

    const auto h3 = core::audit::BuildHistoryHighlightsForSequence(txs, 3);
    ASSERT_EQ(h3.size(), 2u);
    EXPECT_EQ(h3.at("S1"), core::audit::HistoryHighlightKind::Deleted);
    EXPECT_EQ(h3.at("AI1"), core::audit::HistoryHighlightKind::Deleted);
}

TEST(HistoryHighlights, UnknownSequenceReturnsEmpty) {
    std::vector<core::audit::AuditTransaction> txs;
    txs.push_back(MakeTx(1, {MakeEvent("CreateTopGoal", {{"generated_id", "G1"}})}));
    const auto map = core::audit::BuildHistoryHighlightsForSequence(txs, 99);
    EXPECT_TRUE(map.empty());
}
