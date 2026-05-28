#include "core/audit/audit_paths.h"
#include "core/audit/event_store.h"
#include "core/project_file_io.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string_view>

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

// Simulates a process crash mid-append: the trailing line is partially
// written and never terminated by '\n'. Open must truncate the torn tail,
// surface a diagnostic, and leave the store appendable.
TEST(EventStoreJsonl, RecoversFromTornFinalLine) {
    auto root = MakeTempProjectRoot("evstore_torn");
    std::string error;
    auto store = core::audit::EventStore::Open(root, error);
    ASSERT_TRUE(store) << error;

    auto tx1 = MakeTransaction("CmdA");
    ASSERT_TRUE(store->Append(tx1, error)) << error;
    auto tx2 = MakeTransaction("CmdB");
    ASSERT_TRUE(store->Append(tx2, error)) << error;

    // Append a partial line that has no terminating newline. This
    // simulates a crash between write and fsync of a third transaction.
    const auto log = core::audit::EventLogPath(root);
    {
        std::ofstream out(log, std::ios::binary | std::ios::app);
        const std::string torn = R"({"transaction_id":"partial","transaction_sequence":3,"events":[)";
        out.write(torn.data(), static_cast<std::streamsize>(torn.size()));
    }

    auto store2 = core::audit::EventStore::Open(root, error);
    ASSERT_TRUE(store2) << error;
    EXPECT_TRUE(store2->TornTailRecovered());
    EXPECT_FALSE(store2->TornTailDiagnostic().empty());
    // First two transactions survived.
    EXPECT_EQ(store2->LatestTransactionSequence(), 2u);
    ASSERT_EQ(store2->Transactions().size(), 2u);

    // The store must be appendable after recovery, and the new line must
    // chain off the last surviving good line.
    auto tx3 = MakeTransaction("CmdC");
    ASSERT_TRUE(store2->Append(tx3, error)) << error;
    EXPECT_EQ(tx3.transaction_sequence, 3u);
    EXPECT_EQ(tx3.previous_transaction_hash, store2->Transactions()[1].previous_transaction_hash.empty()
                                                 ? std::string{}
                                                 : tx3.previous_transaction_hash);

    // A fresh Open of the repaired log should now load cleanly (no torn flag).
    auto store3 = core::audit::EventStore::Open(root, error);
    ASSERT_TRUE(store3) << error;
    EXPECT_FALSE(store3->TornTailRecovered());
    EXPECT_EQ(store3->LatestTransactionSequence(), 3u);

    std::filesystem::remove_all(root);
}

// Atomic write of the SACM/manifest must replace the destination file
// even when an old `.tmp` sidecar already exists (leftover from a prior
// crash) — a stale temp should not block the next save.
TEST(EventStoreJsonl, WriteTextFileAtomicReplacesExistingAndCleansStaleTemp) {
    auto root = MakeTempProjectRoot("atomic_write");
    auto target = root / "data.txt";
    auto tmp = root / "data.txt.tmp";

    // Pre-create a stale temp file and an existing target.
    {
        std::ofstream(tmp, std::ios::binary) << "stale leftover";
        std::ofstream(target, std::ios::binary) << "original content";
    }
    ASSERT_TRUE(std::filesystem::exists(tmp));

    auto r = core::WriteTextFileAtomic(target, std::string_view{"new content"});
    ASSERT_TRUE(r.has_value()) << (r ? std::string{} : r.error());

    std::ifstream in(target, std::ios::binary);
    std::string got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    EXPECT_EQ(got, "new content");
    // No sidecar tmp left behind on success.
    EXPECT_FALSE(std::filesystem::exists(tmp));

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
