#include "app/areas/perf_overlay_area.h"

#include "core/perf/frame_profiler.h"
#include "ui/gsn/gsn_canvas_renderer.h"
#include "ui/theme.h"

#include <hello_imgui/hello_imgui.h>
#include <hello_imgui/runner_params.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace app::areas {

namespace {

// =====================================================================
//  Frame-time history
// =====================================================================

constexpr int kFrameTimeHistorySize = 240;
constexpr float kBudget60Fps = 1000.0f / 60.0f; // 16.67 ms
constexpr float kBudget30Fps = 1000.0f / 30.0f; // 33.33 ms

struct FrameTimeHistory {
    // Primary series: wall-clock interval between presents (ms). This is what
    // the user actually experiences and what `ImGui::GetIO().Framerate` is
    // derived from — it includes any idle throttling done by hello_imgui.
    float samples_ms[kFrameTimeHistorySize] = {};
    // Secondary series: active render cost (ms), i.e. profiler total per
    // frame. Sits inside samples_ms when the runtime is not idle-throttled
    // and well below it when it is. Drawn as a thin overlay on the graph.
    float render_ms[kFrameTimeHistorySize] = {};
    int next_index = 0;
    int filled = 0;
};

FrameTimeHistory& History() {
    static FrameTimeHistory h;
    return h;
}

void PushFrameTime(float wall_ms, float render_ms) {
    FrameTimeHistory& h = History();
    h.samples_ms[h.next_index] = wall_ms;
    h.render_ms[h.next_index] = render_ms;
    h.next_index = (h.next_index + 1) % kFrameTimeHistorySize;
    if (h.filled < kFrameTimeHistorySize)
        ++h.filled;
}

float Average(const FrameTimeHistory& h) {
    if (h.filled == 0)
        return 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < h.filled; ++i)
        sum += h.samples_ms[i];
    return sum / static_cast<float>(h.filled);
}

float Maximum(const FrameTimeHistory& h) {
    float m = 0.0f;
    for (int i = 0; i < h.filled; ++i)
        m = std::max(m, h.samples_ms[i]);
    return m;
}

struct SpikeStats {
    int count = 0;
    float threshold_ms = 0.0f;
};

SpikeStats ComputeSpikeStats(const FrameTimeHistory& h, float avg_ms) {
    SpikeStats out;
    out.threshold_ms = std::max(2.0f * avg_ms, kBudget60Fps);
    for (int i = 0; i < h.filled; ++i) {
        if (h.samples_ms[i] > out.threshold_ms)
            ++out.count;
    }
    return out;
}

// =====================================================================
//  Subsystem grouping
// =====================================================================

struct SubsystemDef {
    const char* label;
    std::string_view prefix; // matched on first dotted segment(s) of the bucket name
    ImU32 color; // resolved from theme on each frame
};

enum class SubsystemId : int {
    UiAreas = 0,
    Workbench,
    ProjectExplorer,
    GsnCanvas,
    AppMisc,
    AI,
    Modals,
    Other,
    Count,
};

const char* SubsystemLabel(SubsystemId id) {
    switch (id) {
    case SubsystemId::UiAreas:         return "UI Areas";
    case SubsystemId::Workbench:       return "Workbench";
    case SubsystemId::ProjectExplorer: return "Project Explorer";
    case SubsystemId::GsnCanvas:       return "GSN Canvas";
    case SubsystemId::AppMisc:         return "App misc";
    case SubsystemId::AI:              return "AI";
    case SubsystemId::Modals:          return "Modals";
    case SubsystemId::Other:           return "Other";
    default:                           return "?";
    }
}

ImU32 SubsystemColor(SubsystemId id) {
    const ui::Theme& th = ui::GetTheme();
    switch (id) {
    case SubsystemId::UiAreas:         return th.accent;
    case SubsystemId::Workbench:       return th.success;
    case SubsystemId::ProjectExplorer: return th.info;
    case SubsystemId::GsnCanvas:       return th.warning;
    case SubsystemId::AppMisc:         return th.node_solution;
    case SubsystemId::AI:              return th.attention;
    case SubsystemId::Modals:          return th.node_context;
    case SubsystemId::Other:           return th.text_muted;
    default:                           return th.text_muted;
    }
}

SubsystemId ClassifyBucket(std::string_view name) {
    if (name.rfind("app.area.", 0) == 0)
        return SubsystemId::UiAreas;
    if (name.rfind("app.wb.", 0) == 0)
        return SubsystemId::Workbench;
    if (name.rfind("app.pe.", 0) == 0)
        return SubsystemId::ProjectExplorer;
    if (name.rfind("gsn.", 0) == 0)
        return SubsystemId::GsnCanvas;
    if (name.rfind("app.modal", 0) == 0)
        return SubsystemId::Modals;
    if (name.rfind("app.ai", 0) == 0)
        return SubsystemId::AI;
    if (name.rfind("app.", 0) == 0)
        return SubsystemId::AppMisc;
    return SubsystemId::Other;
}

struct SubsystemAccum {
    std::uint64_t total_ns = 0;
    int bucket_count = 0;
};

std::array<SubsystemAccum, static_cast<size_t>(SubsystemId::Count)>
GroupBySubsystem(const std::vector<core::perf::FrameSample>& samples) {
    std::array<SubsystemAccum, static_cast<size_t>(SubsystemId::Count)> out{};
    for (const auto& s : samples) {
        const SubsystemId id = ClassifyBucket(s.name ? s.name : "");
        out[static_cast<size_t>(id)].total_ns += s.total_ns;
        ++out[static_cast<size_t>(id)].bucket_count;
    }
    return out;
}

// =====================================================================
//  Drawing helpers
// =====================================================================

ImU32 FrameTimeStatusColor(float ms) {
    const ui::Theme& th = ui::GetTheme();
    if (ms <= kBudget60Fps * 0.9f)
        return th.success;
    if (ms <= kBudget60Fps)
        return ui::LerpColor(th.success, th.warning, 0.5f);
    if (ms <= kBudget30Fps)
        return th.warning;
    return th.danger;
}

ImU32 FpsStatusColor(float fps) {
    const ui::Theme& th = ui::GetTheme();
    if (fps >= 55.0f)
        return th.success;
    if (fps >= 30.0f)
        return th.warning;
    return th.danger;
}

ImU32 SpikeStatusColor(int count) {
    const ui::Theme& th = ui::GetTheme();
    if (count == 0)
        return th.success;
    if (count <= 2)
        return th.warning;
    return th.danger;
}

ImU32 HeadroomStatusColor(float headroom_pct) {
    const ui::Theme& th = ui::GetTheme();
    if (headroom_pct >= 40.0f)
        return th.success;
    if (headroom_pct >= 10.0f)
        return th.warning;
    return th.danger;
}

// Draw a KPI card. Returns the cursor advance.
void DrawKpiCard(const char* label,
                 const char* value,
                 const char* unit,
                 ImU32 status_color,
                 float width,
                 float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ui::Theme& th = ui::GetTheme();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1 = ImVec2(p0.x + width, p0.y + height);

    // Card fill + status border
    dl->AddRectFilled(p0, p1, th.surface_2, 6.0f);
    dl->AddRect(p0, p1, th.border, 6.0f);
    // Left accent stripe
    dl->AddRectFilled(p0, ImVec2(p0.x + 3.0f, p1.y), status_color, 6.0f, ImDrawFlags_RoundCornersLeft);

    // Label (small, muted, top-left)
    const float pad_x = 10.0f;
    const float pad_y = 6.0f;
    ImGui::PushStyleColor(ImGuiCol_Text, th.text_muted);
    dl->AddText(ImVec2(p0.x + pad_x, p0.y + pad_y), th.text_muted, label);
    ImGui::PopStyleColor();

    // Value (large, primary) - bottom-aligned so taller cards keep the
    // value visually anchored under the label without overflowing.
    ImFont* font = ImGui::GetFont();
    const float value_size = ImGui::GetFontSize() * 1.45f;
    const ImVec2 value_pos = ImVec2(p0.x + pad_x, p1.y - pad_y - value_size);
    dl->AddText(font, value_size, value_pos, th.text_primary, value);

    // Unit (small, muted, after value)
    if (unit && unit[0]) {
        const ImVec2 value_size_vec = font->CalcTextSizeA(value_size, FLT_MAX, 0.0f, value);
        const ImVec2 unit_pos =
            ImVec2(value_pos.x + value_size_vec.x + 4.0f, value_pos.y + value_size_vec.y - ImGui::GetTextLineHeight());
        dl->AddText(unit_pos, th.text_muted, unit);
    }

    // Status dot top-right
    const float dot_r = 4.0f;
    dl->AddCircleFilled(ImVec2(p1.x - pad_x, p0.y + pad_y + dot_r), dot_r, status_color);

    // Advance ImGui cursor (treat the whole card as a dummy)
    ImGui::Dummy(ImVec2(width, height));
}

// Draws the rolling frame-time graph using ImDrawList.
//
// Conventions (frame-TIME plot, read as FPS):
//   * Y axis is inverted so LOWER frame time (= HIGHER FPS) sits near the TOP.
//     The 60 FPS reference line therefore sits ABOVE the 30 FPS reference
//     line, matching user intuition ("higher is better").
//   * Zone bands: green at top (>60 FPS), warning in middle (30-60 FPS),
//     danger at bottom (<30 FPS).
//   * Filled area drops from the measured line DOWN to the baseline so a
//     well-performing run visibly fills the plot.
//   * Reference lines + labels are drawn LAST so they always sit on top of
//     the data.
void DrawFrameTimeGraph(const FrameTimeHistory& h, float avg_ms, float spike_threshold_ms, float graph_height) {
    const ui::Theme& th = ui::GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float avail_w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1 = ImVec2(p0.x + avail_w, p0.y + graph_height);

    // Background + border
    dl->AddRectFilled(p0, p1, th.canvas_bg, 4.0f);
    dl->AddRect(p0, p1, th.border, 4.0f);

    // Y range: at least up to 1.2 × 30 FPS budget so the 30 FPS line stays
    // visible even when measurements are excellent; stretches further if a
    // spike exceeds that. Includes the render-cost trace too — normally
    // smaller than wall-clock, but plotted on the same axis.
    float y_max_ms = kBudget30Fps * 1.2f;
    for (int i = 0; i < h.filled; ++i) {
        y_max_ms = std::max(y_max_ms, h.samples_ms[i]);
        y_max_ms = std::max(y_max_ms, h.render_ms[i]);
    }
    y_max_ms = std::max(y_max_ms, kBudget30Fps * 1.2f);

    // Inverted: 0 ms -> top, y_max_ms -> bottom.
    const float inner_top = p0.y + 1.0f;
    const float inner_bot = p1.y - 1.0f;
    const float inner_h = inner_bot - inner_top;
    auto y_for = [&](float ms) {
        const float t = std::clamp(ms / y_max_ms, 0.0f, 1.0f);
        return inner_top + t * inner_h;
    };

    const float y_60 = y_for(kBudget60Fps);
    const float y_30 = y_for(kBudget30Fps);

    // Zone bands: green above 60 FPS, warning between 30-60, danger below 30.
    dl->AddRectFilled(ImVec2(p0.x, inner_top), ImVec2(p1.x, y_60),      ui::WithAlpha(th.success, 0.06f));
    dl->AddRectFilled(ImVec2(p0.x, y_60),      ImVec2(p1.x, y_30),      ui::WithAlpha(th.warning, 0.07f));
    dl->AddRectFilled(ImVec2(p0.x, y_30),      ImVec2(p1.x, inner_bot), ui::WithAlpha(th.danger,  0.08f));

    auto draw_reference_lines_and_labels = [&]() {
        // Hide the 60 FPS line when it would visually overlap the 30 FPS
        // line (happens at very low FPS where y_max stretches and both
        // budgets compress toward the top of the chart). At that point
        // showing both is misleading — the 30 FPS line is the meaningful
        // boundary to focus on.
        const float min_line_gap_px = ImGui::GetTextLineHeight() + 4.0f;
        const bool show_60 = (y_30 - y_60) >= min_line_gap_px;

        if (show_60) {
            dl->AddLine(ImVec2(p0.x + 1, y_60), ImVec2(p1.x - 1, y_60), ui::WithAlpha(th.warning, 0.75f), 1.0f);
        }
        dl->AddLine(ImVec2(p0.x + 1, y_30), ImVec2(p1.x - 1, y_30), ui::WithAlpha(th.danger,  0.75f), 1.0f);

        // Both labels sit just below their reference line, inside the band
        // that line caps off (60 FPS -> warning band, 30 FPS -> danger band).
        // Because the bands are stacked top-to-bottom, the labels can never
        // collide regardless of how compressed the upper portion of the
        // axis becomes at very low FPS.
        char buf[16];
        float label_w;
        if (show_60) {
            std::snprintf(buf, sizeof(buf), "60 FPS");
            label_w = ImGui::CalcTextSize(buf).x;
            dl->AddText(ImVec2(p1.x - label_w - 8.0f, y_60 + 2.0f),
                        ui::WithAlpha(th.warning, 0.9f), buf);
        }
        std::snprintf(buf, sizeof(buf), "30 FPS");
        label_w = ImGui::CalcTextSize(buf).x;
        dl->AddText(ImVec2(p1.x - label_w - 8.0f, y_30 + 2.0f),
                    ui::WithAlpha(th.danger, 0.9f), buf);
    };

    if (h.filled < 2) {
        draw_reference_lines_and_labels();
        ImGui::Dummy(ImVec2(avail_w, graph_height));
        return;
    }

    // Chronological iteration: oldest -> newest, mapped left -> right
    const int start = (h.next_index + kFrameTimeHistorySize - h.filled) % kFrameTimeHistorySize;
    const float plot_w = (p1.x - p0.x) - 2.0f;
    auto x_for = [&](int idx) {
        const float t = static_cast<float>(idx) / static_cast<float>(h.filled - 1);
        return p0.x + 1.0f + t * plot_w;
    };

    // Per-segment trapezoid fill + colored line. The trapezoid is filled from
    // the line DOWN to the baseline so the area visibly represents "frame
    // time consumed". Colors come from FrameTimeStatusColor so each segment
    // of the trace signals whether we crossed a threshold.
    for (int i = 1; i < h.filled; ++i) {
        const float ms_prev = h.samples_ms[(start + i - 1) % kFrameTimeHistorySize];
        const float ms_cur  = h.samples_ms[(start + i)     % kFrameTimeHistorySize];
        const ImVec2 a = ImVec2(x_for(i - 1), y_for(ms_prev));
        const ImVec2 b = ImVec2(x_for(i),     y_for(ms_cur));

        const ImU32 col_a = FrameTimeStatusColor(ms_prev);
        const ImU32 col_b = FrameTimeStatusColor(ms_cur);
        const ImU32 mid   = ui::LerpColor(col_a, col_b, 0.5f);
        const ImU32 fill  = ui::WithAlpha(mid, 0.55f);

        // Trapezoid from line down to baseline
        dl->AddQuadFilled(a, b, ImVec2(b.x, inner_bot), ImVec2(a.x, inner_bot), fill);
        // Crisp line on top of fill
        dl->AddLine(a, b, mid, 1.6f);
    }

    // Secondary trace: render cost (ms inside the frame, excluding any
    // hello_imgui idle throttling). Drawn as a thin muted line so the gap
    // between this and the primary (wall-clock) trace visualises idle time.
    {
        const ImU32 render_col = ui::WithAlpha(th.text_secondary, 0.85f);
        for (int i = 1; i < h.filled; ++i) {
            const float r_prev = h.render_ms[(start + i - 1) % kFrameTimeHistorySize];
            const float r_cur  = h.render_ms[(start + i)     % kFrameTimeHistorySize];
            const ImVec2 a = ImVec2(x_for(i - 1), y_for(r_prev));
            const ImVec2 b = ImVec2(x_for(i),     y_for(r_cur));
            dl->AddLine(a, b, render_col, 1.0f);
        }
    }

    // Spike markers
    for (int i = 0; i < h.filled; ++i) {
        const float ms = h.samples_ms[(start + i) % kFrameTimeHistorySize];
        if (ms > spike_threshold_ms) {
            dl->AddCircleFilled(ImVec2(x_for(i), y_for(ms)), 3.0f, th.danger);
        }
    }

    // Average line (subtle dashed horizontal)
    if (avg_ms > 0.0f) {
        const float y_avg = y_for(avg_ms);
        const ImU32 col = ui::WithAlpha(th.text_secondary, 0.6f);
        for (float x = p0.x + 2.0f; x < p1.x - 6.0f; x += 6.0f)
            dl->AddLine(ImVec2(x, y_avg), ImVec2(x + 3.0f, y_avg), col, 1.0f);
    }

    // Guidelines + labels drawn LAST so they sit on top of the trace.
    draw_reference_lines_and_labels();

    ImGui::Dummy(ImVec2(avail_w, graph_height));
}

// Draws the per-frame subsystem stacked bar with a legend underneath.
void DrawSubsystemStackedBar(const std::array<SubsystemAccum, static_cast<size_t>(SubsystemId::Count)>& groups,
                             std::uint64_t frame_total_ns,
                             float height) {
    const ui::Theme& th = ui::GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1 = ImVec2(p0.x + avail_w, p0.y + height);

    dl->AddRectFilled(p0, p1, th.surface_1, 6.0f);
    dl->AddRect(p0, p1, th.border, 6.0f);

    if (frame_total_ns == 0) {
        ImGui::Dummy(ImVec2(avail_w, height));
        return;
    }

    // Collect non-zero groups sorted desc by time
    struct GroupView {
        SubsystemId id;
        std::uint64_t ns;
    };
    std::vector<GroupView> views;
    views.reserve(static_cast<size_t>(SubsystemId::Count));
    for (size_t i = 0; i < groups.size(); ++i) {
        if (groups[i].total_ns > 0)
            views.push_back({static_cast<SubsystemId>(i), groups[i].total_ns});
    }
    std::sort(views.begin(), views.end(), [](const GroupView& a, const GroupView& b) { return a.ns > b.ns; });

    const float bar_pad = 2.0f;
    const ImVec2 bar_p0 = ImVec2(p0.x + bar_pad, p0.y + bar_pad);
    const ImVec2 bar_p1 = ImVec2(p1.x - bar_pad, p1.y - bar_pad);
    const float bar_w = bar_p1.x - bar_p0.x;
    const float bar_h = bar_p1.y - bar_p0.y;

    float x = bar_p0.x;
    int seg_idx = 0;
    const int seg_count = static_cast<int>(views.size());
    for (const auto& v : views) {
        const float frac = static_cast<float>(v.ns) / static_cast<float>(frame_total_ns);
        const float w = std::max(2.0f, frac * bar_w);
        const ImVec2 s0 = ImVec2(x, bar_p0.y);
        const ImVec2 s1 = ImVec2(x + w, bar_p1.y);
        const ImU32 col = SubsystemColor(v.id);
        // Rounded corners only at outer ends
        ImDrawFlags flags = 0;
        if (seg_idx == 0)
            flags |= ImDrawFlags_RoundCornersLeft;
        if (seg_idx == seg_count - 1)
            flags |= ImDrawFlags_RoundCornersRight;
        dl->AddRectFilled(s0, s1, col, 4.0f, flags);
        // Subtle highlight at top
        dl->AddRectFilled(s0, ImVec2(s1.x, s0.y + bar_h * 0.35f), ui::WithAlpha(IM_COL32(255, 255, 255, 255), 0.05f),
                          4.0f, flags);

        // Inline label if there's room
        if (w >= 60.0f) {
            const char* lbl = SubsystemLabel(v.id);
            const ImVec2 ts = ImGui::CalcTextSize(lbl);
            if (ts.x + 8.0f <= w) {
                const ImVec2 tp = ImVec2(s0.x + (w - ts.x) * 0.5f, s0.y + (bar_h - ts.y) * 0.5f);
                dl->AddText(tp, ui::InkOn(col), lbl);
            }
        }

        // Tooltip on hover
        if (ImGui::IsMouseHoveringRect(s0, s1)) {
            ImGui::SetTooltip("%s\n%.2f ms  (%.1f%%)", SubsystemLabel(v.id),
                              static_cast<float>(v.ns) / 1.0e6f, frac * 100.0f);
        }

        x += w;
        ++seg_idx;
    }

    ImGui::Dummy(ImVec2(avail_w, height));

    // Legend chips
    ImGui::Spacing();
    const float chip_pad_x = 8.0f;
    const float chip_pad_y = 3.0f;
    const float chip_spacing = 6.0f;
    float row_x = ImGui::GetCursorScreenPos().x;
    const float row_x0 = row_x;
    const float row_right = row_x + ImGui::GetContentRegionAvail().x;
    float row_y = ImGui::GetCursorScreenPos().y;
    ImDrawList* dl2 = ImGui::GetWindowDrawList();
    float chip_h = ImGui::GetTextLineHeight() + chip_pad_y * 2.0f;
    float row_max_h = chip_h;

    for (const auto& v : views) {
        char label[96];
        std::snprintf(label, sizeof(label), "%s  %.2f ms", SubsystemLabel(v.id),
                      static_cast<float>(v.ns) / 1.0e6f);
        const float lbl_w = ImGui::CalcTextSize(label).x;
        const float dot_r = 4.0f;
        const float chip_w = chip_pad_x + dot_r * 2.0f + 6.0f + lbl_w + chip_pad_x;
        if (row_x + chip_w > row_right) {
            row_x = row_x0;
            row_y += chip_h + 4.0f;
            row_max_h += chip_h + 4.0f;
        }
        const ImVec2 c0 = ImVec2(row_x, row_y);
        const ImVec2 c1 = ImVec2(row_x + chip_w, row_y + chip_h);
        dl2->AddRectFilled(c0, c1, th.surface_2, 10.0f);
        dl2->AddRect(c0, c1, th.border, 10.0f);
        dl2->AddCircleFilled(ImVec2(c0.x + chip_pad_x + dot_r, c0.y + chip_h * 0.5f), dot_r, SubsystemColor(v.id));
        dl2->AddText(ImVec2(c0.x + chip_pad_x + dot_r * 2.0f + 6.0f, c0.y + chip_pad_y), th.text_secondary, label);
        row_x += chip_w + chip_spacing;
    }
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, row_max_h + 2.0f));
}

// =====================================================================
//  Hierarchical bucket tree
// =====================================================================

struct BucketNode {
    std::string segment;
    std::string full_name; // only set on leaves
    std::uint64_t total_ns = 0;
    std::uint32_t hit_count = 0;
    std::map<std::string, BucketNode> children;
    bool is_leaf = false;
};

void InsertSample(BucketNode& root, const core::perf::FrameSample& s) {
    if (!s.name)
        return;
    std::string_view name(s.name);
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos <= name.size()) {
        size_t dot = name.find('.', pos);
        if (dot == std::string_view::npos)
            dot = name.size();
        parts.emplace_back(name.substr(pos, dot - pos));
        if (dot == name.size())
            break;
        pos = dot + 1;
    }

    BucketNode* node = &root;
    node->total_ns += s.total_ns;
    for (size_t i = 0; i < parts.size(); ++i) {
        auto [it, inserted] = node->children.try_emplace(parts[i]);
        BucketNode& child = it->second;
        if (inserted)
            child.segment = parts[i];
        child.total_ns += s.total_ns;
        if (i + 1 == parts.size()) {
            child.is_leaf = true;
            child.full_name = std::string(name);
            child.hit_count += s.hit_count;
        }
        node = &child;
    }
}

BucketNode BuildBucketTree(const std::vector<core::perf::FrameSample>& samples) {
    BucketNode root;
    for (const auto& s : samples)
        InsertSample(root, s);
    return root;
}

void DrawBarInline(float frac, float width, ImU32 color, float height) {
    const ui::Theme& th = ui::GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1 = ImVec2(p0.x + width, p0.y + height);
    dl->AddRectFilled(p0, p1, ui::WithAlpha(th.surface_3, 0.6f), 2.0f);
    const float fw = std::max(1.0f, std::min(1.0f, frac) * width);
    dl->AddRectFilled(p0, ImVec2(p0.x + fw, p1.y), color, 2.0f);
    ImGui::Dummy(ImVec2(width, height));
}

void DrawTreeNodeRow(const BucketNode& node, std::uint64_t frame_total_ns, int depth) {
    const ui::Theme& th = ui::GetTheme();
    const float frac = frame_total_ns > 0
        ? static_cast<float>(node.total_ns) / static_cast<float>(frame_total_ns)
        : 0.0f;
    const float ms = static_cast<float>(node.total_ns) / 1.0e6f;

    char header[128];
    if (node.is_leaf && node.children.empty()) {
        std::snprintf(header, sizeof(header), "%s", node.segment.c_str());
    } else {
        std::snprintf(header, sizeof(header), "%s  (%.2f ms)", node.segment.c_str(), ms);
    }

    // Sort children by descending total_ns
    std::vector<const BucketNode*> sorted_children;
    sorted_children.reserve(node.children.size());
    for (const auto& kv : node.children)
        sorted_children.push_back(&kv.second);
    std::sort(sorted_children.begin(), sorted_children.end(),
              [](const BucketNode* a, const BucketNode* b) { return a->total_ns > b->total_ns; });

    const bool is_leaf_only = node.is_leaf && node.children.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
    if (is_leaf_only)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
    if (depth <= 1)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;

    const float row_start_x = ImGui::GetCursorPosX();
    bool open = ImGui::TreeNodeEx((std::string("##") + std::to_string(reinterpret_cast<std::uintptr_t>(&node))).c_str(),
                                  flags, "%s", header);

    // On the same line: time, %, hits, inline bar
    ImGui::SameLine();
    const float right_edge = ImGui::GetWindowContentRegionMax().x + ImGui::GetWindowPos().x;
    const float window_x0 = ImGui::GetWindowPos().x;
    // Columns laid out from the right edge:
    //   [ms (60px) | pct (50px) | hits (40px) | bar (90px) ]
    const float bar_w = 90.0f;
    const float hits_w = 42.0f;
    const float pct_w = 52.0f;
    const float ms_w = 62.0f;
    const float gap = 4.0f;
    const float total_right = bar_w + hits_w + pct_w + ms_w + gap * 3;
    const float right_x = right_edge - window_x0 - 4.0f;

    // ms
    ImGui::SameLine(right_x - total_right);
    ImGui::PushStyleColor(ImGuiCol_Text, th.text_primary);
    ImGui::Text("%6.2f ms", ms);
    ImGui::PopStyleColor();

    // pct
    ImGui::SameLine(right_x - total_right + ms_w + gap);
    ImGui::PushStyleColor(ImGuiCol_Text, th.text_secondary);
    ImGui::Text("%5.1f%%", frac * 100.0f);
    ImGui::PopStyleColor();

    // hits
    ImGui::SameLine(right_x - total_right + ms_w + pct_w + gap * 2);
    ImGui::PushStyleColor(ImGuiCol_Text, th.text_muted);
    if (is_leaf_only)
        ImGui::Text("%4u", node.hit_count);
    else
        ImGui::TextUnformatted("    ");
    ImGui::PopStyleColor();

    // bar
    ImGui::SameLine(right_x - bar_w);
    const SubsystemId sid = ClassifyBucket(node.is_leaf ? node.full_name : "");
    ImU32 bar_col = (depth == 0)
        ? th.accent
        : (node.is_leaf ? SubsystemColor(sid) : SubsystemColor(ClassifyBucket(node.segment + ".")));
    // Top-level groups: pick a color by first segment
    if (depth == 1) {
        bar_col = SubsystemColor(ClassifyBucket(node.segment + "."));
    }
    DrawBarInline(frac, bar_w, bar_col, ImGui::GetTextLineHeight() * 0.6f);

    (void)row_start_x;

    if (open && !is_leaf_only) {
        for (const BucketNode* child : sorted_children)
            DrawTreeNodeRow(*child, frame_total_ns, depth + 1);
        ImGui::TreePop();
    }
}

// =====================================================================
//  Existing helpers (canvas stats chips)
// =====================================================================

void DrawCounterChip(const char* label, int value, std::optional<ImVec4> color_opt = std::nullopt) {
    const ui::Theme& th = ui::GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %d", label, value);
    const ImVec2 ts = ImGui::CalcTextSize(buf);
    const float pad_x = 8.0f;
    const float pad_y = 3.0f;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1 = ImVec2(p0.x + ts.x + pad_x * 2, p0.y + ts.y + pad_y * 2);
    dl->AddRectFilled(p0, p1, th.surface_2, 10.0f);
    dl->AddRect(p0, p1, th.border, 10.0f);
    ImU32 text_col = color_opt.has_value() ? ImGui::ColorConvertFloat4ToU32(*color_opt) : th.text_secondary;
    dl->AddText(ImVec2(p0.x + pad_x, p0.y + pad_y), text_col, buf);
    ImGui::Dummy(ImVec2(ts.x + pad_x * 2, ts.y + pad_y * 2));
}

void DrawCullChip(const char* label, int drawn, int culled) {
    const int total = drawn + culled;
    const float ratio = total > 0 ? static_cast<float>(culled) / static_cast<float>(total) : 0.0f;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s %d/%d  %.0f%% culled", label, drawn, total, ratio * 100.0f);
    const ui::Theme& th = ui::GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 ts = ImGui::CalcTextSize(buf);
    const float pad_x = 8.0f;
    const float pad_y = 3.0f;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1 = ImVec2(p0.x + ts.x + pad_x * 2, p0.y + ts.y + pad_y * 2);
    dl->AddRectFilled(p0, p1, th.surface_2, 10.0f);
    dl->AddRect(p0, p1, th.border, 10.0f);
    const ImU32 col = ImGui::ColorConvertFloat4ToU32(ui::CullRatioColor(ratio));
    // dot
    dl->AddCircleFilled(ImVec2(p0.x + pad_x - 1.0f, p0.y + (ts.y + pad_y * 2) * 0.5f), 3.0f, col);
    dl->AddText(ImVec2(p0.x + pad_x + 6.0f, p0.y + pad_y), th.text_secondary, buf);
    ImGui::Dummy(ImVec2(ts.x + pad_x * 2 + 6.0f, ts.y + pad_y * 2));
    (void)th; (void)col;
}

void ChipsRowBegin() {
    // helper: nothing for now, callers use SameLine between chips.
}

// =====================================================================
//  Report builder (unchanged plain-text output)
// =====================================================================

std::string BuildReport(float total_ms,
                        float avg_ms,
                        float max_ms,
                        float fps,
                        const std::vector<core::perf::FrameSample>& sorted_samples,
                        std::uint64_t total_ns,
                        const ui::gsn::CanvasRenderStats& stats,
                        const core::perf::PerfToggles& toggles) {
    std::string out;
    out.reserve(2048);
    char buf[256];

    std::snprintf(buf,
                  sizeof(buf),
                  "Performance snapshot\n"
                  "FPS: %.1f  Frame: %.2f ms  (avg %.2f / max %.2f ms over history)\n\n",
                  fps,
                  total_ms,
                  avg_ms,
                  max_ms);
    out += buf;

    out += "Profiler buckets (sorted by time):\n";
    out += "  Bucket                                        Time(ms)   %Frame   Hits\n";
    for (const auto& s : sorted_samples) {
        const float ms = static_cast<float>(s.total_ns) / 1.0e6f;
        const float pct = total_ns > 0 ? 100.0f * static_cast<float>(s.total_ns) / static_cast<float>(total_ns) : 0.0f;
        std::snprintf(
            buf, sizeof(buf), "  %-44s %8.3f %7.1f%% %6u\n", s.name ? s.name : "(null)", ms, pct, s.hit_count);
        out += buf;
    }

    out += "\nCanvas render stats:\n";
    std::snprintf(buf,
                  sizeof(buf),
                  "  nodes_drawn=%d nodes_culled=%d edges_drawn=%d edges_culled=%d\n"
                  "  shadows=%d interior_shading=%d selection_glow=%d acp_decorators=%d\n"
                  "  terminology_spans=%d terminology_tokens_scanned=%d clip_rect_pushes=%d\n"
                  "  draw_list_vtx=%d draw_list_idx=%d draw_list_cmds=%d\n",
                  stats.nodes_drawn,
                  stats.nodes_culled,
                  stats.edges_drawn,
                  stats.edges_culled,
                  stats.shadows_drawn,
                  stats.interior_shading_drawn,
                  stats.selection_glow_drawn,
                  stats.acp_decorators_drawn,
                  stats.terminology_spans_drawn,
                  stats.terminology_tokens_scanned,
                  stats.clip_rect_pushes,
                  stats.draw_list_vtx,
                  stats.draw_list_idx,
                  stats.draw_list_cmds);
    out += buf;

    out += "\nFeature toggles:\n";
    std::snprintf(buf,
                  sizeof(buf),
                  "  node_shadows=%d node_interior_shading=%d selection_glow=%d\n"
                  "  terminology_spans=%d acp_decorators=%d high_segment_circles=%d\n"
                  "  freeze_acp_builds=%d\n",
                  toggles.node_shadows,
                  toggles.node_interior_shading,
                  toggles.selection_glow,
                  toggles.terminology_spans,
                  toggles.acp_decorators,
                  toggles.high_segment_circles,
                  toggles.freeze_acp_builds);
    out += buf;

    return out;
}

// Legacy helper kept (used by raw table only)
void DrawCounter(const char* label, int value) {
    ImGui::Text("%-26s %d", label, value);
}

} // namespace

void RenderPerfOverlay(bool& open) {
    if (!open)
        return;

    // Capture our own cost so it shows up in the tree we're about to draw.
    core::perf::ScopedTimer perf_scope_self("app.area.perf_overlay");

    // Pause snapshot: when paused, we freeze everything shown in the overlay
    // (frame-time history, KPI values, samples, canvas stats) so the user can
    // read the values without them flickering.
    static bool s_paused = false;
    struct PausedSnapshot {
        std::uint64_t total_ns = 0;     // render cost in ns (sum of profiler buckets)
        float render_ms = 0.0f;         // render cost in ms (== total_ns / 1e6)
        float wall_ms = 0.0f;           // wall-clock interval since previous frame
        float fps = 0.0f;
        std::vector<core::perf::FrameSample> samples;
        ui::gsn::CanvasRenderStats canvas_stats{};
    };
    static PausedSnapshot s_snapshot;

    const std::uint64_t live_total_ns = core::perf::GetLastFrameTotalNs();
    const float live_render_ms = static_cast<float>(live_total_ns) / 1.0e6f;
    const float live_wall_ms = ImGui::GetIO().DeltaTime * 1000.0f;
    if (!s_paused)
        PushFrameTime(live_wall_ms, live_render_ms);

    ImGui::SetNextWindowSize(ImVec2(620, 820), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Performance", &open)) {
        ImGui::End();
        return;
    }

    // Refresh the snapshot every frame while not paused; while paused, hold
    // the last captured values.
    if (!s_paused) {
        s_snapshot.total_ns = live_total_ns;
        s_snapshot.render_ms = live_render_ms;
        s_snapshot.wall_ms = live_wall_ms;
        s_snapshot.fps = ImGui::GetIO().Framerate;
        const auto& live_samples = core::perf::GetLastFrameSamples();
        s_snapshot.samples.assign(live_samples.begin(), live_samples.end());
        s_snapshot.canvas_stats = ui::gsn::GetLastCanvasRenderStats();
    }

    const std::uint64_t total_ns = s_snapshot.total_ns;
    const float render_ms = s_snapshot.render_ms;
    // Primary "total_ms" used for top-line KPIs and headroom: wall-clock
    // interval, so it matches the FPS value the user sees on screen.
    const float total_ms = s_snapshot.wall_ms;
    const float fps = s_snapshot.fps;

    const FrameTimeHistory& h = History();
    const float avg_ms = Average(h);
    const float max_ms = Maximum(h);
    const SpikeStats spikes = ComputeSpikeStats(h, avg_ms);
    const float headroom_pct = total_ms > 0.0f
        ? std::max(0.0f, (1.0f - total_ms / kBudget60Fps)) * 100.0f
        : 100.0f;

    // Top bar: pause toggle + status indicator
    {
        const bool was_paused = s_paused;
        if (was_paused) {
            const ImU32 base = ImGui::ColorConvertFloat4ToU32(ui::GetWarningColor());
            ImGui::PushStyleColor(ImGuiCol_Button, base);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::ShadeColor(base, 0.15f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::ShadeColor(base, -0.10f));
        }
        if (ImGui::Button(was_paused ? "Resume" : "Pause"))
            s_paused = !s_paused;
        if (was_paused)
            ImGui::PopStyleColor(3);
        ImGui::SameLine();
        if (s_paused) {
            ImGui::TextColored(ui::GetWarningColor(), "\xe2\x97\x8f PAUSED");
            ImGui::SameLine();
            ImGui::TextDisabled("(history & values frozen)");
        } else {
            ImGui::TextDisabled("(live)");
        }

        // VSync toggle — flips runnerParams.fpsIdling.vsyncToMonitor, which
        // hello_imgui re-applies to the backend swap interval every frame.
        if (HelloImGui::RunnerParams* rp = HelloImGui::GetRunnerParams()) {
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(12.0f, 0.0f));
            ImGui::SameLine();
            bool vsync = rp->fpsIdling.vsyncToMonitor;
            if (ImGui::Checkbox("VSync", &vsync))
                rp->fpsIdling.vsyncToMonitor = vsync;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Disable to uncap the frame rate (useful for measuring raw render cost).");
        }
    }
    ImGui::Spacing();

    // =================================================================
    // Section 1: KPI cards row
    // =================================================================
    {
        const float avail = ImGui::GetContentRegionAvail().x;
        const int cards = 6;
        const float spacing = 6.0f;
        const float card_w = std::max(64.0f, (avail - spacing * (cards - 1)) / cards);
        const float card_h = 72.0f;

        char buf[32];

        std::snprintf(buf, sizeof(buf), "%.1f", fps);
        DrawKpiCard("FPS", buf, "", FpsStatusColor(fps), card_w, card_h);
        ImGui::SameLine(0.0f, spacing);

        std::snprintf(buf, sizeof(buf), "%.2f", total_ms);
        DrawKpiCard("FRAME", buf, "ms", FrameTimeStatusColor(total_ms), card_w, card_h);
        ImGui::SameLine(0.0f, spacing);

        std::snprintf(buf, sizeof(buf), "%.2f", avg_ms);
        DrawKpiCard("AVG", buf, "ms", FrameTimeStatusColor(avg_ms), card_w, card_h);
        ImGui::SameLine(0.0f, spacing);

        std::snprintf(buf, sizeof(buf), "%.2f", max_ms);
        DrawKpiCard("MAX", buf, "ms", FrameTimeStatusColor(max_ms), card_w, card_h);
        ImGui::SameLine(0.0f, spacing);

        std::snprintf(buf, sizeof(buf), "%.0f%%", headroom_pct);
        DrawKpiCard("HEADROOM", buf, "", HeadroomStatusColor(headroom_pct), card_w, card_h);
        ImGui::SameLine(0.0f, spacing);

        std::snprintf(buf, sizeof(buf), "%d", spikes.count);
        DrawKpiCard("SPIKES", buf, "/4s", SpikeStatusColor(spikes.count), card_w, card_h);
    }

    ImGui::Spacing();

    // =================================================================
    // Section 2: Frame-time graph
    // =================================================================
    {
        const ui::Theme& th_legend = ui::GetTheme();
        ImGui::TextUnformatted("Frame interval (4s) —");
        ImGui::SameLine(0.0f, 6.0f);
        // Wall-clock swatch
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h_line = ImGui::GetTextLineHeight();
            dl->AddRectFilled(ImVec2(p.x, p.y + h_line * 0.35f),
                              ImVec2(p.x + 14.0f, p.y + h_line * 0.65f),
                              ui::WithAlpha(th_legend.success, 0.85f));
            ImGui::Dummy(ImVec2(16.0f, h_line));
        }
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextDisabled("wall-clock (what the user sees)");
        ImGui::SameLine(0.0f, 10.0f);
        // Render-cost swatch
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h_line = ImGui::GetTextLineHeight();
            dl->AddLine(ImVec2(p.x, p.y + h_line * 0.5f),
                        ImVec2(p.x + 14.0f, p.y + h_line * 0.5f),
                        ui::WithAlpha(th_legend.text_secondary, 0.95f),
                        1.4f);
            ImGui::Dummy(ImVec2(16.0f, h_line));
        }
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextDisabled("render cost (CPU work this frame)");
    }
    DrawFrameTimeGraph(h, avg_ms, spikes.threshold_ms, 130.0f);

    ImGui::Spacing();

    // =================================================================
    // Section 3: Subsystem stacked bar (this frame)
    // =================================================================
    const auto& samples = s_snapshot.samples;
    std::vector<core::perf::FrameSample> sorted(samples.begin(), samples.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const core::perf::FrameSample& a, const core::perf::FrameSample& b) {
                  return a.total_ns > b.total_ns;
              });

    {
        const auto groups = GroupBySubsystem(samples);
        ImGui::Text("This frame: %.2f ms render across %zu buckets", render_ms, samples.size());
        DrawSubsystemStackedBar(groups, total_ns, 28.0f);
    }

    ImGui::Spacing();

    // =================================================================
    // Section 4: Bucket tree
    // =================================================================
    if (ImGui::CollapsingHeader("Buckets (sorted by cost)", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool enabled = core::perf::IsEnabled();
        if (ImGui::Checkbox("Sampling enabled", &enabled))
            core::perf::SetEnabled(enabled);

        BucketNode tree = BuildBucketTree(samples);
        // Render top-level children of the synthetic root, sorted desc.
        std::vector<const BucketNode*> top;
        top.reserve(tree.children.size());
        for (const auto& kv : tree.children)
            top.push_back(&kv.second);
        std::sort(top.begin(), top.end(),
                  [](const BucketNode* a, const BucketNode* b) { return a->total_ns > b->total_ns; });
        for (const BucketNode* n : top)
            DrawTreeNodeRow(*n, total_ns, 1);

        if (ImGui::CollapsingHeader("Raw bucket table")) {
            if (ImGui::BeginTable("perf_buckets_raw",
                                  4,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Bucket", ImGuiTableColumnFlags_WidthStretch, 0.50f);
                ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthStretch, 0.18f);
                ImGui::TableSetupColumn("% Frame", ImGuiTableColumnFlags_WidthStretch, 0.14f);
                ImGui::TableSetupColumn("Hits", ImGuiTableColumnFlags_WidthStretch, 0.18f);
                ImGui::TableHeadersRow();
                for (const auto& s : sorted) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(s.name ? s.name : "(null)");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.3f", static_cast<float>(s.total_ns) / 1.0e6f);
                    ImGui::TableSetColumnIndex(2);
                    const float pct = total_ns > 0
                        ? 100.0f * static_cast<float>(s.total_ns) / static_cast<float>(total_ns)
                        : 0.0f;
                    ImGui::Text("%.1f%%", pct);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%u", s.hit_count);
                }
                ImGui::EndTable();
            }
        }
    }

    // =================================================================
    // Section 5: Canvas render stats as chips
    // =================================================================
    if (ImGui::CollapsingHeader("Canvas render stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        const ui::gsn::CanvasRenderStats& stats = s_snapshot.canvas_stats;

        DrawCullChip("Nodes", stats.nodes_drawn, stats.nodes_culled);
        ImGui::SameLine(0.0f, 6.0f);
        DrawCullChip("Edges", stats.edges_drawn, stats.edges_culled);

        ImGui::Spacing();
        DrawCounterChip("Shadows", stats.shadows_drawn);
        ImGui::SameLine(0.0f, 6.0f);
        DrawCounterChip("Shading", stats.interior_shading_drawn);
        ImGui::SameLine(0.0f, 6.0f);
        DrawCounterChip("Glow", stats.selection_glow_drawn);
        ImGui::SameLine(0.0f, 6.0f);
        DrawCounterChip("ACP", stats.acp_decorators_drawn);
        ImGui::SameLine(0.0f, 6.0f);
        DrawCounterChip("Terms", stats.terminology_spans_drawn);
        ImGui::SameLine(0.0f, 6.0f);
        DrawCounterChip("Clips", stats.clip_rect_pushes);

        ImGui::Spacing();
        DrawCounterChip("vtx", stats.draw_list_vtx);
        ImGui::SameLine(0.0f, 6.0f);
        DrawCounterChip("idx", stats.draw_list_idx);
        ImGui::SameLine(0.0f, 6.0f);
        DrawCounterChip("cmds", stats.draw_list_cmds);
    }

    // =================================================================
    // Section 6: Feature toggles + actions
    // =================================================================
    if (ImGui::CollapsingHeader("Feature toggles (A/B) & report", ImGuiTreeNodeFlags_DefaultOpen)) {
        core::perf::PerfToggles& t = core::perf::GetPerfToggles();
        ImGui::TextWrapped("Disable individual cost contributors to measure their impact on FPS. "
                           "Defaults match production behaviour.");
        ImGui::Checkbox("Node drop shadows", &t.node_shadows);
        ImGui::SameLine();
        ImGui::Checkbox("Interior shading", &t.node_interior_shading);
        ImGui::SameLine();
        ImGui::Checkbox("Selection glow", &t.selection_glow);
        ImGui::Checkbox("Terminology spans", &t.terminology_spans);
        ImGui::SameLine();
        ImGui::Checkbox("ACP decorators", &t.acp_decorators);
        ImGui::SameLine();
        ImGui::Checkbox("High-segment circles", &t.high_segment_circles);
        ImGui::Checkbox("Freeze ACP rebuilds (not yet wired)", &t.freeze_acp_builds);

        ImGui::Separator();

        static std::string last_saved_path;
        if (ImGui::Button("Copy report to clipboard")) {
            const ui::gsn::CanvasRenderStats& stats = s_snapshot.canvas_stats;
            const std::string report = BuildReport(total_ms, avg_ms, max_ms, fps, sorted, total_ns, stats,
                                                   core::perf::GetPerfToggles());
            ImGui::SetClipboardText(report.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Save report to file")) {
            const ui::gsn::CanvasRenderStats& stats = s_snapshot.canvas_stats;
            const std::string report = BuildReport(total_ms, avg_ms, max_ms, fps, sorted, total_ns, stats,
                                                   core::perf::GetPerfToggles());
            std::time_t now = std::time(nullptr);
            std::tm tm_buf{};
#if defined(_WIN32)
            localtime_s(&tm_buf, &now);
#else
            localtime_r(&now, &tm_buf);
#endif
            char name[64];
            std::strftime(name, sizeof(name), "perf-report-%Y%m%d-%H%M%S.txt", &tm_buf);
            std::filesystem::path output_dir = std::filesystem::current_path() / "build";
            std::error_code ec;
            std::filesystem::create_directories(output_dir, ec);
            if (ec || !std::filesystem::is_directory(output_dir))
                output_dir = std::filesystem::current_path();
            std::filesystem::path path = output_dir / name;
            std::ofstream f(path);
            if (f) {
                f << report;
                last_saved_path = path.string();
            } else {
                last_saved_path = "(failed to write)";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset toggles")) {
            t = core::perf::PerfToggles{};
        }
        if (!last_saved_path.empty()) {
            ImGui::TextDisabled("Saved: %s", last_saved_path.c_str());
        }
    }

    ImGui::End();
}

} // namespace app::areas
