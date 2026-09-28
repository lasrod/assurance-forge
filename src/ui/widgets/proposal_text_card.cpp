#include "ui/widgets/proposal_text_card.h"

#include "ui/gsn/gsn_dpi.h"
#include "ui/i18n/localization.h"

#include <algorithm>
#include <cfloat>

namespace ui::widgets {
namespace {

std::string FieldDisplayLabel(const std::string& field) {
    if (field == "name")
        return AF_TR("Name");
    if (field == "content")
        return AF_TR("Content");
    if (field == "description")
        return AF_TR("Description");
    if (field.empty())
        return AF_TR("Text");
    return field;
}

ImVec2 CardPosition(ImVec2 item_min, ImVec2 item_max) {
    const float offset = ui::gsn::DpiSize(8.0f);
    const float estimated_width = ui::gsn::DpiSize(360.0f);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 work_min = viewport ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
    const ImVec2 work_max =
        viewport ? ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y)
                 : ImVec2(FLT_MAX, FLT_MAX);

    float x = item_max.x + offset;
    if (x + estimated_width > work_max.x) {
        x = item_min.x - estimated_width - offset;
    }
    x = std::max(work_min.x + offset, std::min(x, work_max.x - estimated_width - offset));

    float y = item_min.y;
    const float estimated_height = ImGui::GetTextLineHeightWithSpacing() * 8.0f;
    if (y + estimated_height > work_max.y) {
        y = std::max(work_min.y + offset, work_max.y - estimated_height);
    }
    return ImVec2(x, y);
}

} // namespace

void RenderProposalOriginalTextCard(const std::vector<ProposalTextChangePreview>& changes,
                                    ImVec2 item_min,
                                    ImVec2 item_max,
                                    const char* window_id) {
    if (changes.empty())
        return;

    ImGui::SetNextWindowPos(CardPosition(item_min, item_max), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(ui::gsn::DpiSize(280.0f), 0.0f),
                                        ImVec2(ui::gsn::DpiSize(420.0f), FLT_MAX));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin(window_id, nullptr, flags)) {
        ImGui::TextUnformatted(AF_TR("Original text").c_str());
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ui::gsn::DpiSize(380.0f));
        for (size_t index = 0; index < changes.size(); ++index) {
            if (index > 0)
                ImGui::Separator();
            ImGui::TextDisabled("%s", FieldDisplayLabel(changes[index].field).c_str());
            if (changes[index].old_value.empty()) {
                ImGui::TextDisabled("%s", AF_TR("(empty)").c_str());
            } else {
                ImGui::TextWrapped("%s", changes[index].old_value.c_str());
            }
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

} // namespace ui::widgets
