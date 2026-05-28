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
#include <string_view>

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

    // Verify chain while parsing. Track the byte range of each successfully
    // parsed line so we can recover from a torn final write (process crash
    // or power loss between the partial write and the fsync).
    //
    // Torn-write recovery rule: ONLY a final line that lacks a terminating
    // newline is considered torn. A line that ends cleanly with '\n' but
    // fails to parse or chains incorrectly is treated as tampering — we
    // refuse to load and surface the error so the user can decide.
    std::string previous_line_hash;
    std::size_t pos = 0;
    std::size_t last_good_end = 0; // byte offset one past the last good '\n'
    bool        truncated_recovery_needed = false;
    std::string truncated_diag;
    while (pos < content.size()) {
        const std::size_t eol = content.find('\n', pos);
        const bool        has_newline = (eol != std::string::npos);
        std::string       line = content.substr(pos, has_newline ? eol - pos : std::string::npos);
        const std::size_t next_pos = has_newline ? eol + 1 : content.size();

        if (line.empty()) {
            if (has_newline) {
                last_good_end = next_pos; // blank line, harmless
                pos = next_pos;
                continue;
            }
            // Trailing whitespace with no newline — treat as torn tail.
            truncated_recovery_needed = true;
            truncated_diag = "Trailing partial line had no content";
            break;
        }

        // No terminating newline => torn write. Don't try to parse; truncate
        // to the previous good line.
        if (!has_newline) {
            truncated_recovery_needed = true;
            truncated_diag = "Final line is missing newline terminator (torn write at tx #" +
                             std::to_string(store->latest_transaction_sequence_ + 1) + ")";
            break;
        }

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
        last_good_end = next_pos;
        pos = next_pos;
    }

    if (truncated_recovery_needed) {
        // Repair: truncate the on-disk log to the last fully-committed line
        // so subsequent appends chain cleanly. Do this via temp-file +
        // atomic rename so the repair itself is crash-safe.
        const std::string repaired(content.data(), last_good_end);
        auto              w = WriteTextFileAtomic(store->log_path_, repaired);
        if (!w) {
            error = "Failed to truncate torn tail of event log: " + w.error();
            return nullptr;
        }
        // Re-read for hash; cheaper than tracking a running hash here.
        auto repaired_bytes = ReadFileBytes(store->log_path_);
        if (repaired_bytes)
            store->event_store_hash_ =
                repaired_bytes->empty() ? std::string{} : Sha256::HexDigest(*repaired_bytes);
        store->latest_transaction_hash_ = previous_line_hash;
        // Emit a single recovery diagnostic so callers (VerifyProject /
        // status bar) can surface it; we intentionally do not fail Open.
        // The caller's `error` string is reserved for hard failures, so we
        // attach the diagnostic via the in-memory recovery flag.
        store->torn_tail_recovered_ = true;
        store->torn_tail_diagnostic_ = std::move(truncated_diag);
        return store;
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

    // Single-write append + fsync. Composing the full payload (`line + \n`)
    // into one buffer means a single `write()` syscall, which is the
    // practical atomic unit for a single-process appender on a regular
    // file. After the buffered write we flush and call FlushFileBuffers /
    // fsync so committed transactions survive power loss.
    std::string payload;
    payload.reserve(line.size() + 1);
    payload.append(line);
    payload.push_back('\n');

    {
        std::ofstream out(log_path_, std::ios::binary | std::ios::app);
        if (!out) {
            error = "Failed to open event log for append: " + log_path_.string();
            return false;
        }
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        out.flush();
        if (!out) {
            error = "Failed to write transaction to event log";
            return false;
        }
        // ofstream destructor closes the file at scope end.
    }

    std::string fsync_err;
    if (!FsyncFile(log_path_, fsync_err)) {
        // The bytes are buffered but not durably on disk. Report as failure
        // so the caller can surface it; the next Open will torn-recover if
        // the bytes actually never reach disk after a crash.
        error = "Event log fsync failed: " + fsync_err;
        return false;
    }

    latest_transaction_sequence_ = transaction.transaction_sequence;
    latest_event_sequence_ = next_event_sequence;
    latest_transaction_hash_ = Sha256::HexDigest(line);
    transactions_.push_back(transaction);

    // Update the running file hash by re-reading. O(n) per append but
    // acceptable for current log sizes; the appended bytes are already
    // durable on disk by this point, so a failure here only leaves the
    // in-memory hash stale.
    auto bytes = ReadFileBytes(log_path_);
    if (bytes)
        event_store_hash_ = Sha256::HexDigest(*bytes);
    return true;
}

} // namespace core::audit
