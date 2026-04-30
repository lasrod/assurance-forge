#include "ui/panels/review_panel.h"

#include "imgui.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstring>

namespace ui::panels {
namespace {

void CopyToBuffer(char* buffer, size_t size, const std::string& value) {
    if (size == 0) return;
    const size_t count = std::min(size - 1, value.size());
    std::memcpy(buffer, value.data(), count);
    buffer[count] = '\0';
}

void DrawStatusBadge(const core::ReviewItem& item) {
    const bool resolved = item.status == core::ReviewItemStatus::Resolved;
    ImU32 color = resolved ? ui::GetTheme().success : ui::GetTheme().warning;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color));
    ImGui::TextUnformatted(resolved ? "Resolved" : "Open");
    ImGui::PopStyleColor();
}

void DrawProposalActions(const core::ReviewItem& item,
                         const ReviewPanelModel& model,
                         const ReviewPanelCallbacks& callbacks) {
    const bool is_active_draft = model.active_proposal_review_item_id == item.id;
    if (is_active_draft) {
        ImGui::Text("Proposal draft: %d operation(s)", static_cast<int>(model.active_proposal_operation_count));
        if (!model.active_proposal_can_save) ImGui::BeginDisabled();
        if (ImGui::Button("Save Proposal") && callbacks.save_proposal) {
            callbacks.save_proposal(item);
        }
        if (!model.active_proposal_can_save) ImGui::EndDisabled();
        return;
    }

    if (!item.proposal_id.has_value()) {
        if (item.status != core::ReviewItemStatus::Open) {
            ImGui::TextDisabled("No proposal for resolved comment.");
            return;
        }
        if (ImGui::Button("Create Proposed Change") && callbacks.create_proposed_change) {
            callbacks.create_proposed_change(item);
        }
        return;
    }

    core::ProposalValidityResult validity;
    auto found = model.proposal_validity.find(item.proposal_id.value());
    if (found != model.proposal_validity.end()) {
        validity = found->second;
    }

    const bool is_valid = validity.validity == core::ProposalValidity::Valid;
    ImU32 proposal_color = is_valid ? ui::GetTheme().success : ui::GetTheme().danger;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(proposal_color));
    ImGui::TextUnformatted(is_valid ? "Proposed change: Valid" : "Proposed change: Broken");
    ImGui::PopStyleColor();
    if (!is_valid && !validity.reason.empty()) {
        ImGui::TextWrapped("Reason: %s", validity.reason.c_str());
    }

    if (is_valid) {
        if (ImGui::Button("View Proposal") && callbacks.preview_proposal) callbacks.preview_proposal(item);
        ImGui::SameLine();
        if (ImGui::Button("Apply Proposal") && callbacks.apply_proposal) callbacks.apply_proposal(item);
        ImGui::SameLine();
    }
    if (ImGui::Button("Delete Proposal") && callbacks.delete_proposal) callbacks.delete_proposal(item);
}

}  // namespace

void ShowReviewPanel(const ReviewPanelModel& model, const ReviewPanelCallbacks& callbacks) {
    ImGui::TextUnformatted("Review");
    ImGui::Separator();

    if (!model.has_project) {
        ImGui::TextDisabled("Open or create a project to store review comments.");
        return;
    }

    if (model.selected_element_id.empty()) {
        ImGui::TextDisabled("Select an element to review.");
        return;
    }

    ImGui::TextDisabled("Element %s", model.selected_element_id.c_str());
    ImGui::Spacing();

    static std::string active_element_id;
    static char title_buf[160] = "";
    static char message_buf[1024] = "";
    if (active_element_id != model.selected_element_id) {
        active_element_id = model.selected_element_id;
        title_buf[0] = '\0';
        message_buf[0] = '\0';
    }

    if (title_buf[0] == '\0') CopyToBuffer(title_buf, sizeof(title_buf), "Review comment");

    ImGui::TextUnformatted("New Comment");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##review_title", title_buf, sizeof(title_buf));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextMultiline("##review_message", message_buf, sizeof(message_buf),
                              ImVec2(-1.0f, ImGui::GetTextLineHeight() * 4.0f));

    const bool can_add = title_buf[0] != '\0' && message_buf[0] != '\0';
    if (!can_add) ImGui::BeginDisabled();
    if (ImGui::Button("Add Review Comment") && callbacks.add_review_item) {
        callbacks.add_review_item(title_buf, message_buf);
        CopyToBuffer(title_buf, sizeof(title_buf), "Review comment");
        message_buf[0] = '\0';
    }
    if (!can_add) ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::Text("Comments (%d)", static_cast<int>(model.review_items.size()));

    if (model.review_items.empty()) {
        ImGui::TextDisabled("No review comments for this element.");
        return;
    }

    if (ImGui::BeginChild("##review_items", ImVec2(0.0f, 0.0f), false)) {
        for (const core::ReviewItem& item : model.review_items) {
            ImGui::PushID(item.id.c_str());
            ImGui::Separator();
            DrawStatusBadge(item);
            ImGui::SameLine();
            ImGui::TextWrapped("%s", item.title.empty() ? "Review comment" : item.title.c_str());
            if (!item.message.empty()) {
                ImGui::TextWrapped("%s", item.message.c_str());
            }
            DrawProposalActions(item, model, callbacks);
            if (ImGui::Button("Delete") && callbacks.delete_review_item) {
                callbacks.delete_review_item(item);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

}  // namespace ui::panels