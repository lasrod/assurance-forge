#include "core/audit/audit_diff.h"
#include "core/audit/audit_transaction.h"

#include <gtest/gtest.h>

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

TEST(AuditDiff, CreateTopGoalAddsGeneratedId) {
    auto tx = MakeTx(1, {MakeEvent("CreateTopGoal", {{"generated_id", "G7"}})});
    const auto cs = core::audit::ComputeChangeSet(tx);
    EXPECT_EQ(cs.added.size(), 1u);
    EXPECT_TRUE(cs.added.count("G7"));
    EXPECT_TRUE(cs.modified.empty());
    EXPECT_TRUE(cs.deleted.empty());
}

TEST(AuditDiff, CreateChildElementAddsElementAndRelationship) {
    auto tx = MakeTx(1, {MakeEvent("CreateChildElement",
                                   {{"parent_id", "G1"},
                                    {"kind", "Strategy"},
                                    {"generated_id", "AP1_S1"},
                                    {"generated_relationship_id", "AP1_AI1"}})});
    const auto cs = core::audit::ComputeChangeSet(tx);
    EXPECT_EQ(cs.added.size(), 2u);
    EXPECT_TRUE(cs.added.count("AP1_S1"));
    EXPECT_TRUE(cs.added.count("AP1_AI1"));
}

TEST(AuditDiff, RemoveElementUsesDeletedIdsArray) {
    auto tx = MakeTx(1, {MakeEvent("RemoveElement",
                                   {{"element_id", "G1"},
                                    {"mode", "NodeAndDescendants"},
                                    {"deleted_ids", nlohmann::ordered_json::array({"G1", "S1", "AI1"})}})});
    const auto cs = core::audit::ComputeChangeSet(tx);
    EXPECT_EQ(cs.deleted.size(), 3u);
    EXPECT_TRUE(cs.deleted.count("G1"));
    EXPECT_TRUE(cs.deleted.count("S1"));
    EXPECT_TRUE(cs.deleted.count("AI1"));
    EXPECT_TRUE(cs.added.empty());
}

TEST(AuditDiff, RemoveElementFallsBackToElementIdWhenArrayMissing) {
    auto tx = MakeTx(1, {MakeEvent("RemoveElement",
                                   {{"element_id", "G1"}, {"mode", "NodeOnly"}})});
    const auto cs = core::audit::ComputeChangeSet(tx);
    EXPECT_EQ(cs.deleted.size(), 1u);
    EXPECT_TRUE(cs.deleted.count("G1"));
}

TEST(AuditDiff, AggregateMergesAdditionsThenDeletionsCancelOut) {
    auto t1 = MakeTx(1, {MakeEvent("CreateTopGoal", {{"generated_id", "G7"}})});
    auto t2 = MakeTx(2, {MakeEvent("RemoveElement",
                                   {{"element_id", "G7"},
                                    {"mode", "NodeOnly"},
                                    {"deleted_ids", nlohmann::ordered_json::array({"G7"})}})});
    const auto cs = core::audit::ComputeChangeSet(std::vector{t1, t2});
    EXPECT_TRUE(cs.added.empty());
    EXPECT_TRUE(cs.modified.empty());
    EXPECT_EQ(cs.deleted.size(), 1u);
    EXPECT_TRUE(cs.deleted.count("G7"));
}

TEST(AuditDiff, AggregatePreservesAddWhenLaterTransactionDoesNotDelete) {
    auto t1 = MakeTx(1, {MakeEvent("CreateChildElement",
                                   {{"parent_id", "G1"},
                                    {"kind", "Solution"},
                                    {"generated_id", "AP1_Sn1"},
                                    {"generated_relationship_id", "AP1_AE1"}})});
    auto t2 = MakeTx(2, {MakeEvent("CreateTopGoal", {{"generated_id", "G8"}})});
    const auto cs = core::audit::ComputeChangeSet(std::vector{t1, t2});
    EXPECT_EQ(cs.added.size(), 3u);
    EXPECT_TRUE(cs.added.count("AP1_Sn1"));
    EXPECT_TRUE(cs.added.count("AP1_AE1"));
    EXPECT_TRUE(cs.added.count("G8"));
}

TEST(AuditDiff, UnknownEventTypeIsIgnored) {
    auto tx = MakeTx(1, {MakeEvent("FrobnicateElement", {{"target", "G1"}})});
    const auto cs = core::audit::ComputeChangeSet(tx);
    EXPECT_TRUE(cs.added.empty());
    EXPECT_TRUE(cs.modified.empty());
    EXPECT_TRUE(cs.deleted.empty());
}

TEST(AuditDiff, UpdateElementTextMarksElementModified) {
    auto tx = MakeTx(1, {MakeEvent("UpdateElementText",
                                   {{"element_id", "G1"},
                                    {"field", "description"},
                                    {"language", "en"},
                                    {"old_value", "before"},
                                    {"new_value", "after"}})});
    const auto cs = core::audit::ComputeChangeSet(tx);
    EXPECT_TRUE(cs.added.empty());
    EXPECT_TRUE(cs.deleted.empty());
    EXPECT_EQ(cs.modified.size(), 1u);
    EXPECT_TRUE(cs.modified.count("G1"));
}

TEST(AuditDiff, AggregateDoesNotMarkNewlyAddedElementAsModified) {
    auto create = MakeTx(1, {MakeEvent("CreateTopGoal", {{"generated_id", "G1"}})});
    auto edit = MakeTx(2, {MakeEvent("UpdateElementText",
                                     {{"element_id", "G1"},
                                      {"field", "name"},
                                      {"language", "en"},
                                      {"old_value", ""},
                                      {"new_value", "Top"}})});
    const auto cs = core::audit::ComputeChangeSet({create, edit});
    EXPECT_TRUE(cs.added.count("G1"));
    EXPECT_FALSE(cs.modified.count("G1"));
    EXPECT_TRUE(cs.deleted.empty());
}

namespace {

core::audit::AuditTransaction MakeTxWithMeta(std::uint64_t seq,
                                              const char* timestamp,
                                              const char* author,
                                              std::vector<core::audit::AuditEvent> events) {
    core::audit::AuditTransaction tx = MakeTx(seq, std::move(events));
    tx.timestamp = timestamp;
    tx.author = author;
    return tx;
}

} // namespace

TEST(ElementHistorySummary, NoTransactionsReportsNotSeen) {
    const auto s = core::audit::SummarizeElementHistory("G1", {});
    EXPECT_FALSE(s.ever_seen);
    EXPECT_FALSE(s.exists);
    EXPECT_EQ(s.change_count, 0u);
}

TEST(ElementHistorySummary, CreateAndEditCountsAndCapturesMetadata) {
    auto create = MakeTxWithMeta(1, "2026-01-01T00:00:00Z", "alice",
                                 {MakeEvent("CreateTopGoal", {{"generated_id", "G1"}})});
    auto edit = MakeTxWithMeta(2, "2026-01-02T00:00:00Z", "bob",
                               {MakeEvent("UpdateElementText",
                                          {{"element_id", "G1"},
                                           {"field", "name"},
                                           {"language", "en"},
                                           {"old_value", ""},
                                           {"new_value", "Top"}})});
    const auto s = core::audit::SummarizeElementHistory("G1", {create, edit});
    EXPECT_TRUE(s.ever_seen);
    EXPECT_TRUE(s.exists);
    EXPECT_EQ(s.change_count, 2u);
    EXPECT_EQ(s.first_sequence, 1u);
    EXPECT_EQ(s.last_sequence, 2u);
    EXPECT_EQ(s.created_at, "2026-01-01T00:00:00Z");
    EXPECT_EQ(s.created_by, "alice");
    EXPECT_EQ(s.last_changed_at, "2026-01-02T00:00:00Z");
    EXPECT_EQ(s.last_changed_by, "bob");
    EXPECT_FALSE(s.changed_since_baseline);
}

TEST(ElementHistorySummary, DeletedElementExistsFalse) {
    auto create = MakeTxWithMeta(1, "t1", "a", {MakeEvent("CreateTopGoal", {{"generated_id", "G1"}})});
    auto del = MakeTxWithMeta(2, "t2", "b", {MakeEvent("RemoveElement", {{"deleted_ids", {"G1"}}})});
    const auto s = core::audit::SummarizeElementHistory("G1", {create, del});
    EXPECT_TRUE(s.ever_seen);
    EXPECT_FALSE(s.exists);
    EXPECT_EQ(s.change_count, 2u);
}

TEST(ElementHistorySummary, ChangedSinceBaselineWhenLaterEditExists) {
    auto create = MakeTxWithMeta(1, "t1", "a", {MakeEvent("CreateTopGoal", {{"generated_id", "G1"}})});
    auto edit = MakeTxWithMeta(3, "t3", "b",
                               {MakeEvent("UpdateElementText",
                                          {{"element_id", "G1"},
                                           {"field", "name"},
                                           {"language", "en"},
                                           {"old_value", "x"},
                                           {"new_value", "y"}})});
    const auto s = core::audit::SummarizeElementHistory("G1", {create, edit}, /*baseline=*/2u);
    EXPECT_TRUE(s.changed_since_baseline);
}

TEST(ElementHistorySummary, NotChangedSinceBaselineWhenAllChangesBeforeBaseline) {
    auto create = MakeTxWithMeta(1, "t1", "a", {MakeEvent("CreateTopGoal", {{"generated_id", "G1"}})});
    auto edit = MakeTxWithMeta(2, "t2", "b",
                               {MakeEvent("UpdateElementText",
                                          {{"element_id", "G1"},
                                           {"field", "name"},
                                           {"language", "en"},
                                           {"old_value", "x"},
                                           {"new_value", "y"}})});
    const auto s = core::audit::SummarizeElementHistory("G1", {create, edit}, /*baseline=*/5u);
    EXPECT_FALSE(s.changed_since_baseline);
}

TEST(ElementHistorySummary, IgnoresUnrelatedElements) {
    auto create_a = MakeTxWithMeta(1, "t1", "a", {MakeEvent("CreateTopGoal", {{"generated_id", "A"}})});
    auto create_b = MakeTxWithMeta(2, "t2", "b", {MakeEvent("CreateTopGoal", {{"generated_id", "B"}})});
    const auto s = core::audit::SummarizeElementHistory("A", {create_a, create_b});
    EXPECT_TRUE(s.exists);
    EXPECT_EQ(s.change_count, 1u);
    EXPECT_EQ(s.last_sequence, 1u);
}

