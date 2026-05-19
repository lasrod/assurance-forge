#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace core::perf {

// Lightweight per-frame CPU profiler. Single-threaded (matches the ImGui
// rendering model). Uses QueryPerformanceCounter (Windows) for sub-microsecond
// resolution. Runtime-toggleable; samples cost ~30-80 ns each when enabled.
struct FrameSample {
    const char* name; // pointer to a string literal / persistent string
    std::uint64_t total_ns;
    std::uint32_t hit_count;
};

// Begin a new profiling frame. Clears the per-frame accumulators and starts
// the frame timer used for the "Frame" total bucket.
void BeginFrame();

// End the current profiling frame. Snapshots the accumulated samples to the
// "last frame" buffer that `GetLastFrameSamples` returns.
void EndFrame();

// Returns the samples captured during the most recently completed frame.
// Order is insertion order (the order in which `ScopedTimer` first observed
// each bucket name across this run). Safe to call any time.
const std::vector<FrameSample>& GetLastFrameSamples();

// Total wall-clock nanoseconds of the most recent frame between
// BeginFrame() and EndFrame().
std::uint64_t GetLastFrameTotalNs();

// Whether sampling is currently active. When disabled `ScopedTimer` becomes
// a no-op (still constructs/destructs but skips the QPC call).
bool IsEnabled();
void SetEnabled(bool enabled);

// Push/Pop a scoped timer. Prefer the RAII `ScopedTimer` below.
void PushScope(const char* name);
void PopScope();

class ScopedTimer {
public:
    explicit ScopedTimer(const char* name) : active_(IsEnabled()) {
        if (active_)
            PushScope(name);
    }
    ~ScopedTimer() {
        if (active_)
            PopScope();
    }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    bool active_;
};

// Runtime feature toggles used by Phase 4 of the perf-analysis plan. These
// let the user A/B individual cost contributors from the perf overlay without
// rebuilding. Defaults match current behaviour (all features enabled).
struct PerfToggles {
    bool node_shadows = true;
    bool node_interior_shading = true;
    bool selection_glow = true;
    bool terminology_spans = true;
    bool acp_decorators = true;
    bool high_segment_circles = true;
    bool freeze_acp_builds = false; // when true, reuse last frame's ACP indices
};

PerfToggles& GetPerfToggles();

} // namespace core::perf

// Convenience macro for callers that prefer line-token uniqueness.
#define ASF_PERF_SCOPE(name) ::core::perf::ScopedTimer _asf_perf_scope_##__LINE__(name)
