#include "ui/panels/problems_panel.h"

#include "ui/theme.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ui::panels {
namespace {

struct FilterButtonSpec {
    ui::ProblemFilter filter;
    const char* label;
};

bool IsValidationSource(core::ProblemSource source) {
    return source == core::ProblemSource::ModelValidation ||
           source == core::ProblemSource::ImportExport;
}

bool IsReviewSource(core::ProblemSource source) {
    return source == core::ProblemSource::Manual ||
           source == core::ProblemSource::GuidelineReview ||
           source == core::ProblemSource::AIReview;
}

bool MatchesFilter(const core::ProblemItem& problem, ui::ProblemFilter filter) {
    switch (filter) {
        case ui::ProblemFilter::All: return true;
        case ui::ProblemFilter::Validation: return IsValidationSource(problem.source);
        case ui::ProblemFilter::Review: return IsReviewSource(problem.source);
        case ui::ProblemFilter::Warnings: return problem.severity == core::ProblemSeverity::Warning;
        case ui::ProblemFilter::Info: return problem.severity == core::ProblemSeverity::Info;
    }
    return true;
}

int CountMatches(const std::vector<core::ProblemItem>& problems, ui::ProblemFilter filter) {
    int count = 0;
    for (const auto& problem : problems) {
        if (MatchesFilter(problem, filter)) ++count;
    }
    return count;
}

ImVec4 SeverityColor(core::ProblemSeverity severity) {
    const ui::Theme& theme = ui::GetTheme();
    switch (severity) {
        case core::ProblemSeverity::Info: return ImGui::ColorConvertU32ToFloat4(theme.accent_hover);
        case core::ProblemSeverity::Warning: return ImGui::ColorConvertU32ToFloat4(theme.warning);
        case core::ProblemSeverity::Error: return ImGui::ColorConvertU32ToFloat4(theme.danger);
    }
    return ImGui::ColorConvertU32ToFloat4(theme.text_primary);
}

void DrawHeader(bool ai_review_running, const ProblemsPanelCallbacks& callbacks) {
    ImGui::TextUnformatted("Problems");

    const char* label = ai_review_running ? "AI Review..." : "AI Review";
    const float button_width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float right_edge = ImGui::GetWindowContentRegionMax().x;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right_edge - button_width));

    if (ai_review_running) ImGui::BeginDisabled();
    if (ImGui::Button(label, ImVec2(button_width, 0.0f)) && callbacks.on_ai_review_requested) {
        callbacks.on_ai_review_requested();
    }
    if (ai_review_running) ImGui::EndDisabled();
}

void DrawFilterButton(const FilterButtonSpec& spec,
                      int count,
                      ui::ProblemFilter& active_filter) {
    const bool active = active_filter == spec.filter;
    const ui::Theme& theme = ui::GetTheme();
    std::string label = std::string(spec.label) + " (" + std::to_string(count) + ")###filter_" + spec.label;

    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::WithAlpha(theme.accent, 0.72f)));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(theme.accent_hover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(theme.accent_pressed));
    }

    if (ImGui::Button(label.c_str())) {
        active_filter = spec.filter;
    }

    if (active) {
        ImGui::PopStyleColor(3);
    }
}

void DrawFilters(const std::vector<core::ProblemItem>& problems, ui::ProblemFilter& active_filter) {
    const FilterButtonSpec specs[] = {
        { ui::ProblemFilter::All, "All" },
        { ui::ProblemFilter::Validation, "Validation" },
        { ui::ProblemFilter::Review, "Review" },
        { ui::ProblemFilter::Warnings, "Warnings" },
        { ui::ProblemFilter::Info, "Info" },
    };

    for (int index = 0; index < 5; ++index) {
        if (index > 0) ImGui::SameLine();
        DrawFilterButton(specs[index], CountMatches(problems, specs[index].filter), active_filter);
    }
}

bool ProblemExists(const std::vector<core::ProblemItem>& problems, const std::string& problem_id) {
    if (problem_id.empty()) return false;
    for (const auto& problem : problems) {
        if (problem.id == problem_id) return true;
    }
    return false;
}

void ClearRemovedSelection(const std::vector<core::ProblemItem>& problems, ui::UiState& ui_state) {
    if (ui_state.selected_problem_id.empty()) return;
    if (ProblemExists(problems, ui_state.selected_problem_id)) return;
    ui_state.selected_problem_id.clear();
    ui_state.selected_problem_element_id.clear();
}

void DrawProblemRow(const core::ProblemItem& problem,
                    ui::UiState& ui_state,
                    const ProblemsPanelCallbacks& callbacks) {
    const bool selected = ui_state.selected_problem_id == problem.id;

    ImGui::PushID(problem.id.c_str());
    ImGui::TableNextRow();
    if (selected) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ui::WithAlpha(ui::GetTheme().accent, 0.28f));
    }

    ImGui::TableSetColumnIndex(0);
    ImGui::PushStyleColor(ImGuiCol_Text, SeverityColor(problem.severity));
    ImGui::Selectable(core::ToString(problem.severity), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
    ImGui::PopStyleColor();
    if (ImGui::IsItemClicked()) {
        ui_state.selected_problem_id = problem.id;
        ui_state.selected_problem_element_id = problem.element_id;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (callbacks.on_problem_activated) callbacks.on_problem_activated(problem);
    }

    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(core::ToString(problem.source));

    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(problem.element_id.empty() ? "-" : problem.element_id.c_str());

    ImGui::TableSetColumnIndex(3);
    ImGui::TextUnformatted(problem.type.c_str());

    ImGui::TableSetColumnIndex(4);
    ImGui::TextUnformatted(problem.message.c_str());
    if (ImGui::IsItemHovered() && !problem.message.empty()) {
        ImGui::SetTooltip("%s", problem.message.c_str());
    }

    ImGui::TableSetColumnIndex(5);
    ImGui::TextUnformatted(problem.guideline_id.empty() ? "-" : problem.guideline_id.c_str());

    ImGui::PopID();
}

}  // namespace

void ShowProblemsPanel(float x,
                       float width,
                       float height,
                       float top_y,
                       ImGuiWindowFlags panel_flags,
                       ProblemsPanelModel model,
                       const ProblemsPanelCallbacks& callbacks) {
    ImGui::SetNextWindowPos(ImVec2(x, top_y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("Problems", nullptr, panel_flags);

    const std::vector<core::ProblemItem>& problems = model.problems_manager.GetProblems();
    ClearRemovedSelection(problems, model.ui_state);

    DrawHeader(model.ai_review_running, callbacks);
    ImGui::Separator();
    DrawFilters(problems, model.ui_state.active_problem_filter);
    ImGui::Separator();

    if (problems.empty()) {
        ImGui::TextDisabled("No problems found.");
        ImGui::End();
        return;
    }

    int visible_count = CountMatches(problems, model.ui_state.active_problem_filter);
    if (visible_count == 0) {
        ImGui::TextDisabled("No problems match the current filter.");
        ImGui::End();
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_Borders
                          | ImGuiTableFlags_RowBg
                          | ImGuiTableFlags_Resizable
                          | ImGuiTableFlags_ScrollY
                          | ImGuiTableFlags_SizingStretchProp;

    if (ImGui::BeginTable("problems_table", 6, flags, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Severity", ImGuiTableColumnFlags_WidthFixed, 86.0f);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 128.0f);
        ImGui::TableSetupColumn("Element", ImGuiTableColumnFlags_WidthFixed, 88.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 96.0f);
        ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Guideline", ImGuiTableColumnFlags_WidthFixed, 116.0f);
        ImGui::TableHeadersRow();

        for (const auto& problem : problems) {
            if (!MatchesFilter(problem, model.ui_state.active_problem_filter)) continue;
            DrawProblemRow(problem, model.ui_state, callbacks);
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

}  // namespace ui::panels
