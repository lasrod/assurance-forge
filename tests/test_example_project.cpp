#include "app/example_project.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

// The welcome screen's "Open the Example Project": the shipped example is never
// opened in place, and a copy the user has worked in is never overwritten.

namespace {

namespace fs = std::filesystem;

void WriteFile(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

class ExampleProjectTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("af_example_project_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "_" +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(root_);
        // The layout a package ships: <exe dir>/examples/kitchen-blender.
        bundled_ = root_ / "install" / "examples" / app::kExampleProjectFolderName;
        WriteFile(bundled_ / "af.proj", "{\"format\": \"assurance-forge-project\"}");
        WriteFile(bundled_ / "arguments" / "main.sacm", "<sacm/>");
        copy_root_ = root_ / "Documents" / "Assurance Forge Examples";
    }
    void TearDown() override {
        fs::remove_all(root_);
    }

    fs::path root_;
    fs::path bundled_;
    fs::path copy_root_;
};

} // namespace

TEST_F(ExampleProjectTest, FindsTheExampleShippedBesideTheExecutable) {
    EXPECT_EQ(app::FindBundledExampleProject(root_ / "install"), bundled_);
}

// A build without the examples submodule ships no example; the welcome screen
// must then not offer one, which it learns from an empty path.
TEST_F(ExampleProjectTest, FindsNothingWhereNoExampleIsShipped) {
    EXPECT_TRUE(app::FindBundledExampleProject(root_ / "elsewhere").empty());
    EXPECT_TRUE(app::FindBundledExampleProject({}).empty());
}

TEST_F(ExampleProjectTest, CopiesTheWholeProjectAndOpensTheCopy) {
    const app::ExampleCopyResult result = app::PrepareExampleProjectCopy(bundled_, copy_root_);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_FALSE(result.reused_existing);
    EXPECT_EQ(result.manifest, copy_root_ / app::kExampleProjectFolderName / "af.proj");
    EXPECT_EQ(ReadFile(copy_root_ / app::kExampleProjectFolderName / "arguments" / "main.sacm"), "<sacm/>");
    EXPECT_NE(result.manifest.parent_path(), bundled_) << "the shipped copy is never the one opened";
}

// The second click must reopen the user's copy as they left it, not replace
// their work with a fresh example.
TEST_F(ExampleProjectTest, ReopensAnEarlierCopyWithoutOverwritingIt) {
    ASSERT_TRUE(app::PrepareExampleProjectCopy(bundled_, copy_root_).success);
    const fs::path edited = copy_root_ / app::kExampleProjectFolderName / "arguments" / "main.sacm";
    WriteFile(edited, "<sacm>my edits</sacm>");

    const app::ExampleCopyResult again = app::PrepareExampleProjectCopy(bundled_, copy_root_);
    ASSERT_TRUE(again.success) << again.error;
    EXPECT_TRUE(again.reused_existing);
    EXPECT_EQ(ReadFile(edited), "<sacm>my edits</sacm>");
}

// A copy interrupted part-way leaves only a ".partial" folder, never one under
// the final name: the next attempt discards it and copies afresh, rather than
// reopening a project with files missing.
TEST_F(ExampleProjectTest, DiscardsAnInterruptedCopyAndStartsAgain) {
    fs::path partial = copy_root_ / app::kExampleProjectFolderName;
    partial += ".partial";
    WriteFile(partial / "af.proj", "{}"); // the manifest made it, the argument did not

    const app::ExampleCopyResult result = app::PrepareExampleProjectCopy(bundled_, copy_root_);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_FALSE(result.reused_existing);
    const fs::path copy = copy_root_ / app::kExampleProjectFolderName;
    EXPECT_EQ(ReadFile(copy / "arguments" / "main.sacm"), "<sacm/>");
    EXPECT_FALSE(fs::exists(partial));
}

// Something else already under the example's name is not ours to replace.
TEST_F(ExampleProjectTest, RefusesAFolderThatIsNotAProjectAndLeavesItAlone) {
    const fs::path copy = copy_root_ / app::kExampleProjectFolderName;
    WriteFile(copy / "notes.txt", "mine");

    const app::ExampleCopyResult result = app::PrepareExampleProjectCopy(bundled_, copy_root_);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
    EXPECT_EQ(ReadFile(copy / "notes.txt"), "mine");
    EXPECT_FALSE(fs::exists(copy / "af.proj"));
}

// .af/ is the audit history of wherever the project was last opened. Carried
// into the copy, it made the example open with "Audit log divergence detected".
TEST_F(ExampleProjectTest, LeavesTheAuditHistoryOfTheShippedCopyBehind) {
    WriteFile(bundled_ / ".af" / "audit" / "log.jsonl", "{}");

    const app::ExampleCopyResult result = app::PrepareExampleProjectCopy(bundled_, copy_root_);
    ASSERT_TRUE(result.success) << result.error;
    const fs::path copy = copy_root_ / app::kExampleProjectFolderName;
    EXPECT_FALSE(fs::exists(copy / ".af"));
    EXPECT_TRUE(fs::exists(copy / "arguments" / "main.sacm"));
}

TEST_F(ExampleProjectTest, RefusesWhenThereIsNothingToCopyOrNowhereToPutIt) {
    const app::ExampleCopyResult missing = app::PrepareExampleProjectCopy(root_ / "nothing", copy_root_);
    EXPECT_FALSE(missing.success);
    EXPECT_FALSE(missing.error.empty());

    const app::ExampleCopyResult nowhere = app::PrepareExampleProjectCopy(bundled_, {});
    EXPECT_FALSE(nowhere.success);
    EXPECT_FALSE(nowhere.error.empty());
}
