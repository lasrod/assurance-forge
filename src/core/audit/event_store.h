#pragma once

#include "core/audit/audit_transaction.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Append-only JSON-Lines event store backed by
// `<project_root>/.af/audit/transactions.af.jsonl` (design §9).
//
// Each line is one serialized `AuditTransaction`. Transactions are chained:
// every transaction records the sha256 of the immediately-previous line so a
// reader can verify the log has not been tampered with by recomputing
// hashes during `Open`.
//
// The store assigns the next available transaction and event sequence
// numbers when `Append` is called; callers should leave those fields at zero
// in the transaction they pass in.
namespace core::audit {

class EventStore {
public:
    // Open (or create) the event store rooted at `project_root`. Verifies the
    // hash chain of any existing log; populates `error` and returns nullptr
    // on tamper detection or I/O failure.
    static std::unique_ptr<EventStore> Open(const std::filesystem::path& project_root, std::string& error);

    // Append a new transaction. On success the transaction's sequence
    // numbers, timestamp (if blank), previous_transaction_hash and event
    // sequences are filled in. Returns false on I/O failure.
    bool Append(AuditTransaction& transaction, std::string& error);

    const std::vector<AuditTransaction>& Transactions() const { return transactions_; }
    std::uint64_t                        LatestTransactionSequence() const { return latest_transaction_sequence_; }
    std::uint64_t                        LatestEventSequence() const { return latest_event_sequence_; }
    // sha256 hex of the entire on-disk log content (empty when the log is empty).
    const std::string& EventStoreHash() const { return event_store_hash_; }
    // sha256 hex of the most recently appended transaction line (empty for empty log).
    const std::string& LatestTransactionHash() const { return latest_transaction_hash_; }
    const std::filesystem::path& LogPath() const { return log_path_; }

    // True if `Open` truncated a torn (partially written) trailing line.
    // The on-disk log was repaired in place and is safe to append to;
    // `TornTailDiagnostic()` describes what was discarded so the UI can
    // surface a non-fatal warning instead of forcing destructive reconcile.
    bool               TornTailRecovered() const { return torn_tail_recovered_; }
    const std::string& TornTailDiagnostic() const { return torn_tail_diagnostic_; }

private:
    EventStore() = default;

    std::filesystem::path         log_path_;
    std::vector<AuditTransaction> transactions_;
    std::uint64_t                 latest_transaction_sequence_ = 0;
    std::uint64_t                 latest_event_sequence_ = 0;
    std::string                   event_store_hash_;
    std::string                   latest_transaction_hash_;
    bool                          torn_tail_recovered_ = false;
    std::string                   torn_tail_diagnostic_;
};

} // namespace core::audit
