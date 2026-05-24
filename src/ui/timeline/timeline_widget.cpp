#include "ui/timeline/timeline_widget.h"

#include "ui/gsn/gsn_dpi.h"
#include "ui/theme.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace ui::timeline {

namespace {

using core::audit::TimelineModel;
using core::audit::TimelinePoint;
using core::audit::TimelinePointType;
using core::audit::TimelineViewMode;
using ui::gsn::DpiSize;

const char* ViewModeLabel(TimelineViewMode mode) {
    switch (mode) {
        case TimelineViewMode::Baselines: return "Baselines";
        case TimelineViewMode::Snapshots: return "Snapshots";
        case TimelineViewMode::Changes: return "Changes";
        case TimelineViewMode::Compare: return "Compare";
        case TimelineViewMode::SelectedElement: return "Selected element";
    }
    return "?";
}

float MarkerHalfWidth(const TimelinePoint& p) {
    switch (p.type) {
        case TimelinePointType::Now:      return DpiSize(10.0f);
        case TimelinePointType::Baseline: return DpiSize(9.0f);
        case TimelinePointType::Snapshot: return DpiSize(6.0f);
        case TimelinePointType::Change:   return DpiSize(4.0f);
        default:                          return DpiSize(5.0f);
    }
}

} // namespace

std::vector<MarkerLayout> ComputeMarkerLayout(const TimelineModel& model,
                                              const ImVec2& rect_min,
                                              const ImVec2& rect_max) {
    std::vector<MarkerLayout> out;
    out.reserve(model.points.size());
    const float left = rect_min.x;
    const float right = rect_max.x;
    const float span = std::max(1.0f, right - left);
    const std::uint64_t latest = model.latest_sequence;
    for (std::size_t i = 0; i < model.points.size(); ++i) {
        const TimelinePoint& p = model.points[i];
        float t = (latest == 0)
                      ? 1.0f
                      : (static_cast<float>(p.transaction_sequence) / static_cast<float>(latest));
        t = std::clamp(t, 0.0f, 1.0f);
        MarkerLayout m;
        m.point_index = i;
        m.center_x = left + t * span;
        m.half_width = MarkerHalfWidth(p);
        out.push_back(m);
    }
    if (out.empty()) return out;

    // Distribute markers that share a transaction sequence (very common
    // when several baselines/snapshots are created in a row without
    // intervening edits). Without this, every such marker maps to the
    // same x and stacks unclickably. We spread each run of equal-sequence
    // markers across the space between the previous distinct-sequence
    // anchor (or rail_left) and the run's natural right-edge position
    // (which, for runs that end with NOW, is rail_right).
    const float gap = DpiSize(4.0f);
    std::size_t i = 0;
    const std::size_t n = out.size();
    while (i < n) {
        std::size_t j = i + 1;
        const std::uint64_t seq_i = model.points[out[i].point_index].transaction_sequence;
        while (j < n && model.points[out[j].point_index].transaction_sequence == seq_i)
            ++j;
        const std::size_t run_size = j - i;
        if (run_size > 1) {
            const float anchor_right = out[j - 1].center_x;
            float anchor_left;
            if (i == 0) {
                anchor_left = left;
            } else {
                anchor_left = out[i - 1].center_x +
                              out[i - 1].half_width + out[i].half_width + gap;
            }
            if (anchor_left > anchor_right) anchor_left = anchor_right;
            const float step = (run_size > 1)
                ? (anchor_right - anchor_left) / static_cast<float>(run_size - 1)
                : 0.0f;
            for (std::size_t k = 0; k < run_size; ++k) {
                out[i + k].center_x = anchor_left + step * static_cast<float>(k);
            }
        }
        i = j;
    }

    // Collision cleanup pass (right-to-left): if any adjacent pair is
    // still closer than their combined half-widths + gap, push the
    // earlier one left. NOW remains anchored at its computed position.
    for (std::size_t k = n; k-- > 1;) {
        const float min_spacing = out[k].half_width + out[k - 1].half_width + gap;
        const float max_allowed = out[k].center_x - min_spacing;
        if (out[k - 1].center_x > max_allowed)
            out[k - 1].center_x = max_allowed;
    }
    return out;
}

namespace {

void DrawBaselineMarker(ImDrawList* dl, ImVec2 center, float h_half, ImU32 fill, ImU32 stroke,
                        const std::string& label) {
    const float w = h_half;
    const float h = DpiSize(14.0f);
    ImVec2 tl(center.x - w, center.y - h);
    ImVec2 br(center.x + w, center.y + h);
    dl->AddRectFilled(tl, br, fill, DpiSize(3.0f));
    dl->AddRect(tl, br, stroke, DpiSize(3.0f), 0, DpiSize(1.0f));
    if (!label.empty()) {
        ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
        ImVec2 text_pos(center.x - text_size.x * 0.5f, br.y + DpiSize(2.0f));
        dl->AddText(text_pos, ui::GetTheme().text_secondary, label.c_str());
    }
}

void DrawSnapshotMarker(ImDrawList* dl, ImVec2 center, float h_half, ImU32 fill, ImU32 stroke) {
    const float r = h_half;
    ImVec2 pts[4] = {
        ImVec2(center.x, center.y - r),
        ImVec2(center.x + r, center.y),
        ImVec2(center.x, center.y + r),
        ImVec2(center.x - r, center.y),
    };
    dl->AddConvexPolyFilled(pts, 4, fill);
    dl->AddPolyline(pts, 4, stroke, ImDrawFlags_Closed, DpiSize(1.0f));
}

void DrawChangeMarker(ImDrawList* dl, ImVec2 center, float h_half, ImU32 fill, ImU32 stroke) {
    dl->AddCircleFilled(center, h_half, fill, 12);
    dl->AddCircle(center, h_half, stroke, 12, DpiSize(1.0f));
}

void DrawNowMarker(ImDrawList* dl, ImVec2 center, float h_half, ImU32 fill, ImU32 stroke,
                   const std::string& label) {
    const float w = h_half;
    const float h = DpiSize(14.0f);
    ImVec2 tl(center.x - w, center.y - h);
    ImVec2 br(center.x + w, center.y + h);
    dl->AddRectFilled(tl, br, fill, DpiSize(3.0f));
    dl->AddRect(tl, br, stroke, DpiSize(3.0f), 0, DpiSize(1.5f));
    if (!label.empty()) {
        ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
        ImVec2 text_pos(center.x - text_size.x * 0.5f, br.y + DpiSize(2.0f));
        dl->AddText(text_pos, ui::GetTheme().text_primary, label.c_str());
    }
}

} // namespace

TimelineAction RenderTimelineWidget(TimelineState& state,
                                    const TimelineModel& model,
                                    const ImVec2& rect_min,
                                    const ImVec2& rect_max,
                                    const char* tab_id) {
    TimelineAction action;

    // Scope every ImGui ID under the tab key so multiple package canvases
    // never collide on shared label suffixes (e.g. duplicate combo IDs when
    // two canvases co-exist in the same window).
    ImGui::PushID("af_timeline_widget");
    ImGui::PushID(tab_id ? tab_id : "");

    const ui::Theme& th = ui::GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // --- Background pill ---
    const float radius = DpiSize(8.0f);
    dl->AddRectFilled(rect_min, rect_max, ui::WithAlpha(th.surface_2, 0.85f), radius);
    dl->AddRect(rect_min, rect_max, th.border, radius, 0, DpiSize(1.0f));

    const float height = rect_max.y - rect_min.y;
    const float pad_x = DpiSize(8.0f);
    const float dropdown_w = DpiSize(110.0f);
    const float menu_btn_w = DpiSize(28.0f);

    // --- View-mode dropdown (left) ---
    ImGui::SetCursorScreenPos(ImVec2(rect_min.x + pad_x,
                                     rect_min.y + (height - ImGui::GetFrameHeight()) * 0.5f));
    ImGui::SetNextItemWidth(dropdown_w);
    {
        if (ImGui::BeginCombo("##tl_view_mode", ViewModeLabel(state.view_mode),
                              ImGuiComboFlags_HeightSmall)) {
            const TimelineViewMode modes[] = {TimelineViewMode::Baselines,
                                              TimelineViewMode::Snapshots,
                                              TimelineViewMode::Changes};
            for (TimelineViewMode m : modes) {
                bool selected = (m == state.view_mode);
                if (ImGui::Selectable(ViewModeLabel(m), selected)) {
                    action.type = TimelineActionType::ChangeViewMode;
                    action.view_mode = m;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    // --- Actions menu (right "⋯" button) ---
    const float menu_btn_x = rect_max.x - pad_x - menu_btn_w;
    ImGui::SetCursorScreenPos(ImVec2(menu_btn_x,
                                     rect_min.y + (height - ImGui::GetFrameHeight()) * 0.5f));
    {
        if (ImGui::Button("...##tl_actions", ImVec2(menu_btn_w, 0.0f))) {
            ImGui::OpenPopup("##tl_actions_popup");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Timeline actions");
        if (ImGui::BeginPopup("##tl_actions_popup")) {
            if (ImGui::MenuItem("Create baseline at current state")) {
                action.type = TimelineActionType::CreateBaseline;
            }
            if (ImGui::MenuItem("Create snapshot at current state")) {
                action.type = TimelineActionType::CreateSnapshot;
            }
            if (state.preview_sequence.has_value()) {
                ImGui::Separator();
                if (ImGui::MenuItem("Return to latest")) {
                    action.type = TimelineActionType::ReturnToLatest;
                }
            }
            ImGui::EndPopup();
        }
    }

    // --- Rail track ---
    // Gap between the dropdown's right edge and the rail's leftmost marker
    // center must be big enough that the first marker's hit-rect (which
    // extends `half_width + ~2dp` to the left of its center) cannot land on
    // the combo frame. 20dp gives clear separation at all DPI scales.
    const float rail_left = rect_min.x + pad_x + dropdown_w + DpiSize(20.0f);
    const float rail_right = menu_btn_x - DpiSize(20.0f);
    if (rail_right - rail_left < DpiSize(40.0f)) {
        // Not enough room — bail out.
        ImGui::PopID();
        ImGui::PopID();
        return action;
    }
    const float rail_y = rect_min.y + height * 0.5f;
    const float track_thickness = std::max(1.0f, DpiSize(2.0f));
    dl->AddRectFilled(ImVec2(rail_left, rail_y - track_thickness * 0.5f),
                      ImVec2(rail_right, rail_y + track_thickness * 0.5f),
                      ui::WithAlpha(th.border_strong, 0.75f), track_thickness);

    // --- Markers ---
    ImVec2 rail_min(rail_left, rail_y);
    ImVec2 rail_max(rail_right, rail_y);
    auto layouts = ComputeMarkerLayout(model, rail_min, rail_max);

    for (const MarkerLayout& m : layouts) {
        const TimelinePoint& p = model.points[m.point_index];
        ImVec2 center(m.center_x, rail_y);

        ImU32 fill = th.surface_2;
        ImU32 stroke = th.border_strong;
        switch (p.type) {
            case TimelinePointType::Now:
                fill = ui::WithAlpha(th.accent, 0.90f);
                stroke = th.accent_hover;
                DrawNowMarker(dl, center, m.half_width, fill, stroke, p.label);
                break;
            case TimelinePointType::Baseline:
                fill = ui::WithAlpha(th.accent, 0.55f);
                stroke = th.accent_hover;
                DrawBaselineMarker(dl, center, m.half_width, fill, stroke, p.label);
                break;
            case TimelinePointType::Snapshot:
                fill = ui::WithAlpha(th.text_secondary, 0.55f);
                stroke = th.border_strong;
                DrawSnapshotMarker(dl, center, m.half_width, fill, stroke);
                break;
            case TimelinePointType::Change:
                fill = ui::WithAlpha(th.text_secondary, 0.50f);
                stroke = th.border_strong;
                DrawChangeMarker(dl, center, m.half_width, fill, stroke);
                break;
            default:
                break;
        }

        // Hit-test invisible button for hover/click.
        char hit_id[48];
        std::snprintf(hit_id, sizeof(hit_id), "##tl_mk_%zu", m.point_index);
        const float btn_w = m.half_width * 2.0f + DpiSize(4.0f);
        const float btn_h = DpiSize(28.0f);
        ImGui::SetCursorScreenPos(ImVec2(center.x - btn_w * 0.5f, center.y - btn_h * 0.5f));
        ImGui::InvisibleButton(hit_id, ImVec2(btn_w, btn_h));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", p.tooltip.c_str());
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            if (p.type == TimelinePointType::Now) {
                action.type = TimelineActionType::ReturnToLatest;
            } else {
                action.type = TimelineActionType::PreviewSequence;
                action.sequence = p.transaction_sequence;
            }
        }
    }

    // --- Preview indicator ---
    if (state.preview_sequence.has_value() && model.latest_sequence > 0) {
        const float t = std::clamp(
            static_cast<float>(*state.preview_sequence) / static_cast<float>(model.latest_sequence),
            0.0f, 1.0f);
        const float x = rail_left + t * (rail_right - rail_left);
        const float h = DpiSize(18.0f);
        dl->AddLine(ImVec2(x, rail_y - h), ImVec2(x, rail_y + h),
                    ui::WithAlpha(th.accent_hover, 0.95f), DpiSize(2.0f));
    }

    ImGui::PopID();
    ImGui::PopID();
    return action;
}

} // namespace ui::timeline
