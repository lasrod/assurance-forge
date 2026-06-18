#include "app/areas/modal_host_internal.h"

#include "app/app_runtime_state.h"
#include "imgui.h"
#include "ui/i18n/localization.h"

#include <cctype>
#include <string>

namespace app::areas {
namespace {

// Derive a safe, upper-case pattern identifier from the pattern name: letters
// and digits are kept, every other run collapses to a single '-', and leading
// or trailing '-' are trimmed (e.g. "Hazard Decomposition!" -> "HAZARD-DECOMPOSITION").
std::string DeriveIdentifier(const std::string& name) {
    std::string out;
    bool pending_separator = false;
    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            if (pending_separator && !out.empty())
                out.push_back('-');
            pending_separator = false;
            out.push_back(static_cast<char>(std::toupper(c)));
        } else {
            pending_separator = true;
        }
    }
    return out;
}

} // namespace

void ModalHost::RenderCreatePatternModal() {
    if (!state_.pattern.show_create_pattern_modal)
        return;

    PatternUiState& pattern = state_.pattern;
    const std::string title = AF_TR("Create Pattern") + "###Create Pattern";

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(420.0f);
        if (ImGui::InputText((AF_TR("Pattern name") + "##new_pattern_name").c_str(),
                             pattern.name_buf,
                             sizeof(pattern.name_buf))) {
            // Keep the identifier in sync with the name until the user edits it
            // by hand, so the common case requires no extra typing.
            if (!pattern.identifier_user_edited)
                CopyToBuffer(pattern.identifier_buf, sizeof(pattern.identifier_buf), DeriveIdentifier(pattern.name_buf));
        }

        ImGui::SetNextItemWidth(420.0f);
        if (ImGui::InputText((AF_TR("Pattern identifier") + "##new_pattern_identifier").c_str(),
                             pattern.identifier_buf,
                             sizeof(pattern.identifier_buf))) {
            pattern.identifier_user_edited = true;
        }

        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputTextMultiline((AF_TR("Description") + "##new_pattern_description").c_str(),
                                  pattern.description_buf,
                                  sizeof(pattern.description_buf),
                                  ImVec2(420.0f, 96.0f));
        ImGui::Spacing();

        const bool can_create = !TrimWhitespace(pattern.name_buf).empty() &&
                                !TrimWhitespace(pattern.identifier_buf).empty();
        if (!can_create)
            ImGui::BeginDisabled();
        if (ImGui::Button(AF_TR("Create").c_str(), ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_add_pattern();
            if (!pattern.show_create_pattern_modal)
                ImGui::CloseCurrentPopup();
        }
        if (!can_create)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(100.0f, 0.0f))) {
            pattern.show_create_pattern_modal = false;
            pattern.pending_parent_entry.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.pattern.show_create_pattern_modal) {
        ImGui::OpenPopup(title.c_str());
    }
}

} // namespace app::areas
