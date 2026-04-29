#include <gtest/gtest.h>

#include "ai/ai_task_runner.h"

#include <chrono>
#include <stdexcept>

TEST(AiTaskRunnerTest, ReportsRunningThenSuccess) {
    ai::AiTaskRunner runner;
    auto handle = runner.RunConnectionTest([]() {
        return ai::SuccessStatus("done");
    });

    ai::AiTaskSnapshot first = handle->Snapshot();
    EXPECT_TRUE(first.state == ai::AiTaskState::Running || first.state == ai::AiTaskState::Success);

    bool completed = handle->WaitUntilComplete(std::chrono::milliseconds(5000));
    EXPECT_TRUE(completed);

    ai::AiTaskSnapshot latest = handle->Snapshot();
    EXPECT_EQ(latest.state, ai::AiTaskState::Success);
    EXPECT_EQ(latest.status.message, "done");
}

TEST(AiTaskRunnerTest, CapturesThrownExceptionAsError) {
    ai::AiTaskRunner runner;
    auto handle = runner.RunConnectionTest([]() -> ai::AiConnectionStatus {
        throw std::runtime_error("failure");
    });

    bool completed = handle->WaitUntilComplete(std::chrono::milliseconds(5000));
    EXPECT_TRUE(completed);

    ai::AiTaskSnapshot latest = handle->Snapshot();
    EXPECT_EQ(latest.state, ai::AiTaskState::Error);
    EXPECT_EQ(latest.status.errorCode, ai::AiErrorCode::Unknown);
}

TEST(AiTaskRunnerTest, RunsGenerateTaskAndCapturesResponse) {
    ai::AiTaskRunner runner;
    auto handle = runner.RunGenerate([]() {
        ai::AiResponse response;
        response.success = true;
        response.text = R"({"findings":[]})";
        return response;
    });

    bool completed = handle->WaitUntilComplete(std::chrono::milliseconds(5000));
    EXPECT_TRUE(completed);

    ai::AiTaskSnapshot latest = handle->Snapshot();
    EXPECT_EQ(latest.state, ai::AiTaskState::Success);
    EXPECT_TRUE(latest.response.success);
    EXPECT_EQ(latest.response.text, R"({"findings":[]})");
}

TEST(AiTaskRunnerTest, GenerateExceptionBecomesErrorResponse) {
    ai::AiTaskRunner runner;
    auto handle = runner.RunGenerate([]() -> ai::AiResponse {
        throw std::runtime_error("generate failed");
    });

    bool completed = handle->WaitUntilComplete(std::chrono::milliseconds(5000));
    EXPECT_TRUE(completed);

    ai::AiTaskSnapshot latest = handle->Snapshot();
    EXPECT_EQ(latest.state, ai::AiTaskState::Error);
    EXPECT_FALSE(latest.response.success);
    EXPECT_EQ(latest.response.errorCode, ai::AiErrorCode::Unknown);
    EXPECT_EQ(latest.response.errorMessage, "generate failed");
}

