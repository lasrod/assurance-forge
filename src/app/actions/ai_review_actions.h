#pragma once

#include <string>

namespace app {
struct AppRuntimeState;
}

namespace app::actions {

class AiReviewActions {
public:
    explicit AiReviewActions(AppRuntimeState& state);

    void BeginForSelection();
    void BeginForSelection(const std::string& review_profile_id);
    void RunForSelection();
    void RunForSelection(const std::string& review_profile_id);
    void StartPendingRequest();
    void PollTask();

private:
    AppRuntimeState& state_;
};

} // namespace app::actions
