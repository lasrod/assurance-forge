#include "app/areas/perf_overlay_area.h"

#include "core/perf/frame_profiler.h"
#include "ui/gsn/gsn_canvas_renderer.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <string>
#include <vector>

namespace app::areas {

namespace {

constexpr int kFrameTimeHistorySize = 240;

struct FrameTimeHistory {
    float samples_ms[kFrameTimeHistorySize] = {};
    int next_index = 0;
    int filled = 0;
};

FrameTimeHistory& History() {
    static FrameTimeHistory h;
    return h;
}

void PushFrameTime(float ms) {
    FrameTimeHistory& h = History();
    h.samples_ms[h.next_index] = ms;
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

void DrawCounter(const char* label, int value) {
    ImGui::Text("%-26s %d", label, value);
}

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

} // namespace

void RenderPerfOverlay(bool& open) {
    if (!open)
        return;

    const std::uint64_t total_ns = core::perf::GetLastFrameTotalNs();
    const float total_ms = static_cast<float>(total_ns) / 1.0e6f;
    PushFrameTime(total_ms);

    ImGui::SetNextWindowSize(ImVec2(540, 720), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Performance", &open)) {
        ImGui::End();
        return;
    }

    // --- Frame time summary ---
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::SameLine();
    ImGui::Text("  Frame: %.2f ms", total_ms);
    ImGui::SameLine();
    const FrameTimeHistory& h = History();
    ImGui::Text("  avg %.2f / max %.2f ms", Average(h), Maximum(h));

    if (ImGui::Button("Copy report to clipboard")) {
        const auto& samples = core::perf::GetLastFrameSamples();
        std::vector<core::perf::FrameSample> sorted(samples.begin(), samples.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const core::perf::FrameSample& a, const core::perf::FrameSample& b) {
                      return a.total_ns > b.total_ns;
                  });
        const ui::gsn::CanvasRenderStats stats = ui::gsn::GetLastCanvasRenderStats();
        const std::string report = BuildReport(total_ms, Average(h), Maximum(h),
                                               ImGui::GetIO().Framerate, sorted, total_ns, stats,
                                               core::perf::GetPerfToggles());
        ImGui::SetClipboardText(report.c_str());
    }
    ImGui::SameLine();
    static std::string last_saved_path;
    if (ImGui::Button("Save report to file")) {
        const auto& samples = core::perf::GetLastFrameSamples();
        std::vector<core::perf::FrameSample> sorted(samples.begin(), samples.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const core::perf::FrameSample& a, const core::perf::FrameSample& b) {
                      return a.total_ns > b.total_ns;
                  });
        const ui::gsn::CanvasRenderStats stats = ui::gsn::GetLastCanvasRenderStats();
        const std::string report = BuildReport(total_ms, Average(h), Maximum(h),
                                               ImGui::GetIO().Framerate, sorted, total_ns, stats,
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
        std::filesystem::path path = std::filesystem::current_path() / name;
        std::ofstream f(path);
        if (f) {
            f << report;
            last_saved_path = path.string();
        } else {
            last_saved_path = "(failed to write)";
        }
    }
    ImGui::SameLine();
    if (!last_saved_path.empty()) {
        ImGui::TextDisabled("Saved: %s", last_saved_path.c_str());
    } else {
        ImGui::TextDisabled("(copy or save the snapshot)");
    }

    // Plot the rolling frame-time history. PlotLines needs a contiguous
    // chronological buffer, so we copy starting at the oldest sample.
    {
        std::vector<float> ordered;
        ordered.reserve(h.filled);
        const int start = (h.next_index + kFrameTimeHistorySize - h.filled) % kFrameTimeHistorySize;
        for (int i = 0; i < h.filled; ++i)
            ordered.push_back(h.samples_ms[(start + i) % kFrameTimeHistorySize]);
        char overlay[32];
        std::snprintf(overlay, sizeof(overlay), "%.2f ms", total_ms);
        ImGui::PlotLines("##frame_time",
                         ordered.empty() ? nullptr : ordered.data(),
                         static_cast<int>(ordered.size()),
                         0,
                         overlay,
                         0.0f,
                         FLT_MAX,
                         ImVec2(-FLT_MIN, 60.0f));
    }

    // --- Profiler buckets ---
    if (ImGui::CollapsingHeader("Profiler buckets", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool enabled = core::perf::IsEnabled();
        if (ImGui::Checkbox("Sampling enabled", &enabled))
            core::perf::SetEnabled(enabled);

        const auto& samples = core::perf::GetLastFrameSamples();
        std::vector<core::perf::FrameSample> sorted(samples.begin(), samples.end());
        std::sort(sorted.begin(), sorted.end(), [](const core::perf::FrameSample& a, const core::perf::FrameSample& b) {
            return a.total_ns > b.total_ns;
        });

        if (ImGui::BeginTable("perf_buckets",
                              4,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
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
                const float pct =
                    total_ns > 0 ? 100.0f * static_cast<float>(s.total_ns) / static_cast<float>(total_ns) : 0.0f;
                ImGui::Text("%.1f%%", pct);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%u", s.hit_count);
            }
            ImGui::EndTable();
        }
    }

    // --- Canvas render stats ---
    if (ImGui::CollapsingHeader("Canvas render stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        const ui::gsn::CanvasRenderStats stats = ui::gsn::GetLastCanvasRenderStats();
        DrawCounter("Nodes drawn", stats.nodes_drawn);
        DrawCounter("Nodes culled", stats.nodes_culled);
        DrawCounter("Edges drawn", stats.edges_drawn);
        DrawCounter("Edges culled", stats.edges_culled);
        DrawCounter("Shadows drawn", stats.shadows_drawn);
        DrawCounter("Interior shading drawn", stats.interior_shading_drawn);
        DrawCounter("Selection glows drawn", stats.selection_glow_drawn);
        DrawCounter("ACP decorators drawn", stats.acp_decorators_drawn);
        DrawCounter("Terminology spans drawn", stats.terminology_spans_drawn);
        DrawCounter("Clip-rect pushes", stats.clip_rect_pushes);
        ImGui::Separator();
        DrawCounter("Draw list vertices", stats.draw_list_vtx);
        DrawCounter("Draw list indices", stats.draw_list_idx);
        DrawCounter("Draw list commands", stats.draw_list_cmds);
    }

    // --- Feature toggles (Phase 4) ---
    if (ImGui::CollapsingHeader("Feature toggles (A/B)", ImGuiTreeNodeFlags_DefaultOpen)) {
        core::perf::PerfToggles& t = core::perf::GetPerfToggles();
        ImGui::TextWrapped("Disable individual cost contributors to measure their impact on FPS. "
                           "Defaults match production behaviour.");
        ImGui::Checkbox("Node drop shadows", &t.node_shadows);
        ImGui::Checkbox("Node interior shading", &t.node_interior_shading);
        ImGui::Checkbox("Selection glow rings", &t.selection_glow);
        ImGui::Checkbox("Terminology spans", &t.terminology_spans);
        ImGui::Checkbox("ACP decorators", &t.acp_decorators);
        ImGui::Checkbox("High-segment circles", &t.high_segment_circles);
        ImGui::Checkbox("Freeze ACP rebuilds (not yet wired)", &t.freeze_acp_builds);
        if (ImGui::Button("Reset toggles")) {
            t = core::perf::PerfToggles{};
        }
    }

    ImGui::End();
}

} // namespace app::areas
