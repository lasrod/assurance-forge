#pragma once

// How the MCP adapter finds a running Assurance Forge application.
//
// The application publishes one record per running instance, keyed by a
// runtime instance id -- not by project. The listener outlives project
// open/close/switch (ADR 0014), so discovery answers "which applications are
// running", and the record's project fingerprint answers "which of them has
// the project I need" without disclosing the path itself.
//
// **Machine-local state, so it never goes in the project.** A pipe name, a pid
// and a token describe this machine at this moment. Writing them into the
// project directory would put them in front of `af.proj`'s hash tracking, into
// version control, and onto a colleague's machine after a `git pull` -- so they
// live in the user's runtime directory instead.
//
// A record is a claim, not proof, that an application is running: liveness is
// the recorded pid, and a record whose process is gone is ignored and pruned
// by whichever side meets it first.

#include <filesystem>
#include <string>
#include <vector>

namespace bridge {

namespace instance_state {
// Coarse, deliberately content-free states for the discovery record. What the
// open project *is* stays out of the record; only whether there is one.
inline constexpr const char* kNoProject = "no_project";
inline constexpr const char* kProjectOpen = "project_open";
} // namespace instance_state

struct InstanceRecord {
    int protocol = 0;
    // "af-" + 16 hex, minted once per application run. Names the record file,
    // the transport address, and nothing else.
    std::string instance_id;
    long long pid = 0;
    // Named pipe name on Windows, socket path on POSIX.
    std::string address;
    std::string token;
    std::string app_version;
    // instance_state::kNoProject or instance_state::kProjectOpen.
    std::string state;
    // Fingerprint of the open project root (see ProjectKey), empty when none.
    // A fingerprint rather than a path: discovery metadata must not disclose
    // where a user keeps their safety cases.
    std::string project_key;
    std::string last_heartbeat_utc;
};

// A short, stable key for a project path: lowercased on Windows because the
// filesystem is case-insensitive there and `C:\Cases\X` must not get a
// different key from `c:\cases\x`. Half a sha256 -- far more than enough to
// keep two paths on one machine apart, and short enough for a pipe name.
std::string ProjectKey(const std::filesystem::path& project_root);

// `<user runtime dir>/assurance-forge/bridge/instances/`.
std::filesystem::path InstancesDirectory();

std::string GenerateInstanceId();

// The transport address for one instance. Per-instance, so two applications
// never collide -- including two with the same project open.
std::string InstanceAddress(const std::string& instance_id);

// 32 bytes of `std::random_device` output as hex. Drawn from the device
// directly rather than used to seed a PRNG: on the platforms we ship,
// `random_device` is backed by the operating system's CSPRNG, and a seeded
// mt19937 would be reconstructible from a few observed tokens.
std::string GenerateToken();

// Writes `<instances dir>/<instance-id>.json`. On Windows this also puts an
// explicit user-only ACL on the instances directory (files inherit it); on
// POSIX the record file is chmod 0600. The token is in this file, and anyone
// who can read it can drive the application.
bool WriteInstanceRecord(const InstanceRecord& record, std::string& error);

void RemoveInstanceRecord(const std::string& instance_id);

// True when the recorded pid names a process that is still running. A pid of
// zero or less is never alive.
bool IsProcessAlive(long long pid);

// Every parseable record in the instances directory, live or not. Unreadable
// and malformed files are skipped: a caller cannot act on the difference.
std::vector<InstanceRecord> EnumerateInstanceRecords();

// Deletes records whose process is gone. Returns how many were removed. Safe
// for either side to call: only a dead instance's record is ever touched.
int PruneStaleInstanceRecords();

// The live instance that has `project_root` open, if there is exactly such an
// instance. Prunes stale records on the way. This is how the project-bound
// adapter finds its application until dynamic session binding replaces
// project matching.
bool FindInstanceForProject(const std::filesystem::path& project_root, InstanceRecord& out, std::string& error);

// Advisory single-owner check (ADR 0014): a *different* live instance that has
// `project_root` open. `own_instance_id` excludes the caller's own record.
bool AnotherInstanceHasProjectOpen(const std::filesystem::path& project_root,
                                   const std::string& own_instance_id,
                                   InstanceRecord& out);

} // namespace bridge
