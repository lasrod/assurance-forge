#pragma once

#include "ai/ai_types.h"

#include <cstddef>
#include <functional>

namespace ui::panels {

struct PreferencesPanelModel {
    ai::AiProviderSettings* settings = nullptr;
    bool keyStored = false;
    bool secureStoreAvailable = false;
    bool testRunning = false;
    ai::AiConnectionStatus connectionStatus;
    char* apiKeyBuffer = nullptr;
    size_t apiKeyBufferSize = 0;
    char* modelBuffer = nullptr;
    size_t modelBufferSize = 0;
};

struct PreferencesPanelCallbacks {
    std::function<void(const ai::AiProviderSettings&)> save_settings;
    std::function<void(const char*)> save_api_key;
    std::function<void()> remove_api_key;
    std::function<void()> test_connection;
};

void ShowPreferencesWindow(bool& open,
                           PreferencesPanelModel model,
                           const PreferencesPanelCallbacks& callbacks);

}  // namespace ui::panels