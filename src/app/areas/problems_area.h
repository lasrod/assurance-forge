#pragma once

#include "core/problems/problem_item.h"

#include <functional>

namespace app {
struct AppRuntimeState;
}

namespace app::areas {

struct ProblemsAreaCallbacks {
    std::function<void(const core::ProblemItem&)> activate_problem;
    std::function<void(const core::ProblemItem&)> quick_fix_problem;
    std::function<void(const core::ProblemItem&)> open_review_problem;
};

void RenderProblemsAreaContent(AppRuntimeState& state, const ProblemsAreaCallbacks& callbacks);

} // namespace app::areas
