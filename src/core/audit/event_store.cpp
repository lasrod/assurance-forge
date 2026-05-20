#include "core/audit/event_store.h"

#include "core/audit/audit_paths.h"
#include "core/project_file_io.h"
#include "core/sha256.h"
#include "core/time_utils.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace core::audit {

namespace {

std::string GenerateTransactionId() {
    // RFC 4122 v4-style id without depending on a UUID library. Sufficient
    // for log uniqueness; not used for cryptographic identification.
    static thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<std::uint64_t> dist;
    const std::uint64_t a = dist(rng);
    const std::uint64_t b = dist(rng);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(16) << a << std::setw(16) << b;
    std::string hex = oss.str();
    // 8-4-4-4-12 layout
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" +
           hex.substr(16, 4) + "-" + hex.substr(20, 12);
}

bool EnsureAuditDirExists(const std::filesystem::path& project_root, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(AuditDir(project_root), ec);
    if (ec) {
        error = "Could not create audit directory: " + ec.message();
        return false;
    }
    return true;
}

} // namespace

std::unique_ptr<EventStore> EventStore::Open(const std::filesystem::path& project_root, std::string& error) {
    if (!EnsureAuditDirExists(project_root, error))
        return nullptr;

    auto store = std::unique_ptr<EventStore>(new EventStore());
    store->log_path_ = EventLogPath(project_root);

    std::error_code ec;
    if (!std::filesystem::exists(store->log_path_, ec)) {
        // Create an empty file so subsequent Append calls have a target.
        auto w = WriteTextFile(store->log_path_, std::string_view{});
        if (!w) {
            error = std::move(w.error());
            return nullptr;
        }
        return store;
    }

    auto bytes = ReadFileBytes(store->log_path_);
    if (!bytes) {
        error = std::move(bytes.error());
        return nullptr;
    }
    std::string content(reinterpret_cast<const char*>(bytes->data()), bytes->size());

    // Verify chain while parsing.
    std::string previous_line_hash;
    std::size_t pos = 0;
    while (pos < content.size()) {
        std::size_t eol = content.find('\n', pos);
        std::string line = content.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = (eol == std::string::npos) ? content.size() : eol + 1;
        if (line.empty())
            continue;

        AuditTransaction tx;
        std::string parse_err;
        if (!ParseAuditTransactionLine(line, tx, parse_err)) {
            error = "Corrupted event log: " + parse_err;
            return nullptr;
        }

        if (tx.previous_transaction_hash != previous_line_hash) {
            error = "Event log hash chain broken at transaction " + std::to_string(tx.transaction_sequence);
            return nullptr;
        }

        store->latest_transaction_sequence_ = tx.transaction_sequence;
        for (const AuditEvent& e : tx.events)
            store->latest_event_sequence_ = std::max(store->latest_event_sequence_, e.event_sequence);
        store->transactions_.push_back(std::move(tx));

        previous_line_hash = Sha256::HexDigest(line);
    }

    store->latest_transaction_hash_ = previous_line_hash;
    store->event_store_hash_ = bytes->empty() ? std::string{} : Sha256::HexDigest(*bytes);
    return store;
}

bool EventStore::Append(AuditTransaction& transaction, std::string& error) {
    if (transaction.events.empty()) {
        error = "Refusing to append empty transaction";
        return false;
    }

    transaction.transaction_sequence = latest_transaction_sequence_ + 1;
    std::uint64_t next_event_sequence = latest_event_sequence_;
    for (AuditEvent& e : transaction.events)
        e.event_sequence = ++next_event_sequence;

    if (transaction.timestamp.empty())
        transaction.timestamp = NowUtcString();
    if (transaction.transaction_id.empty())
        transaction.transaction_id = GenerateTransactionId();
    if (transaction.author.empty())
        transaction.author = "system";
    transaction.previous_transaction_hash = latest_transaction_hash_;

    const std::string line = SerializeAuditTransactionLine(transaction);

    // Append atomically: open in append mode, write `line + "\n"` in a single
    // write. The OS guarantees append-mode writes are atomic up to PIPE_BUF
    // for pipes but not for regular files; for now we keep it simple and
    // rely on POSIX/Win32 buffered append.
    std::ofstream out(log_path_, std::ios::binary | std::ios::app);
    if (!out) {
        error = "Failed to open event log for append: " + log_path_.string();
        return false;
    }
    out.write(line.data(), static_cast<std::streamsize>(line.size()));
    out.put('\n');
    if (!out) {
        error = "Failed to write transaction to event log";
        return false;
    }
    out.close();

    latest_transaction_sequence_ = transaction.transaction_sequence;
    latest_event_sequence_ = next_event_sequence;
    latest_transaction_hash_ = Sha256::HexDigest(line);
    transactions_.push_back(transaction);

    // Update the running file hash by re-reading. Cheap for the sizes we
    // expect; can be optimized later by maintaining an incremental hash.
    auto bytes = ReadFileBytes(log_path_);
    if (bytes)
        event_store_hash_ = Sha256::HexDigest(*bytes);
    return true;
}

} // namespace core::audit
