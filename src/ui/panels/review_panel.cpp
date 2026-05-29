#include "ui/panels/review_panel.h"

#include "core/string_utils.h"
#include "imgui.h"
#include "ui/i18n/localization.h"
#include "ui/imgui_buffer_utils.h"
#include "ui/theme.h"

#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>

namespace ui::panels {
namespace {

using ui::CopyToBuffer;

void DrawStatusBadge(const core::reviews::ReviewItem& item) {
    const bool resolved = item.status == core::reviews::ReviewItemStatus::Resolved;
    ImU32 color = resolved ? ui::GetTheme().success : ui::GetTheme().warning;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color));
    ImGui::TextUnformatted((resolved ? AF_TR("Resolved") : AF_TR("Open")).c_str());
    ImGui::PopStyleColor();
}

void DrawProblemSeverityBadge(const core::ProblemItem& problem) {
    ImU32 color = ui::GetTheme().text_secondary;
    if (problem.severity == core::ProblemSeverity::Error)
        color = ui::GetTheme().danger;
    else if (problem.severity == core::ProblemSeverity::Warning)
        color = ui::GetTheme().warning;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color));
    ImGui::TextUnformatted(core::ToString(problem.severity));
    ImGui::PopStyleColor();
}

bool ContainsGuidelineId(const std::vector<std::string>& guideline_ids, const std::string& guideline_id) {
    return std::find(guideline_ids.begin(), guideline_ids.end(), guideline_id) != guideline_ids.end();
}

const ReviewGuidelineOption* FindGuidelineOption(const ReviewPanelModel& model, const std::string& guideline_id) {
    auto found = std::find_if(model.guideline_options.begin(),
                              model.guideline_options.end(),
                              [&](const ReviewGuidelineOption& option) { return option.id == guideline_id; });
    return found == model.guideline_options.end() ? nullptr : &*found;
}

bool MatchesGuidelineFilter(const ReviewGuidelineOption& option, const std::string& lowered_filter) {
    if (lowered_filter.empty())
        return true;
    return core::ToLower(option.id).find(lowered_filter) != std::string::npos ||
           core::ToLower(option.category).find(lowered_filter) != std::string::npos ||
           core::ToLower(option.title).find(lowered_filter) != std::string::npos;
}

std::string GuidelineDisplayLabel(const ReviewGuidelineOption& option) {
    if (option.title.empty())
        return option.id;
    return option.id + " - " + option.title;
}

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

ImVec2 ProposalHoverCardPosition(ImVec2 item_min, ImVec2 item_max) {
    constexpr float kOffset = 8.0f;
    constexpr float kEstimatedWidth = 360.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 work_min = viewport ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
    const ImVec2 work_max =
        viewport ? ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y)
                 : ImVec2(FLT_MAX, FLT_MAX);

    float x = item_max.x + kOffset;
    if (x + kEstimatedWidth > work_max.x) {
        x = item_min.x - kEstimatedWidth - kOffset;
    }
    x = std::max(work_min.x + kOffset, std::min(x, work_max.x - kEstimatedWidth - kOffset));

    float y = item_min.y;
    const float line_height = ImGui::GetTextLineHeightWithSpacing();
    if (y + line_height * 8.0f > work_max.y) {
        y = std::max(work_min.y + kOffset, work_max.y - line_height * 8.0f);
    }
    return ImVec2(x, y);
}

void RenderProposalOriginalTextHoverCard(const std::vector<ProposalTextChangePreview>& changes,
                                         ImVec2 item_min,
                                         ImVec2 item_max) {
    if (changes.empty())
        return;

    ImGui::SetNextWindowPos(ProposalHoverCardPosition(item_min, item_max), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(280.0f, 0.0f), ImVec2(420.0f, FLT_MAX));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("Original Text##proposal_original_text_hover", nullptr, flags)) {
        ImGui::TextUnformatted(AF_TR("Original text").c_str());
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f);
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

const std::vector<ProposalTextChangePreview>* FindProposalTextChanges(const ReviewPanelModel& model,
                                                                      const std::string& proposal_id) {
    auto found = model.proposal_text_changes.find(proposal_id);
    return found == model.proposal_text_changes.end() ? nullptr : &found->second;
}

void OpenGuidelineStub(const std::string& guideline_id, std::string& popup_guideline_id) {
    popup_guideline_id = guideline_id;
    ImGui::OpenPopup((AF_TR("SCCG Guideline") + "###SCCG Guideline").c_str());
}

void DrawGuidelineStubPopup(const ReviewPanelModel& model, std::string& popup_guideline_id) {
    if (!ImGui::BeginPopupModal((AF_TR("SCCG Guideline") + "###SCCG Guideline").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    if (popup_guideline_id == "__browse__") {
        ImGui::TextUnformatted(AF_TR("Browse SCCG Guidelines").c_str());
    } else if (!popup_guideline_id.empty()) {
        const ReviewGuidelineOption* option = FindGuidelineOption(model, popup_guideline_id);
        ImGui::TextUnformatted(popup_guideline_id.c_str());
        if (option && !option->title.empty()) {
            ImGui::TextWrapped("%s", option->title.c_str());
        }
    }
    ImGui::Spacing();
    ImGui::TextUnformatted(AF_TR("Not Implemented").c_str());
    ImGui::Spacing();
    if (ImGui::Button(AF_TR("Close").c_str())) {
        popup_guideline_id.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void DrawGuidelineTags(const ReviewPanelModel& model,
                       const std::vector<std::string>& guideline_ids,
                       std::string& popup_guideline_id) {
    if (guideline_ids.empty())
        return;

    ImGui::TextDisabled("%s", AF_TR("SCCG").c_str());
    ImGui::SameLine();
    for (size_t index = 0; index < guideline_ids.size(); ++index) {
        if (index > 0)
            ImGui::SameLine();
        const std::string& guideline_id = guideline_ids[index];
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::SmallButton(guideline_id.c_str())) {
            OpenGuidelineStub(guideline_id, popup_guideline_id);
        }
        if (ImGui::IsItemHovered()) {
            const ReviewGuidelineOption* option = FindGuidelineOption(model, guideline_id);
            ImGui::SetTooltip("%s", option ? GuidelineDisplayLabel(*option).c_str() : guideline_id.c_str());
        }
        ImGui::PopID();
    }
}

void DrawSelectedGuidelineTags(std::vector<std::string>& selected_guideline_ids) {
    if (selected_guideline_ids.empty()) {
        ImGui::TextDisabled("%s", AF_TR("No SCCG guideline violations selected.").c_str());
        return;
    }

    ImGui::TextDisabled("%s", AF_TR("Selected").c_str());
    ImGui::SameLine();
    for (size_t index = 0; index < selected_guideline_ids.size();) {
        if (index > 0)
            ImGui::SameLine();
        ImGui::PushID(static_cast<int>(index));
        std::string label = selected_guideline_ids[index] + " x";
        if (ImGui::SmallButton(label.c_str())) {
            selected_guideline_ids.erase(selected_guideline_ids.begin() + static_cast<std::ptrdiff_t>(index));
            ImGui::PopID();
            continue;
        }
        ImGui::PopID();
        ++index;
    }
}

void DrawGuidelineSelector(const ReviewPanelModel& model,
                           std::vector<std::string>& selected_guideline_ids,
                           char* filter_buffer,
                           size_t filter_buffer_size,
                           std::string& popup_guideline_id) {
    ImGui::TextUnformatted(AF_TR("SCCG Guideline Violations").c_str());
    ImGui::SameLine();
    if (ImGui::Button(AF_TR("Browse SCCG Guidelines").c_str())) {
        OpenGuidelineStub("__browse__", popup_guideline_id);
    }

    DrawSelectedGuidelineTags(selected_guideline_ids);

    const bool has_options = !model.guideline_options.empty();
    if (!has_options)
        ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##guideline_filter", AF_TR("Filter SCCG IDs or titles").c_str(), filter_buffer,
                             filter_buffer_size);

    const float list_height = ImGui::GetTextLineHeightWithSpacing() * 6.0f;
    if (ImGui::BeginChild("##guideline_options", ImVec2(0.0f, list_height), true)) {
        if (!has_options) {
            ImGui::TextDisabled("%s",
                                model.guideline_status.empty() ? AF_TR("SCCG guidelines are not available.").c_str()
                                                                : model.guideline_status.c_str());
        } else {
            const std::string filter(filter_buffer);
            const std::string lowered_filter = core::ToLower(filter);
            int shown = 0;
            for (const ReviewGuidelineOption& option : model.guideline_options) {
                if (ContainsGuidelineId(selected_guideline_ids, option.id))
                    continue;
                if (!MatchesGuidelineFilter(option, lowered_filter))
                    continue;

                const std::string label = GuidelineDisplayLabel(option);
                if (ImGui::Selectable(label.c_str())) {
                    selected_guideline_ids.push_back(option.id);
                    filter_buffer[0] = '\0';
                }
                ++shown;
                if (shown >= 80) {
                    ImGui::TextDisabled("%s", AF_TR("Keep filtering to narrow the remaining guidelines.").c_str());
                    break;
                }
            }
            if (shown == 0) {
                ImGui::TextDisabled("%s", AF_TR("No matching SCCG guideline IDs.").c_str());
            }
        }
    }
    ImGui::EndChild();
    if (!has_options)
        ImGui::EndDisabled();
}

void DrawProposalActions(const core::reviews::ReviewItem& item,
                         const ReviewPanelModel& model,
                         const ReviewPanelCallbacks& callbacks) {
    const bool is_active_draft = model.active_proposal_review_item_id == item.id;
    if (is_active_draft) {
        ImGui::Text(AF_TR("Proposal draft: %d operation(s)").c_str(),
                    static_cast<int>(model.active_proposal_operation_count));
        if (!model.active_proposal_can_save)
            ImGui::BeginDisabled();
        if (ImGui::Button(AF_TR("Save Proposal").c_str()) && callbacks.save_proposal) {
            callbacks.save_proposal(item);
        }
        if (!model.active_proposal_can_save)
            ImGui::EndDisabled();
        return;
    }

    if (!item.proposal_id.has_value()) {
        if (item.status != core::reviews::ReviewItemStatus::Open) {
            ImGui::TextDisabled("%s", AF_TR("No proposal for resolved comment.").c_str());
            return;
        }
        if (ImGui::Button(AF_TR("Create Proposed Change").c_str()) && callbacks.create_proposed_change) {
            callbacks.create_proposed_change(item);
        }
        return;
    }

    core::reviews::ProposalValidityResult validity;
    auto validity_it = model.proposal_validity.find(item.proposal_id.value());
    if (validity_it != model.proposal_validity.end()) {
        validity = validity_it->second;
    }

    const std::vector<ProposalTextChangePreview>* text_changes =
        FindProposalTextChanges(model, item.proposal_id.value());
    ImGui::BeginGroup();

    const bool is_valid = validity.validity == core::reviews::ProposalValidity::Valid;
    ImU32 proposal_color = is_valid ? ui::GetTheme().success : ui::GetTheme().danger;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(proposal_color));
    ImGui::TextUnformatted((is_valid ? AF_TR("Proposed change: Valid") : AF_TR("Proposed change: Broken")).c_str());
    ImGui::PopStyleColor();
    if (!is_valid && !validity.reason.empty()) {
        ImGui::TextWrapped(AF_TR("Reason: %s").c_str(), validity.reason.c_str());
    }

    if (item.status == core::reviews::ReviewItemStatus::Open) {
        if (ImGui::Button(AF_TR("Edit Proposal").c_str()) && callbacks.edit_proposal)
            callbacks.edit_proposal(item);
        ImGui::SameLine();
    }
    if (is_valid) {
        if (ImGui::Button(AF_TR("View Proposal").c_str()) && callbacks.preview_proposal)
            callbacks.preview_proposal(item);
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Apply Proposal").c_str()) && callbacks.apply_proposal)
            callbacks.apply_proposal(item);
        ImGui::SameLine();
    }
    if (ImGui::Button(AF_TR("Delete Proposal").c_str()) && callbacks.delete_proposal)
        callbacks.delete_proposal(item);

    ImGui::EndGroup();
    if (text_changes && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
        RenderProposalOriginalTextHoverCard(*text_changes, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    }
}

void DrawReviewItemActions(const core::reviews::ReviewItem& item,
                           const ReviewPanelModel& model,
                           const ReviewPanelCallbacks& callbacks) {
    DrawProposalActions(item, model, callbacks);
    if (item.status == core::reviews::ReviewItemStatus::Open) {
        if (ImGui::Button(AF_TR("Resolve").c_str()) && callbacks.resolve_review_item) {
            callbacks.resolve_review_item(item);
        }
        ImGui::SameLine();
    }
    if (ImGui::Button(AF_TR("Delete").c_str()) && callbacks.delete_review_item) {
        callbacks.delete_review_item(item);
    }
}

void DrawSelectableMessageText(const char* id_suffix, const std::string& message, float line_count) {
    if (message.empty())
        return;

    std::vector<char> buffer(message.begin(), message.end());
    buffer.push_back('\0');

    const float min_lines = line_count < 2.0f ? 2.0f : line_count;
    ImGui::PushID(id_suffix);
    ImGui::InputTextMultiline("##message",
                              buffer.data(),
                              buffer.size(),
                              ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * min_lines),
                              ImGuiInputTextFlags_ReadOnly);
    ImGui::PopID();
}

void DrawProblemItem(const core::ProblemItem& problem, const ReviewPanelCallbacks& callbacks) {
    DrawProblemSeverityBadge(problem);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", problem.type.empty() ? AF_TR("Problem").c_str() : problem.type.c_str());
    ImGui::TextDisabled(AF_TR("Source: %s").c_str(), core::ToString(problem.source));
    // Translate defensively: validation messages from core/ are English msgids,
    // user-typed text falls back to itself.
    DrawSelectableMessageText("problem_message", AF_TR(problem.message), 3.5f);
    if (!problem.quick_fix_label.empty() && callbacks.quick_fix_problem) {
        // quick_fix_label is set as an English msgid in app/; translate at render.
        if (ImGui::Button(AF_TR(problem.quick_fix_label).c_str())) {
            callbacks.quick_fix_problem(problem);
        }
        ImGui::SameLine();
    }
    if (ImGui::Button((AF_TR("Resolve") + "##problem_resolve").c_str()) && callbacks.delete_problem) {
        callbacks.delete_problem(problem);
    }
    ImGui::SameLine();
    if (ImGui::Button((AF_TR("Delete") + "##problem_delete").c_str()) && callbacks.delete_problem) {
        callbacks.delete_problem(problem);
    }
}

} // namespace

void ShowReviewPanel(const ReviewPanelModel& model, const ReviewPanelCallbacks& callbacks) {
    ImGui::TextUnformatted(AF_TR("Review").c_str());
    ImGui::Separator();

    if (!model.has_project) {
        ImGui::TextDisabled("%s", AF_TR("Open or create a project to store review comments.").c_str());
        return;
    }

    if (model.selected_element_id.empty()) {
        ImGui::TextDisabled("%s", AF_TR("Select an element to review.").c_str());
        return;
    }

    ImGui::TextDisabled(AF_TR("Element %s").c_str(), model.selected_element_id.c_str());
    const std::string review_status =
        model.review_status_text.empty() ? AF_TR("Not reviewed") : model.review_status_text;
    ImGui::Text(AF_TR("Review status: %s").c_str(), review_status.c_str());
    if (!model.review_status_detail.empty()) {
        ImGui::TextDisabled("%s", model.review_status_detail.c_str());
    }
    bool manual_ok = model.manual_review_ok;
    if (ImGui::Checkbox(AF_TR("Manual review OK").c_str(), &manual_ok) && callbacks.set_manual_review_ok) {
        callbacks.set_manual_review_ok(manual_ok);
    }
    bool ai_ok = model.ai_review_ok;
    ImGui::BeginDisabled();
    ImGui::Checkbox(AF_TR("AI review OK").c_str(), &ai_ok);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", AF_TR("AI review OK is set by AI review outcomes.").c_str());
    }
    ImGui::Spacing();

    static std::string active_element_id;
    static char title_buf[160] = "";
    static char message_buf[1024] = "";
    static char guideline_filter_buf[160] = "";
    static std::vector<std::string> selected_guideline_ids;
    static std::string popup_guideline_id;
    static bool show_guideline_selector = false;
    if (active_element_id != model.selected_element_id) {
        active_element_id = model.selected_element_id;
        title_buf[0] = '\0';
        message_buf[0] = '\0';
        guideline_filter_buf[0] = '\0';
        selected_guideline_ids.clear();
        show_guideline_selector = false;
    }

    ImGui::Separator();
    ImGui::Text(AF_TR("Comments (%d)").c_str(), static_cast<int>(model.review_items.size()));

    if (model.review_items.empty() && model.problem_items.empty()) {
        ImGui::TextDisabled("%s", AF_TR("No review comments for this element.").c_str());
    }

    for (const core::reviews::ReviewItem& item : model.review_items) {
        const bool focused_item = !model.focus_review_item_id.empty() && model.focus_review_item_id == item.id;
        ImGui::PushID(item.id.c_str());
        ImGui::Separator();
        DrawStatusBadge(item);
        ImGui::SameLine();
        if (focused_item) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::GetTheme().accent));
        }
        ImGui::TextWrapped("%s", item.title.empty() ? AF_TR("Review comment").c_str() : item.title.c_str());
        if (focused_item) {
            ImGui::PopStyleColor();
        }
        const std::string reviewer = item.reviewer_name.empty() ? AF_TR("not recorded") : item.reviewer_name;
        ImGui::TextDisabled(AF_TR("Reviewed by %s").c_str(), reviewer.c_str());
        DrawSelectableMessageText("review_message", item.message, 4.0f);
        DrawGuidelineTags(model, item.guideline_ids, popup_guideline_id);
        DrawReviewItemActions(item, model, callbacks);
        if (focused_item) {
            ImGui::SetScrollHereY(0.2f);
        }
        ImGui::PopID();
    }

    if (!model.problem_items.empty()) {
        ImGui::Separator();
        ImGui::Text(AF_TR("Other Problems (%d)").c_str(), static_cast<int>(model.problem_items.size()));
        for (const core::ProblemItem& problem : model.problem_items) {
            ImGui::PushID(problem.id.c_str());
            ImGui::Separator();
            DrawProblemItem(problem, callbacks);
            ImGui::PopID();
        }
    }

    if (title_buf[0] == '\0')
        CopyToBuffer(title_buf, sizeof(title_buf), AF_TR("Review comment"));

    ImGui::Separator();
    ImGui::TextUnformatted(AF_TR("New Comment").c_str());
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##review_title", title_buf, sizeof(title_buf));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextMultiline(
        "##review_message", message_buf, sizeof(message_buf), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 4.0f));

    if (ImGui::Button(AF_TR("Add SCCG violation").c_str())) {
        show_guideline_selector = !show_guideline_selector;
    }

    if (show_guideline_selector) {
        DrawGuidelineSelector(
            model, selected_guideline_ids, guideline_filter_buf, sizeof(guideline_filter_buf), popup_guideline_id);
    }

    const bool can_add = title_buf[0] != '\0' && message_buf[0] != '\0';
    if (!can_add)
        ImGui::BeginDisabled();
    if (ImGui::Button(AF_TR("Add Review Comment").c_str()) && callbacks.add_review_item) {
        callbacks.add_review_item(title_buf, message_buf, selected_guideline_ids);
        CopyToBuffer(title_buf, sizeof(title_buf), AF_TR("Review comment"));
        message_buf[0] = '\0';
        guideline_filter_buf[0] = '\0';
        selected_guideline_ids.clear();
        show_guideline_selector = false;
    }
    if (!can_add)
        ImGui::EndDisabled();

    DrawGuidelineStubPopup(model, popup_guideline_id);
}

} // namespace ui::panels
