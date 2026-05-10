#pragma once

#include <cstddef>
#include <functional>

namespace app {
struct AppRuntimeState;
}

namespace app::areas {

struct TermUsagesAreaCallbacks {
    std::function<void(std::size_t)> activate_usage;
};

void RenderTermUsagesAreaContent(AppRuntimeState& state, const TermUsagesAreaCallbacks& callbacks);

} // namespace app::areas
