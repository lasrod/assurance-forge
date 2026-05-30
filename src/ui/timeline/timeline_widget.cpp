#include "ui/timeline/timeline_widget.h"

#include "ui/gsn/gsn_dpi.h"
#include "ui/i18n/localization.h"
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
using ui::gsn::DpiSize;

// Phase 3 visual constants. Marker visual sizes are intentionally compact
// — the hit-test rect is expanded to `kHitRadius` per marker so users can
// click the small visuals comfortably.
float VisibleDotRadius()      { return DpiSize(3.0f); }
float SnapshotDiamondRadius() { return DpiSize(5.0f); }
float BaselineLineHalfThk()   { return DpiSize(1.25f); }
float NowLineHalfThk()        { return DpiSize(1.75f); }
float RailHalfHeight()        { return DpiSize(9.0f); }   // marker visual half-height
float HitHalfWidth()          { return DpiSize(10.0f); }  // generous hit-test radius
float HitHalfHeight()         { return DpiSize(14.0f); }
float LabelBandHeight()       { return DpiSize(14.0f); }  // reserved above the rail for labels
float SelectionHaloRadius()   { return DpiSize(10.0f); }
float RailPad()               { return DpiSize(12.0f); }  // padding inside the rail rect

// MarkerHalfWidth is the visual half-width (used only for spacing
// estimation). Hit-test uses `HitHalfWidth()` regardless.
float MarkerHalfWidth(const TimelinePoint& p) {
    switch (p.type) {
        case TimelinePointType::Now:             return NowLineHalfThk() * 2.0f;
        case TimelinePointType::Baseline:        return BaselineLineHalfThk() * 2.0f;
        case TimelinePointType::InitialSnapshot: return SnapshotDiamondRadius();
        case TimelinePointType::Snapshot:        return SnapshotDiamondRadius();
        case TimelinePointType::Change:          return VisibleDotRadius();
        default:                                 return DpiSize(4.0f);
    }
}

} // namespace

// Phase 3.1: index-based even-spacing layout. Each marker occupies an
// equal slot across `[rect_min.x + pad, rect_max.x - pad]`. The synthetic
// `Now` marker (always the last point) naturally lands at the far right;
// every other marker is placed at its index fraction. No collision
// spreading is required because every slot is unique by construction.
std::vector<MarkerLayout> ComputeMarkerLayout(const TimelineModel& model,
                                              const ImVec2& rect_min,
                                              const ImVec2& rect_max) {
    std::vector<MarkerLayout> out;
    out.reserve(model.points.size());
    const std::size_t n = model.points.size();
    if (n == 0) return out;

    const float pad = RailPad();
    const float left = rect_min.x + pad;
    const float right = rect_max.x - pad;
    const float span = std::max(1.0f, right - left);

    for (std::size_t i = 0; i < n; ++i) {
        const TimelinePoint& p = model.points[i];
        const float t = (n == 1) ? 1.0f
                                 : static_cast<float>(i) / static_cast<float>(n - 1);
        MarkerLayout m;
        m.point_index = i;
        m.center_x = left + t * span;
        m.half_width = MarkerHalfWidth(p);
        out.push_back(m);
    }
    return out;
}

namespace {

void DrawBaselineMarker(ImDrawList* dl, ImVec2 center, ImU32 fill, ImU32 stroke,
                        const std::string& label, bool selected) {
    const float half_thk = BaselineLineHalfThk() * (selected ? 1.6f : 1.0f);
    const float h = RailHalfHeight();
    ImVec2 tl(center.x - half_thk, center.y - h);
    ImVec2 br(center.x + half_thk, center.y + h);
    dl->AddRectFilled(tl, br, stroke);
    // Inner fill for visual interest at higher DPI.
    if (half_thk > DpiSize(1.0f)) {
        ImVec2 itl(center.x - half_thk * 0.5f, center.y - h + 1.0f);
        ImVec2 ibr(center.x + half_thk * 0.5f, center.y + h - 1.0f);
        dl->AddRectFilled(itl, ibr, fill);
    }
    if (!label.empty()) {
        ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
        ImVec2 text_pos(center.x - text_size.x * 0.5f,
                        center.y - h - LabelBandHeight() + DpiSize(1.0f));
        dl->AddText(text_pos, ui::GetTheme().text_primary, label.c_str());
    }
}

void DrawSnapshotMarker(ImDrawList* dl, ImVec2 center, ImU32 fill, ImU32 stroke,
                        bool selected, bool initial, const std::string& label) {
    const float r = SnapshotDiamondRadius() * (selected ? 1.25f : 1.0f);
    ImVec2 pts[4] = {
        ImVec2(center.x, center.y - r),
        ImVec2(center.x + r, center.y),
        ImVec2(center.x, center.y + r),
        ImVec2(center.x - r, center.y),
    };
    dl->AddConvexPolyFilled(pts, 4, fill);
    dl->AddPolyline(pts, 4, stroke, ImDrawFlags_Closed, DpiSize(1.0f));
    if (initial && !label.empty()) {
        ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
        ImVec2 text_pos(center.x - text_size.x * 0.5f,
                        center.y - RailHalfHeight() - LabelBandHeight() + DpiSize(1.0f));
        dl->AddText(text_pos, ui::GetTheme().text_secondary, label.c_str());
    }
}

void DrawChangeMarker(ImDrawList* dl, ImVec2 center, ImU32 fill, ImU32 stroke, bool selected) {
    const float r = VisibleDotRadius() * (selected ? 1.6f : 1.0f);
    dl->AddCircleFilled(center, r, fill, 12);
    dl->AddCircle(center, r, stroke, 12, DpiSize(1.0f));
}

void DrawNowMarker(ImDrawList* dl, ImVec2 center, ImU32 fill, ImU32 stroke,
                   const std::string& label) {
    const float half_thk = NowLineHalfThk();
    const float h = RailHalfHeight() + DpiSize(2.0f);
    ImVec2 tl(center.x - half_thk, center.y - h);
    ImVec2 br(center.x + half_thk, center.y + h);
    dl->AddRectFilled(tl, br, fill);
    dl->AddRect(tl, br, stroke, 0.0f, 0, DpiSize(1.0f));
    if (!label.empty()) {
        ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
        ImVec2 text_pos(center.x - text_size.x * 0.5f,
                        center.y - h - LabelBandHeight() + DpiSize(1.0f));
        dl->AddText(text_pos, ui::GetTheme().accent_hover, label.c_str());
    }
}

void DrawSelectionHalo(ImDrawList* dl, ImVec2 center, ImU32 color) {
    dl->AddCircle(center, SelectionHaloRadius(), color, 16, DpiSize(1.5f));
}

bool MarkerIsPreviewed(const TimelinePoint& p, const TimelineState& state) {
    if (!state.preview_sequence.has_value()) return false;
    // Only the non-Now marker(s) at the preview sequence are "selected".
    // Multiple kinds (baseline + change) at the same sequence are all
    // highlighted — they represent the same instant in history.
    if (p.type == TimelinePointType::Now) return false;
    return p.transaction_sequence == *state.preview_sequence;
}

} // namespace

TimelineAction RenderTimelineWidget(TimelineState& state,
                                    const TimelineModel& model,
                                    const ImVec2& rect_min,
                                    const ImVec2& rect_max,
                                    const char* tab_id) {
    TimelineAction action;

    // Scope every ImGui ID under the tab key so multiple package canvases
    // never collide on shared label suffixes.
    ImGui::PushID("af_timeline_widget");
    ImGui::PushID(tab_id ? tab_id : "");

    const ui::Theme& th = ui::GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // --- Background pill ---
    const float bg_radius = DpiSize(8.0f);
    dl->AddRectFilled(rect_min, rect_max, ui::WithAlpha(th.surface_2, 0.85f), bg_radius);
    dl->AddRect(rect_min, rect_max, th.border, bg_radius, 0, DpiSize(1.0f));

    const float height = rect_max.y - rect_min.y;
    const float pad_x = DpiSize(8.0f);
    const float menu_btn_w = DpiSize(28.0f);

    // --- Actions menu (right "⋯" button) ---
    const float menu_btn_x = rect_max.x - pad_x - menu_btn_w;
    ImGui::SetCursorScreenPos(ImVec2(menu_btn_x,
                                     rect_min.y + (height - ImGui::GetFrameHeight()) * 0.5f));
    {
        if (ImGui::Button("...##tl_actions", ImVec2(menu_btn_w, 0.0f))) {
            ImGui::OpenPopup("##tl_actions_popup");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", AF_TR("Timeline actions").c_str());
        if (ImGui::BeginPopup("##tl_actions_popup")) {
            if (ImGui::MenuItem(AF_TR("Create baseline at current state").c_str())) {
                action.type = TimelineActionType::CreateBaseline;
            }
            if (ImGui::MenuItem(AF_TR("Create snapshot at current state").c_str())) {
                action.type = TimelineActionType::CreateSnapshot;
            }
            if (state.preview_sequence.has_value()) {
                ImGui::Separator();
                if (ImGui::MenuItem(AF_TR("Return to latest").c_str())) {
                    action.type = TimelineActionType::ReturnToLatest;
                }
            }
            ImGui::EndPopup();
        }
    }

    // --- Rail track ---
    // Phase 3.4: the view-mode dropdown was removed; the rail now extends
    // from the left pill edge to just before the actions button. We keep
    // a left/right gutter so the first / last markers cannot land on the
    // pill border.
    const float rail_left = rect_min.x + pad_x + DpiSize(8.0f);
    const float rail_right = menu_btn_x - DpiSize(12.0f);
    if (rail_right - rail_left < DpiSize(40.0f)) {
        // Not enough room — bail out.
        ImGui::PopID();
        ImGui::PopID();
        return action;
    }
    // Vertical centering accounts for the label band reserved above the rail
    // so labels never clip the panel border or scroll bar.
    const float rail_y = rect_min.y + LabelBandHeight() + (height - LabelBandHeight()) * 0.5f;
    const float track_thickness = std::max(1.0f, DpiSize(2.0f));
    dl->AddRectFilled(ImVec2(rail_left, rail_y - track_thickness * 0.5f),
                      ImVec2(rail_right, rail_y + track_thickness * 0.5f),
                      ui::WithAlpha(th.border_strong, 0.75f), track_thickness);

    // --- Markers ---
    ImVec2 rail_min(rail_left, rail_y);
    ImVec2 rail_max(rail_right, rail_y);
    auto layouts = ComputeMarkerLayout(model, rail_min, rail_max);

    const ImU32 halo_color = ui::WithAlpha(th.accent_hover, 0.85f);

    for (const MarkerLayout& m : layouts) {
        const TimelinePoint& p = model.points[m.point_index];
        ImVec2 center(m.center_x, rail_y);
        const bool selected = MarkerIsPreviewed(p, state);

        // Phase 3.7: selection halo first so the marker draws on top.
        if (selected) {
            DrawSelectionHalo(dl, center, halo_color);
        }

        ImU32 fill = th.surface_2;
        ImU32 stroke = th.border_strong;
        switch (p.type) {
            case TimelinePointType::Now:
                fill = ui::WithAlpha(th.accent, 0.95f);
                stroke = th.accent_hover;
                DrawNowMarker(dl, center, fill, stroke, p.label);
                break;
            case TimelinePointType::Baseline:
                fill = ui::WithAlpha(th.accent, 0.65f);
                stroke = th.accent_hover;
                DrawBaselineMarker(dl, center, fill, stroke, p.label, selected);
                break;
            case TimelinePointType::InitialSnapshot:
                fill = ui::WithAlpha(th.accent, 0.55f);
                stroke = th.accent_hover;
                DrawSnapshotMarker(dl, center, fill, stroke, selected, /*initial=*/true, p.label);
                break;
            case TimelinePointType::Snapshot:
                fill = ui::WithAlpha(th.text_secondary, 0.55f);
                stroke = th.border_strong;
                DrawSnapshotMarker(dl, center, fill, stroke, selected, /*initial=*/false,
                                   std::string());
                break;
            case TimelinePointType::Change:
                fill = ui::WithAlpha(th.text_secondary, 0.65f);
                stroke = th.border_strong;
                DrawChangeMarker(dl, center, fill, stroke, selected);
                break;
            default:
                break;
        }

        // Phase 3.5: hit-test rect — uniform `HitHalfWidth × HitHalfHeight`
        // around the marker center regardless of the marker's visual size,
        // so the tiny Change dot is just as clickable as a Baseline line.
        char hit_id[48];
        std::snprintf(hit_id, sizeof(hit_id), "##tl_mk_%zu", m.point_index);
        const float btn_w = HitHalfWidth() * 2.0f;
        const float btn_h = HitHalfHeight() * 2.0f;
        ImGui::SetCursorScreenPos(ImVec2(center.x - btn_w * 0.5f, center.y - btn_h * 0.5f));
        ImGui::InvisibleButton(hit_id, ImVec2(btn_w, btn_h));
        if (ImGui::IsItemHovered()) {
            // Phase 3.6: per-kind tooltip (uses the kind-specific text the
            // builder already formatted into `TimelinePoint::tooltip`).
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

    ImGui::PopID();
    ImGui::PopID();
    return action;
}

} // namespace ui::timeline
