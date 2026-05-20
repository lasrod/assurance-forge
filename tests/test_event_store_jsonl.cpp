#include "core/audit/audit_paths.h"
#include "core/audit/event_store.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path MakeTempProjectRoot(const std::string& tag) {
    auto root = std::filesystem::temp_directory_path() / ("af_test_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

core::audit::AuditTransaction MakeTransaction(const std::string& command) {
    core::audit::AuditTransaction tx;
    tx.command_name = command;
    tx.author = "tester";
    core::audit::AuditEvent ev;
    ev.event_type = "Noop";
    ev.payload = nlohmann::ordered_json::object();
    ev.payload["command"] = command;
    tx.events.push_back(ev);
    return tx;
}

} // namespace

TEST(EventStoreJsonl, AppendAssignsMonotonicSequencesAndPersistsLines) {
    auto root = MakeTempProjectRoot("evstore_append");
    std::string error;
    auto store = core::audit::EventStore::Open(root, error);
    ASSERT_TRUE(store) << error;
    EXPECT_EQ(store->LatestTransactionSequence(), 0u);
    EXPECT_EQ(store->LatestEventSequence(), 0u);

    auto tx1 = MakeTransaction("CmdA");
    ASSERT_TRUE(store->Append(tx1, error)) << error;
    EXPECT_EQ(tx1.transaction_sequence, 1u);
    EXPECT_EQ(tx1.events.front().event_sequence, 1u);
    EXPECT_TRUE(tx1.previous_transaction_hash.empty());

    auto tx2 = MakeTransaction("CmdB");
    ASSERT_TRUE(store->Append(tx2, error)) << error;
    EXPECT_EQ(tx2.transaction_sequence, 2u);
    EXPECT_EQ(tx2.events.front().event_sequence, 2u);
    EXPECT_FALSE(tx2.previous_transaction_hash.empty());

    // Reload and verify chain.
    auto store2 = core::audit::EventStore::Open(root, error);
    ASSERT_TRUE(store2) << error;
    EXPECT_EQ(store2->LatestTransactionSequence(), 2u);
    EXPECT_EQ(store2->LatestEventSequence(), 2u);
    ASSERT_EQ(store2->Transactions().size(), 2u);
    EXPECT_EQ(store2->Transactions()[1].previous_transaction_hash, tx2.previous_transaction_hash);
    EXPECT_FALSE(store2->EventStoreHash().empty());

    std::filesystem::remove_all(root);
}

TEST(EventStoreJsonl, DetectsTamperedLog) {
    auto root = MakeTempProjectRoot("evstore_tamper");
    std::string error;
    auto store = core::audit::EventStore::Open(root, error);
    ASSERT_TRUE(store) << error;

    auto tx1 = MakeTransaction("CmdA");
    ASSERT_TRUE(store->Append(tx1, error)) << error;
    auto tx2 = MakeTransaction("CmdB");
    ASSERT_TRUE(store->Append(tx2, error)) << error;

    // Tamper: rewrite the first line's command_name, breaking the chain.
    const auto log = core::audit::EventLogPath(root);
    std::ifstream in(log, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    auto pos = content.find("CmdA");
    ASSERT_NE(pos, std::string::npos);
    content.replace(pos, 4, "CmdX");
    std::ofstream out(log, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();

    auto store_bad = core::audit::EventStore::Open(root, error);
    EXPECT_FALSE(store_bad);
    EXPECT_NE(error.find("hash chain"), std::string::npos);

    std::filesystem::remove_all(root);
}

TEST(EventStoreJsonl, RefusesEmptyTransaction) {
    auto root = MakeTempProjectRoot("evstore_empty");
    std::string error;
    auto store = core::audit::EventStore::Open(root, error);
    ASSERT_TRUE(store) << error;

    core::audit::AuditTransaction empty;
    empty.command_name = "Noop";
    EXPECT_FALSE(store->Append(empty, error));

    std::filesystem::remove_all(root);
}
