#pragma once

// How the MCP adapter finds the application that has a given project open.
//
// The application publishes one record per open project; the adapter reads the
// record for the project it was launched against, and connects. No record, a
// stale record, or a refused connection all mean the same thing to the adapter:
// the application is not available, so serve reads from disk and refuse writes.
//
// **Machine-local state, so it never goes in the project.** A pipe name, a pid
// and a token describe this machine at this moment. Writing them into the
// project directory would put them in front of `af.proj`'s hash tracking, into
// version control, and onto a colleague's machine after a `git pull` -- so they
// live in the user's runtime directory instead, keyed by project path. The
// project directory keeps exactly one writer, which is the point of the whole
// change.

#include <filesystem>
#include <string>

namespace bridge {

struct EndpointRecord {
    int protocol = 0;
    long long pid = 0;
    // Named pipe name on Windows, socket path on POSIX.
    std::string address;
    std::string token;
    std::string project_root;
    std::string app_version;
};

// A short, stable key for a project path: lowercased on Windows because the
// filesystem is case-insensitive there and `C:\Cases\X` must not get a
// different endpoint from `c:\cases\x`.
std::string ProjectKey(const std::filesystem::path& project_root);

// `<user runtime dir>/assurance-forge/bridge/<key>.json`.
std::filesystem::path EndpointRecordPath(const std::filesystem::path& project_root);

// The transport address for this project. Per-project, so two projects open in
// two windows do not collide.
std::string EndpointAddressFor(const std::filesystem::path& project_root);

// 32 bytes of `std::random_device` output as hex. Drawn from the device
// directly rather than used to seed a PRNG: on the platforms we ship,
// `random_device` is backed by the operating system's CSPRNG, and a seeded
// mt19937 would be reconstructible from a few observed tokens.
std::string GenerateToken();

bool WriteEndpointRecord(const EndpointRecord& record, std::string& error);

// Absent, unreadable and malformed all report false -- a caller cannot act on
// the difference, and every one of them means "connect to nothing".
bool ReadEndpointRecord(const std::filesystem::path& project_root, EndpointRecord& out, std::string& error);

void RemoveEndpointRecord(const std::filesystem::path& project_root);

} // namespace bridge
