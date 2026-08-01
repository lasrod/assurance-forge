#include "ui/panels/problems_panel.h"

#include "ui/i18n/localization.h"
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
    return source == core::ProblemSource::ModelValidation || source == core::ProblemSource::ImportExport;
}

bool IsReviewSource(core::ProblemSource source) {
    return source == core::ProblemSource::Manual || source == core::ProblemSource::ReviewComment ||
           source == core::ProblemSource::GuidelineReview || source == core::ProblemSource::AIReview;
}

bool MatchesFilter(const core::ProblemItem& problem, ui::ProblemFilter filter) {
    switch (filter) {
    case ui::ProblemFilter::All:
        return true;
    case ui::ProblemFilter::Validation:
        return IsValidationSource(problem.source);
    case ui::ProblemFilter::Review:
        return IsReviewSource(problem.source);
    case ui::ProblemFilter::Warnings:
        return problem.severity == core::ProblemSeverity::Warning;
    case ui::ProblemFilter::Info:
        return problem.severity == core::ProblemSeverity::Info;
    }
    return true;
}

std::string SingleLineMessage(const std::string& message) {
    std::string flattened;
    flattened.reserve(message.size());
    bool last_was_space = false;
    for (char ch : message) {
        const bool is_space = ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ';
        if (is_space) {
            if (!flattened.empty() && !last_was_space) {
                flattened.push_back(' ');
                last_was_space = true;
            }
            continue;
        }
        flattened.push_back(ch);
        last_was_space = false;
    }
    if (!flattened.empty() && flattened.back() == ' ')
        flattened.pop_back();
    return flattened;
}

int CountMatches(const std::vector<core::ProblemItem>& problems, ui::ProblemFilter filter) {
    int count = 0;
    for (const auto& problem : problems) {
        if (MatchesFilter(problem, filter))
            ++count;
    }
    return count;
}

ImVec4 SeverityColor(core::ProblemSeverity severity) {
    const ui::Theme& theme = ui::GetTheme();
    switch (severity) {
    case core::ProblemSeverity::Info:
        return ImGui::ColorConvertU32ToFloat4(theme.accent_hover);
    case core::ProblemSeverity::Warning:
        return ImGui::ColorConvertU32ToFloat4(theme.warning);
    case core::ProblemSeverity::Error:
        return ImGui::ColorConvertU32ToFloat4(theme.danger);
    }
    return ImGui::ColorConvertU32ToFloat4(theme.text_primary);
}

void DrawHeader(bool show_title) {
    if (show_title) {
        ImGui::TextUnformatted(AF_TR("Problems").c_str());
        ImGui::Separator();
    }
}

// Translate filter labels through literal AF_TR calls so tools/i18n/extract_msgids.py
// can discover them. spec.label remains the stable English ID suffix.
std::string TranslateFilterLabel(ui::ProblemFilter filter, const char* fallback) {
    switch (filter) {
    case ui::ProblemFilter::All:
        return AF_TR("All");
    case ui::ProblemFilter::Validation:
        return AF_TR("Validation");
    case ui::ProblemFilter::Review:
        return AF_TR("Review");
    case ui::ProblemFilter::Warnings:
        return AF_TR("Warnings");
    case ui::ProblemFilter::Info:
        return AF_TR("Info");
    }
    return fallback;
}

void DrawFilterButton(const FilterButtonSpec& spec, int count, ui::ProblemFilter& active_filter) {
    const bool active = active_filter == spec.filter;
    const ui::Theme& theme = ui::GetTheme();
    std::string label =
        TranslateFilterLabel(spec.filter, spec.label) + " (" + std::to_string(count) + ")###filter_" + spec.label;

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
        {ui::ProblemFilter::All, "All"},
        {ui::ProblemFilter::Validation, "Validation"},
        {ui::ProblemFilter::Review, "Review"},
        {ui::ProblemFilter::Warnings, "Warnings"},
        {ui::ProblemFilter::Info, "Info"},
    };

    for (int index = 0; index < 5; ++index) {
        if (index > 0)
            ImGui::SameLine();
        DrawFilterButton(specs[index], CountMatches(problems, specs[index].filter), active_filter);
    }
}

bool ProblemExists(const std::vector<core::ProblemItem>& problems, const std::string& problem_id) {
    if (problem_id.empty())
        return false;
    for (const auto& problem : problems) {
        if (problem.id == problem_id)
            return true;
    }
    return false;
}

void ClearRemovedSelection(const std::vector<core::ProblemItem>& problems, ui::UiState& ui_state) {
    if (ui_state.selected_problem_id.empty())
        return;
    if (ProblemExists(problems, ui_state.selected_problem_id))
        return;
    ui_state.selected_problem_id.clear();
    ui_state.selected_problem_element_id.clear();
    ui_state.problems_panel_focus_pending = false;
}

void SelectAndActivateProblem(const core::ProblemItem& problem,
                              ui::UiState& ui_state,
                              const ProblemsPanelCallbacks& callbacks) {
    ui_state.selected_problem_id = problem.id;
    ui_state.selected_problem_element_id = problem.element_id;
    if (callbacks.on_problem_activated)
        callbacks.on_problem_activated(problem);
}

bool DrawClickableCell(const char* id_suffix,
                       const std::string& text,
                       const core::ProblemItem& problem,
                       ui::UiState& ui_state,
                       const ProblemsPanelCallbacks& callbacks,
                       ImVec4* text_color = nullptr) {
    if (text_color)
        ImGui::PushStyleColor(ImGuiCol_Text, *text_color);
    const float cell_width = ImGui::GetColumnWidth();
    const bool clicked = ImGui::Selectable(
        (text + "##" + id_suffix).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(cell_width, 0.0f));
    if (text_color)
        ImGui::PopStyleColor();
    if (clicked)
        SelectAndActivateProblem(problem, ui_state, callbacks);
    return clicked;
}

void DrawProblemRow(const core::ProblemItem& problem, ui::UiState& ui_state, const ProblemsPanelCallbacks& callbacks) {
    const bool selected = ui_state.selected_problem_id == problem.id;
    const bool focus_this_row = selected && ui_state.problems_panel_focus_pending;
    // Problem messages can carry English msgids from core/ validation; translate
    // defensively. tr() of an untranslatable (e.g. user-typed) string returns it
    // unchanged, so this is safe for dynamic content too.
    const std::string message = SingleLineMessage(AF_TR(problem.message));
    const bool review_problem = IsReviewSource(problem.source);

    ImGui::PushID(problem.id.c_str());
    ImGui::TableNextRow();
    if (selected) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ui::WithAlpha(ui::GetTheme().accent, 0.28f));
    }
    if (focus_this_row) {
        // SetScrollHereY needs to be called from inside the table row context.
        ImGui::SetScrollHereY(0.5f);
        ui_state.problems_panel_focus_pending = false;
    }

    ImGui::TableSetColumnIndex(0);
    ImVec4 severity_color = SeverityColor(problem.severity);
    DrawClickableCell("severity", core::ToString(problem.severity), problem, ui_state, callbacks, &severity_color);

    ImGui::TableSetColumnIndex(1);
    DrawClickableCell("source", core::ToString(problem.source), problem, ui_state, callbacks);

    ImGui::TableSetColumnIndex(2);
    DrawClickableCell("element", problem.element_id.empty() ? "-" : problem.element_id, problem, ui_state, callbacks);

    ImGui::TableSetColumnIndex(3);
    DrawClickableCell("type", problem.type, problem, ui_state, callbacks);

    ImGui::TableSetColumnIndex(4);
    DrawClickableCell("message", message, problem, ui_state, callbacks);
    if (ImGui::IsItemHovered() && !problem.message.empty()) {
        ImGui::SetTooltip("%s", AF_TR(problem.message).c_str());
    }

    ImGui::TableSetColumnIndex(5);
    DrawClickableCell(
        "guideline", problem.guideline_id.empty() ? "-" : problem.guideline_id, problem, ui_state, callbacks);

    ImGui::TableSetColumnIndex(6);
    if (review_problem && callbacks.on_open_review) {
        if (ImGui::SmallButton(AF_TR("Open review").c_str()))
            callbacks.on_open_review(problem);
    } else if (!problem.quick_fix_label.empty() && callbacks.on_quick_fix) {
        // quick_fix_label is set as an English msgid in app/; translate at render.
        if (ImGui::SmallButton(AF_TR(problem.quick_fix_label).c_str()))
            callbacks.on_quick_fix(problem);
    } else {
        ImGui::TextUnformatted("-");
    }

    ImGui::PopID();
}

} // namespace

void ShowProblemsPanel(float x,
                       float width,
                       float height,
                       float top_y,
                       ImGuiWindowFlags panel_flags,
                       ProblemsPanelModel model,
                       const ProblemsPanelCallbacks& callbacks) {
    ImGui::SetNextWindowPos(ImVec2(x, top_y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin((AF_TR("Problems") + "###Problems").c_str(), nullptr, panel_flags);

    ShowProblemsPanelContent(model, callbacks);

    ImGui::End();
}

void ShowProblemsPanelContent(ProblemsPanelModel model, const ProblemsPanelCallbacks& callbacks, bool show_title) {
    const std::vector<core::ProblemItem>& problems = model.problems_manager.GetProblems();
    ClearRemovedSelection(problems, model.ui_state);

    DrawHeader(show_title);
    DrawFilters(problems, model.ui_state.active_problem_filter);
    ImGui::Separator();

    if (problems.empty()) {
        ImGui::TextDisabled("%s", AF_TR("No problems found.").c_str());
        return;
    }

    int visible_count = CountMatches(problems, model.ui_state.active_problem_filter);
    if (visible_count == 0) {
        ImGui::TextDisabled("%s", AF_TR("No problems match the current filter.").c_str());
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

    if (ImGui::BeginTable("problems_table", 7, flags, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(AF_TR("Severity").c_str(), ImGuiTableColumnFlags_WidthFixed, 86.0f);
        ImGui::TableSetupColumn(AF_TR("Source").c_str(), ImGuiTableColumnFlags_WidthFixed, 128.0f);
        ImGui::TableSetupColumn(AF_TR("Element").c_str(), ImGuiTableColumnFlags_WidthFixed, 88.0f);
        ImGui::TableSetupColumn(AF_TR("Type").c_str(), ImGuiTableColumnFlags_WidthFixed, 96.0f);
        ImGui::TableSetupColumn(AF_TR("Message").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(AF_TR("Guideline").c_str(), ImGuiTableColumnFlags_WidthFixed, 116.0f);
        ImGui::TableSetupColumn(AF_TR("Fix").c_str(), ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableHeadersRow();

        for (const auto& problem : problems) {
            if (!MatchesFilter(problem, model.ui_state.active_problem_filter))
                continue;
            DrawProblemRow(problem, model.ui_state, callbacks);
        }

        ImGui::EndTable();
    }
}

} // namespace ui::panels
