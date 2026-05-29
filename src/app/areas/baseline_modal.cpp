#include "app/areas/baseline_modal.h"

#include "core/audit/audit_baseline.h"
#include "core/string_utils.h"
#include "ui/i18n/localization.h"

#include "imgui.h"

#include <cstring>

namespace app::areas {

namespace {

constexpr const char* kPopupId = "Create baseline##af_baseline_modal";

} // namespace

void OpenBaselineModal(BaselineModalState& state,
                       std::uint64_t at_sequence,
                       const std::string& canonical_model_hash) {
    state.open = true;
    state.at_sequence = at_sequence;
    std::memset(state.name_buf, 0, sizeof(state.name_buf));
    std::memset(state.description_buf, 0, sizeof(state.description_buf));
    state.error_message.clear();
    state.canonical_model_hash = canonical_model_hash;
    state.want_focus = true;
}

void RenderBaselineModal(BaselineModalState& state,
                         const std::filesystem::path& project_root,
                         const std::string& created_by,
                         const std::function<void(const std::string&)>& on_status) {
    if (!state.open) return;

    // Drive ImGui's modal popup lifecycle from the `open` flag.
    if (!ImGui::IsPopupOpen(kPopupId))
        ImGui::OpenPopup(kPopupId);

    ImVec2 viewport_center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(viewport_center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
    // Lock width to prevent the AlwaysAutoResize + GetContentRegionAvail()
    // feedback loop that grows the popup horizontally every frame.
    ImGui::SetNextWindowSizeConstraints(ImVec2(480.0f, 0.0f), ImVec2(480.0f, FLT_MAX));

    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextWrapped(AF_TR("Pin a named baseline to transaction sequence %llu.").c_str(),
                       static_cast<unsigned long long>(state.at_sequence));
    ImGui::Spacing();

    ImGui::TextUnformatted(AF_TR("Name").c_str());
    if (state.want_focus) {
        ImGui::SetKeyboardFocusHere();
        state.want_focus = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##baseline_name", state.name_buf, sizeof(state.name_buf));

    ImGui::Spacing();
    ImGui::TextUnformatted(AF_TR("Description (optional)").c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextMultiline("##baseline_description",
                              state.description_buf, sizeof(state.description_buf),
                              ImVec2(0.0f, ImGui::GetTextLineHeight() * 4.0f));

    if (!state.error_message.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
        ImGui::TextWrapped("%s", state.error_message.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();

    const float button_width = 110.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float used = button_width * 2.0f + spacing;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > used) ImGui::Dummy(ImVec2(avail - used, 0.0f));
    ImGui::SameLine();

    bool close_requested = false;
    if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(button_width, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        close_requested = true;
    }
    ImGui::SameLine();

    const std::string trimmed_name = core::TrimWhitespace(state.name_buf);
    const bool can_create = !trimmed_name.empty();
    if (!can_create) ImGui::BeginDisabled();
    if (ImGui::Button(AF_TR("Create").c_str(), ImVec2(button_width, 0.0f))) {
        core::audit::CreateBaselineRequest req;
        req.name = trimmed_name;
        req.description = core::TrimWhitespace(state.description_buf);
        req.created_by = created_by.empty() ? std::string("unknown") : created_by;
        req.at_transaction_sequence = state.at_sequence;
        req.canonical_model_hash = state.canonical_model_hash;

        core::audit::BaselineMetadata created;
        std::string err;
        if (core::audit::CreateBaseline(project_root, req, created, err)) {
            if (on_status) {
                on_status(ui::i18n::trf("Baseline \"{0}\" created at sequence {1}.",
                                        created.name,
                                        created.transaction_sequence));
            }
            close_requested = true;
        } else {
            state.error_message = err.empty() ? AF_TR("Failed to create baseline.") : err;
        }
    }
    if (!can_create) ImGui::EndDisabled();

    if (close_requested) {
        state.open = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

} // namespace app::areas
