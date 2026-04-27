#pragma once

// Shows the welcome/startup modal dialog.
// Call this once per frame after ImGui::NewFrame() if the modal should be displayed.
void ShowWelcomeModal(bool& is_open);
