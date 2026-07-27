#include "bridge/endpoint.h"

#include "bridge/protocol.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>

namespace {

// The endpoint record lives in the user's runtime directory, which these tests
// must not scribble on. Every platform branch of `UserRuntimeDirectory` reads an
// environment variable first, so redirecting them all points the whole module at
// a temporary directory for the lifetime of one test.
class BridgeEndpointTest : public ::testing::Test {
  protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() /
                ("af-bridge-endpoint-" + std::to_string(::testing::UnitTest::GetInstance()
                                                            ->current_test_info()
                                                            ->line()));
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

    std::filesystem::path root_;

  private:
    void Remember(const std::string& name) {
        const char* value = std::getenv(name.c_str());
        saved_[name]      = value == nullptr ? std::string() : std::string(value);
    }

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

    std::map<std::string, std::string> saved_;
};

TEST_F(BridgeEndpointTest, GivesTwoProjectsTwoDifferentKeys) {
    EXPECT_NE(bridge::ProjectKey("C:/cases/alpha"), bridge::ProjectKey("C:/cases/beta"));
}

TEST_F(BridgeEndpointTest, TreatsSeparatorStylesAsOneProject) {
    EXPECT_EQ(bridge::ProjectKey("C:/cases/alpha"), bridge::ProjectKey("C:\\cases\\alpha"));
}

#ifdef _WIN32
// Windows paths are case-insensitive, so `C:\Cases\X` and `c:\cases\x` are the
// same project. Keying them differently would publish two endpoint records for
// one open project and let the adapter connect to neither.
TEST_F(BridgeEndpointTest, TreatsCaseVariantsAsOneProjectOnWindows) {
    EXPECT_EQ(bridge::ProjectKey("C:/Cases/Alpha"), bridge::ProjectKey("c:/cases/alpha"));
}
#endif

// The key ends up inside a Windows pipe name and a POSIX `sun_path`, both of
// which have hard length limits.
TEST_F(BridgeEndpointTest, KeepsTheKeyShort) {
    const std::string key = bridge::ProjectKey("C:/cases/alpha");
    EXPECT_EQ(key.size(), 16u);
    EXPECT_EQ(key.find_first_not_of("0123456789abcdef"), std::string::npos);
}

TEST_F(BridgeEndpointTest, PutsTheRecordOutsideTheProject) {
    const std::filesystem::path project = root_ / "project";
    std::filesystem::create_directories(project);

    const std::filesystem::path record = bridge::EndpointRecordPath(project);

    // A pipe name, a pid and a token describe this machine at this moment. In
    // the project directory they would be hash-tracked by `af.proj` and would
    // reach a colleague through version control.
    EXPECT_EQ(record.string().find(project.string()), std::string::npos);
    EXPECT_EQ(record.extension(), ".json");
}

TEST_F(BridgeEndpointTest, RoundTripsARecord) {
    bridge::EndpointRecord written;
    written.protocol     = bridge::kProtocolVersion;
    written.pid          = 4242;
    written.address      = bridge::EndpointAddressFor("C:/cases/alpha");
    written.token        = bridge::GenerateToken();
    written.project_root = "C:/cases/alpha";
    written.app_version  = "0.1.0";

    std::string error;
    ASSERT_TRUE(bridge::WriteEndpointRecord(written, error)) << error;

    bridge::EndpointRecord read;
    ASSERT_TRUE(bridge::ReadEndpointRecord("C:/cases/alpha", read, error)) << error;

    EXPECT_EQ(read.protocol, written.protocol);
    EXPECT_EQ(read.pid, written.pid);
    EXPECT_EQ(read.address, written.address);
    EXPECT_EQ(read.token, written.token);
    EXPECT_EQ(read.app_version, "0.1.0");
}

TEST_F(BridgeEndpointTest, ReportsAnAbsentRecordRatherThanInventingOne) {
    bridge::EndpointRecord read;
    std::string            error;
    EXPECT_FALSE(bridge::ReadEndpointRecord("C:/cases/never-opened", read, error));
    EXPECT_FALSE(error.empty());
}

// Half a record is as useless as none, and connecting with an empty token would
// look to the application like an unauthorized caller rather than a stale file.
TEST_F(BridgeEndpointTest, RejectsARecordMissingItsAddressOrToken) {
    const std::filesystem::path record = bridge::EndpointRecordPath("C:/cases/partial");
    std::filesystem::create_directories(record.parent_path());
    std::ofstream(record) << R"({"protocol":1,"pid":1,"address":"","token":""})";

    bridge::EndpointRecord read;
    std::string            error;
    EXPECT_FALSE(bridge::ReadEndpointRecord("C:/cases/partial", read, error));
    EXPECT_FALSE(error.empty());
}

TEST_F(BridgeEndpointTest, RejectsARecordThatIsNotJson) {
    const std::filesystem::path record = bridge::EndpointRecordPath("C:/cases/corrupt");
    std::filesystem::create_directories(record.parent_path());
    std::ofstream(record) << "{ this is not json";

    bridge::EndpointRecord read;
    std::string            error;
    EXPECT_FALSE(bridge::ReadEndpointRecord("C:/cases/corrupt", read, error));
}

TEST_F(BridgeEndpointTest, RemovesTheRecordOnShutdown) {
    bridge::EndpointRecord written;
    written.protocol     = bridge::kProtocolVersion;
    written.address      = "addr";
    written.token        = "token";
    written.project_root = "C:/cases/alpha";

    std::string error;
    ASSERT_TRUE(bridge::WriteEndpointRecord(written, error)) << error;
    ASSERT_TRUE(std::filesystem::exists(bridge::EndpointRecordPath("C:/cases/alpha")));

    bridge::RemoveEndpointRecord("C:/cases/alpha");
    EXPECT_FALSE(std::filesystem::exists(bridge::EndpointRecordPath("C:/cases/alpha")));
}

// The token is the second gate in front of a running safety-case editor. A
// predictable one is not a gate.
TEST_F(BridgeEndpointTest, GeneratesLongDistinctTokens) {
    std::set<std::string> tokens;
    for (int index = 0; index < 32; ++index) {
        const std::string token = bridge::GenerateToken();
        EXPECT_EQ(token.size(), 64u);
        EXPECT_EQ(token.find_first_not_of("0123456789abcdef"), std::string::npos);
        tokens.insert(token);
    }
    EXPECT_EQ(tokens.size(), 32u);
}

TEST_F(BridgeEndpointTest, GivesTwoProjectsDifferentAddresses) {
    EXPECT_NE(bridge::EndpointAddressFor("C:/cases/alpha"),
              bridge::EndpointAddressFor("C:/cases/beta"));
}

} // namespace
