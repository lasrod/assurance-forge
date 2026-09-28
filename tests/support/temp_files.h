// Scratch files for tests.
//
// ctest runs each test case in its own process, several at once under -j, so a
// scratch directory must be unique across processes and not only within one. A
// per-process counter alone gives every process the same first name, which let
// parallel tests share, and delete, each other's directories. Naming by gtest's
// random_seed() avoided that but left a new directory behind on every run.
#pragma once

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace test_support {

inline long CurrentProcessId() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

// A new, empty directory under the system temp directory, named
// af_<tag>_<process id>_<n>. It is not removed afterwards; hold it in a TempDir
// for that.
inline std::filesystem::path UniqueTempDirectory(std::string_view tag) {
    static std::atomic<int> counter{0};
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("af_" + std::string(tag) + "_" + std::to_string(CurrentProcessId()) + "_" + std::to_string(++counter));
    // Only a directory an earlier run left behind under a reused process id.
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

// An empty directory under the system temp directory that belongs to the running
// test: named af_<tag>_<hash of the test's full name>, so the same test gets the
// same directory on every run -- emptied here first, so nothing accumulates --
// while tests running in parallel never share one. Two calls with the same tag
// in one test return the same directory.
inline std::filesystem::path TestTempDirectory(std::string_view tag) {
    const ::testing::TestInfo* test = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string test_name =
        test != nullptr ? std::string(test->test_suite_name()) + "." + test->name() : std::string("no_test");
    char hash[17];
    std::snprintf(hash, sizeof(hash), "%016zx", std::hash<std::string>{}(test_name));
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("af_" + std::string(tag) + "_" + std::string(hash, 8));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

// Removes `path` and everything under it when it goes out of scope.
struct TempDir {
    std::filesystem::path path;

    TempDir() = default;
    explicit TempDir(std::filesystem::path value) : path(std::move(value)) {}
    TempDir(TempDir&& other) noexcept : path(std::move(other.path)) {
        other.path.clear();
    }
    TempDir& operator=(TempDir&& other) noexcept {
        if (this != &other) {
            Remove();
            path = std::move(other.path);
            other.path.clear();
        }
        return *this;
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    ~TempDir() {
        Remove();
    }

private:
    void Remove() noexcept {
        if (path.empty())
            return;
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

// Writes `content` to `path`, creating its parent directories.
inline void WriteFile(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

// The bytes of `path`, or empty when it cannot be read.
inline std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace test_support
