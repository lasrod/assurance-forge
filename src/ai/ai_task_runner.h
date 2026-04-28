#pragma once

#include "ai/ai_types.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace ai {

struct AiTaskSnapshot {
    AiTaskState state = AiTaskState::Idle;
    AiConnectionStatus status;
};

class AiTaskHandle {
public:
    AiTaskSnapshot Snapshot() const;
    bool IsRunning() const;

    // Blocks until the task is no longer Running, or until the timeout elapses.
    // Returns true if the task completed before the timeout.
    bool WaitUntilComplete(std::chrono::milliseconds timeout) const;

private:
    friend class AiTaskRunner;
    struct SharedState {
        mutable std::mutex mutex;
        mutable std::condition_variable cv;
        AiTaskSnapshot snapshot;
    };
    explicit AiTaskHandle(std::shared_ptr<SharedState> state);
    std::shared_ptr<SharedState> state_;
};

class AiTaskRunner {
public:
    std::shared_ptr<AiTaskHandle> RunConnectionTest(std::function<AiConnectionStatus()> job);
};

}  // namespace ai