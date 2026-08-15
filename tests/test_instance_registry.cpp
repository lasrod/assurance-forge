#include "bridge/instance_registry.h"

#include "bridge/protocol.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#ifdef _WIN32
#include <process.h>
#else
#include <sys/un.h>
#include <unistd.h>
#endif
#include <fstream>
#include <map>
#include <set>
#include <string>

namespace {

long long OwnPid() {
#ifdef _WIN32
    return static_cast<long long>(_getpid());
#else
    return static_cast<long long>(::getpid());
#endif
}

// A pid that names no process: pids are small positive integers handed out by
// the operating system, and this one is far above any real pid table.
constexpr long long kDeadPid = 0x7FFFFFF0LL;

// The instance records live in the user's runtime directory, which these tests
// must not scribble on. Every platform branch of `UserRuntimeDirectory` reads an
// environment variable first, so redirecting them all points the whole module at
// a temporary directory for the lifetime of one test.
class InstanceRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ =
            std::filesystem::temp_directory_path() /
            ("af-instance-registry-" + std::to_string(::testing::UnitTest::GetInstance()->current_test_info()->line()));
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);

        Remember("LOCALAPPDATA");
        Remember("XDG_RUNTIME_DIR");
        Remember("HOME");
        Set("LOCALAPPDATA", root_.string());
        Set("XDG_RUNTIME_DIR", root_.string());
        Set("HOME", root_.string());
    }

    void TearDown() override {
        for (const std::pair<const std::string, std::string>& entry : saved_) {
            if (entry.second.empty()) {
                Unset(entry.first);
            } else {
                Set(entry.first, entry.second);
            }
        }
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    static bridge::InstanceRecord
    MakeRecord(const std::string& instance_id, long long pid, const std::string& project_key = {}) {
        bridge::InstanceRecord record;
        record.protocol = bridge::kProtocolVersion;
        record.instance_id = instance_id;
        record.pid = pid;
        record.address = bridge::InstanceAddress(instance_id);
        record.token = bridge::GenerateToken();
        record.app_version = "0.1.0";
        record.state = project_key.empty() ? bridge::instance_state::kNoProject : bridge::instance_state::kProjectOpen;
        record.project_key = project_key;
        record.last_heartbeat_utc = "2026-08-15T00:00:00Z";
        return record;
    }

    std::filesystem::path root_;

    static void Set(const std::string& name, const std::string& value) {
#ifdef _WIN32
        _putenv_s(name.c_str(), value.c_str());
#else
        setenv(name.c_str(), value.c_str(), 1);
#endif
    }

    static void Unset(const std::string& name) {
#ifdef _WIN32
        _putenv_s(name.c_str(), "");
#else
        unsetenv(name.c_str());
#endif
    }

private:
    void Remember(const std::string& name) {
        const char* value = std::getenv(name.c_str());
        saved_[name] = value == nullptr ? std::string() : std::string(value);
    }

    std::map<std::string, std::string> saved_;
};

TEST_F(InstanceRegistryTest, GivesTwoProjectsTwoDifferentKeys) {
    EXPECT_NE(bridge::ProjectKey("C:/cases/alpha"), bridge::ProjectKey("C:/cases/beta"));
}

#ifdef _WIN32
// Windows accepts either separator for the same path, so `C:\cases\alpha` and
// `C:/cases/alpha` are one project. Keying them differently would let two
// spellings of one open project disagree about which instance has it.
TEST_F(InstanceRegistryTest, TreatsSeparatorStylesAsOneProjectOnWindows) {
    EXPECT_EQ(bridge::ProjectKey("C:/cases/alpha"), bridge::ProjectKey("C:\\cases\\alpha"));
}

// Windows paths are case-insensitive, so `C:\Cases\X` and `c:\cases\x` are the
// same project.
TEST_F(InstanceRegistryTest, TreatsCaseVariantsAsOneProjectOnWindows) {
    EXPECT_EQ(bridge::ProjectKey("C:/Cases/Alpha"), bridge::ProjectKey("c:/cases/alpha"));
}
#else
// The same two claims are false on POSIX, and asserting them there is what had
// CI red on every commit of this branch. A backslash is an ordinary character in
// a POSIX filename and case is significant, so `a\b` and `a/b` name different
// things and must key differently -- folding them together would treat two
// projects as one, which is the failure the key exists to prevent.
TEST_F(InstanceRegistryTest, TreatsSeparatorStylesAsDifferentProjectsOnPosix) {
    EXPECT_NE(bridge::ProjectKey("/cases/alpha"), bridge::ProjectKey("/cases\\alpha"));
}

TEST_F(InstanceRegistryTest, TreatsCaseVariantsAsDifferentProjectsOnPosix) {
    EXPECT_NE(bridge::ProjectKey("/Cases/Alpha"), bridge::ProjectKey("/cases/alpha"));
}
#endif

#ifndef _WIN32
// The whole address has to fit `sockaddr_un::sun_path` -- 108 bytes on Linux,
// 104 on macOS -- and a short instance id is not on its own enough, because the
// runtime directory in front of it is the part that varies.
//
// This is asserted against the fallback the product picks when the user has set
// no `XDG_RUNTIME_DIR`, since that is the path a real macOS user gets.
TEST_F(InstanceRegistryTest, KeepsTheWholeAddressWithinTheSocketPathLimit) {
    Unset("XDG_RUNTIME_DIR");
    Set("HOME", "/Users/a-reasonably-long-user-name");

    // Checked against the *tightest* limit of the platforms this ships on, not
    // against the one this build happens to have. `sun_path` is 108 bytes on
    // Linux and 104 on macOS, so a Linux-only assertion would let a macOS break
    // through.
    constexpr std::size_t kTightestSunPath = 104;
    static_assert(kTightestSunPath <= sizeof(sockaddr_un::sun_path),
                  "this platform is tighter than macOS; lower the bound");

    const std::string address = bridge::InstanceAddress(bridge::GenerateInstanceId());
    EXPECT_LT(address.size(), kTightestSunPath) << address;
}
#endif

// The key ends up inside the record and in `hello`; the instance id ends up
// inside a Windows pipe name and a POSIX `sun_path`, both of which have hard
// length limits.
TEST_F(InstanceRegistryTest, KeepsKeysAndInstanceIdsShort) {
    const std::string key = bridge::ProjectKey("C:/cases/alpha");
    EXPECT_EQ(key.size(), 16u);
    EXPECT_EQ(key.find_first_not_of("0123456789abcdef"), std::string::npos);

    const std::string instance_id = bridge::GenerateInstanceId();
    EXPECT_EQ(instance_id.rfind("af-", 0), 0u);
    EXPECT_EQ(instance_id.size(), 3u + 16u);
}

TEST_F(InstanceRegistryTest, RoundTripsARecordThroughEnumeration) {
    const bridge::InstanceRecord written = MakeRecord("af-roundtrip00000", OwnPid(), "0123456789abcdef");
    std::string error;
    ASSERT_TRUE(bridge::WriteInstanceRecord(written, error)) << error;

    const std::vector<bridge::InstanceRecord> records = bridge::EnumerateInstanceRecords();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].protocol, written.protocol);
    EXPECT_EQ(records[0].instance_id, written.instance_id);
    EXPECT_EQ(records[0].pid, written.pid);
    EXPECT_EQ(records[0].address, written.address);
    EXPECT_EQ(records[0].token, written.token);
    EXPECT_EQ(records[0].app_version, "0.1.0");
    EXPECT_EQ(records[0].state, bridge::instance_state::kProjectOpen);
    EXPECT_EQ(records[0].project_key, "0123456789abcdef");
    EXPECT_EQ(records[0].last_heartbeat_utc, "2026-08-15T00:00:00Z");
}

// The record is discovery metadata. Where the user keeps their safety cases is
// not something every local process gets to read from a listing, so the record
// carries a fingerprint, never the path.
TEST_F(InstanceRegistryTest, RecordDisclosesNoProjectPath) {
    const std::filesystem::path project = root_ / "Sensitive-Customer-Case";
    std::filesystem::create_directories(project);

    bridge::InstanceRecord record = MakeRecord("af-nodisclose0000", OwnPid(), bridge::ProjectKey(project));
    std::string error;
    ASSERT_TRUE(bridge::WriteInstanceRecord(record, error)) << error;

    std::ifstream file(bridge::InstancesDirectory() / "af-nodisclose0000.json", std::ios::binary);
    ASSERT_TRUE(file.is_open());
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content.find("Sensitive-Customer-Case"), std::string::npos);
}

TEST_F(InstanceRegistryTest, EnumerationSkipsPartialAndCorruptRecords) {
    std::filesystem::create_directories(bridge::InstancesDirectory());
    std::ofstream(bridge::InstancesDirectory() / "af-partial.json")
        << R"({"protocol":2,"instanceId":"af-partial","pid":1,"address":"","token":""})";
    std::ofstream(bridge::InstancesDirectory() / "af-corrupt.json") << "{ this is not json";

    EXPECT_TRUE(bridge::EnumerateInstanceRecords().empty());
}

TEST_F(InstanceRegistryTest, RemovesTheRecordOnShutdown) {
    const bridge::InstanceRecord record = MakeRecord("af-removed0000000", OwnPid());
    std::string error;
    ASSERT_TRUE(bridge::WriteInstanceRecord(record, error)) << error;
    ASSERT_TRUE(std::filesystem::exists(bridge::InstancesDirectory() / "af-removed0000000.json"));

    bridge::RemoveInstanceRecord("af-removed0000000");
    EXPECT_FALSE(std::filesystem::exists(bridge::InstancesDirectory() / "af-removed0000000.json"));
}

TEST_F(InstanceRegistryTest, KnowsItsOwnProcessIsAlive) {
    EXPECT_TRUE(bridge::IsProcessAlive(OwnPid()));
    EXPECT_FALSE(bridge::IsProcessAlive(kDeadPid));
    EXPECT_FALSE(bridge::IsProcessAlive(0));
    EXPECT_FALSE(bridge::IsProcessAlive(-1));
}

// A record is a claim, not proof, that an application is running. Pruning may
// only ever remove the record of a process that is gone -- a live instance's
// record is another process's property.
TEST_F(InstanceRegistryTest, PrunesOnlyRecordsOfDeadProcesses) {
    std::string error;
    ASSERT_TRUE(bridge::WriteInstanceRecord(MakeRecord("af-alive000000000", OwnPid()), error)) << error;
    ASSERT_TRUE(bridge::WriteInstanceRecord(MakeRecord("af-crashed0000000", kDeadPid), error)) << error;

    EXPECT_EQ(bridge::PruneStaleInstanceRecords(), 1);
    const std::vector<bridge::InstanceRecord> records = bridge::EnumerateInstanceRecords();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].instance_id, "af-alive000000000");
}

TEST_F(InstanceRegistryTest, FindsTheLiveInstanceThatHasTheProjectOpen) {
    const std::filesystem::path project = root_ / "alpha";
    std::filesystem::create_directories(project);
    const std::string key = bridge::ProjectKey(project);

    std::string error;
    ASSERT_TRUE(bridge::WriteInstanceRecord(MakeRecord("af-otherproject00", OwnPid(), "ffffffffffffffff"), error))
        << error;
    ASSERT_TRUE(bridge::WriteInstanceRecord(MakeRecord("af-dead0000000000", kDeadPid, key), error)) << error;
    ASSERT_TRUE(bridge::WriteInstanceRecord(MakeRecord("af-match000000000", OwnPid(), key), error)) << error;

    bridge::InstanceRecord found;
    ASSERT_TRUE(bridge::FindInstanceForProject(project, found, error)) << error;
    EXPECT_EQ(found.instance_id, "af-match000000000");

    // The dead claimant was pruned on the way past.
    EXPECT_FALSE(std::filesystem::exists(bridge::InstancesDirectory() / "af-dead0000000000.json"));
}

TEST_F(InstanceRegistryTest, ReportsWhenNoInstanceHasTheProjectOpen) {
    const std::filesystem::path project = root_ / "alpha";
    std::filesystem::create_directories(project);

    std::string error;
    ASSERT_TRUE(bridge::WriteInstanceRecord(MakeRecord("af-noproject00000", OwnPid()), error)) << error;

    bridge::InstanceRecord found;
    EXPECT_FALSE(bridge::FindInstanceForProject(project, found, error));
    EXPECT_FALSE(error.empty());
}

// The advisory single-owner check must see the *other* instance, never report
// the caller's own record back as a conflict.
TEST_F(InstanceRegistryTest, AdvisoryCheckExcludesTheCallersOwnRecord) {
    const std::filesystem::path project = root_ / "alpha";
    std::filesystem::create_directories(project);
    const std::string key = bridge::ProjectKey(project);

    std::string error;
    ASSERT_TRUE(bridge::WriteInstanceRecord(MakeRecord("af-mine0000000000", OwnPid(), key), error)) << error;

    bridge::InstanceRecord other;
    EXPECT_FALSE(bridge::AnotherInstanceHasProjectOpen(project, "af-mine0000000000", other));

    ASSERT_TRUE(bridge::WriteInstanceRecord(MakeRecord("af-theirs00000000", OwnPid(), key), error)) << error;
    ASSERT_TRUE(bridge::AnotherInstanceHasProjectOpen(project, "af-mine0000000000", other));
    EXPECT_EQ(other.instance_id, "af-theirs00000000");
}

// The token is the second gate in front of a running safety-case editor. A
// predictable one is not a gate.
TEST_F(InstanceRegistryTest, GeneratesLongDistinctTokens) {
    std::set<std::string> tokens;
    for (int index = 0; index < 32; ++index) {
        const std::string token = bridge::GenerateToken();
        EXPECT_EQ(token.size(), 64u);
        EXPECT_EQ(token.find_first_not_of("0123456789abcdef"), std::string::npos);
        tokens.insert(token);
    }
    EXPECT_EQ(tokens.size(), 32u);
}

TEST_F(InstanceRegistryTest, GivesTwoInstancesDifferentAddresses) {
    EXPECT_NE(bridge::InstanceAddress("af-one"), bridge::InstanceAddress("af-two"));
}

} // namespace
