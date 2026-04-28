#include "ai/ai_task_runner.h"

#include <exception>
#include <thread>
#include <utility>

namespace ai {

AiTaskHandle::AiTaskHandle(std::shared_ptr<SharedState> state) : state_(std::move(state)) {}

AiTaskSnapshot AiTaskHandle::Snapshot() const {
    if (!state_) return {};
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->snapshot;
}

bool AiTaskHandle::IsRunning() const {
    return Snapshot().state == AiTaskState::Running;
}

std::shared_ptr<AiTaskHandle> AiTaskRunner::RunConnectionTest(std::function<AiConnectionStatus()> job) {
    auto state = std::make_shared<AiTaskHandle::SharedState>();
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->snapshot.state = AiTaskState::Running;
        state->snapshot.status = MakeStatus(AiTaskState::Running, AiErrorCode::None, "Testing connection...");
    }

    auto handle = std::shared_ptr<AiTaskHandle>(new AiTaskHandle(state));
    std::thread([state, job = std::move(job)]() mutable {
        AiConnectionStatus status;
        try {
            status = job ? job() : ErrorStatus(AiErrorCode::Unknown, "AI task was not configured.");
        } catch (const std::exception& exception) {
            status = ErrorStatus(AiErrorCode::Unknown, exception.what());
        } catch (...) {
            status = ErrorStatus(AiErrorCode::Unknown, "AI task failed.");
        }

        std::lock_guard<std::mutex> lock(state->mutex);
        state->snapshot.state = status.state == AiTaskState::Success ? AiTaskState::Success : AiTaskState::Error;
        state->snapshot.status = std::move(status);
    }).detach();

    return handle;
}

}  // namespace ai
