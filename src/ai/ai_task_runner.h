#pragma once

#include "ai/ai_types.h"

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

private:
    friend class AiTaskRunner;
    struct SharedState {
        mutable std::mutex mutex;
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