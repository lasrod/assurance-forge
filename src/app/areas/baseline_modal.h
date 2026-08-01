#pragma once

// Modal dialog for creating a new baseline at the current package state.
//
// A baseline is a named, immutable pointer to a specific transaction
// sequence in the audit log (see `core::audit::BaselineMetadata`). This
// modal collects a name + optional description from the user and writes
// the baseline via `core::audit::CreateBaseline`.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace app::areas {

struct BaselineModalState {
    bool open = false;
    std::uint64_t at_sequence = 0; // Transaction the baseline will pin to.
    char name_buf[256] = {};
    char description_buf[1024] = {};
    std::string error_message;        // Populated on CreateBaseline failure.
    std::string canonical_model_hash; // Optional; embedded into metadata.
    bool want_focus = false;          // Set true on Open; consumed on first frame.
};

// Initialize the modal state and request that it appear next frame. Clears
// previous text input. `canonical_model_hash` is embedded into the created
// baseline metadata when provided.
void OpenBaselineModal(BaselineModalState& state, std::uint64_t at_sequence, const std::string& canonical_model_hash);

// Render the modal. Must be called every frame from a context that the
// modal popup can attach to (e.g. the same Window scope every frame). On
// successful "Create" the baseline is written immediately and the supplied
// `on_status` callback is invoked with a user-facing summary. The modal
// closes itself on success or Cancel.
void RenderBaselineModal(BaselineModalState& state,
                         const std::filesystem::path& project_root,
                         const std::string& created_by,
                         const std::function<void(const std::string&)>& on_status);

} // namespace app::areas
