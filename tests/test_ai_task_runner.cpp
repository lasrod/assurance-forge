#include <gtest/gtest.h>

#include "ai/ai_task_runner.h"

#include <chrono>
#include <stdexcept>
#include <thread>

TEST(AiTaskRunnerTest, ReportsRunningThenSuccess) {
    ai::AiTaskRunner runner;
    auto handle = runner.RunConnectionTest([]() {
        return ai::SuccessStatus("done");
    });

    ai::AiTaskSnapshot first = handle->Snapshot();
    EXPECT_TRUE(first.state == ai::AiTaskState::Running || first.state == ai::AiTaskState::Success);

    ai::AiTaskSnapshot latest;
    for (int attempt = 0; attempt < 50; ++attempt) {
        latest = handle->Snapshot();
        if (latest.state != ai::AiTaskState::Running) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_EQ(latest.state, ai::AiTaskState::Success);
    EXPECT_EQ(latest.status.message, "done");
}

TEST(AiTaskRunnerTest, CapturesThrownExceptionAsError) {
    ai::AiTaskRunner runner;
    auto handle = runner.RunConnectionTest([]() -> ai::AiConnectionStatus {
        throw std::runtime_error("failure");
    });

    ai::AiTaskSnapshot latest;
    for (int attempt = 0; attempt < 50; ++attempt) {
        latest = handle->Snapshot();
        if (latest.state != ai::AiTaskState::Running) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_EQ(latest.state, ai::AiTaskState::Error);
    EXPECT_EQ(latest.status.errorCode, ai::AiErrorCode::Unknown);
}