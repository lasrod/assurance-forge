#include "core/perf/frame_profiler.h"

#include <chrono>
#include <unordered_map>
#include <vector>

namespace core::perf {

namespace {

using Clock = std::chrono::steady_clock;

struct ScopeFrame {
    const char* name;
    Clock::time_point start;
    std::uint64_t children_ns; // ns consumed by nested scopes (exclusive timing)
};

struct State {
    bool enabled = true;
    std::vector<FrameSample> current;
    std::vector<FrameSample> last;
    std::unordered_map<const char*, std::size_t> name_to_index; // pointer-keyed (string literals)
    std::vector<ScopeFrame> stack;
    Clock::time_point frame_start{};
    std::uint64_t last_frame_total_ns = 0;
    PerfToggles toggles{};
};

State& GetState() {
    static State s;
    return s;
}

std::size_t FindOrCreateBucket(State& s, const char* name) {
    auto it = s.name_to_index.find(name);
    if (it != s.name_to_index.end())
        return it->second;
    std::size_t index = s.current.size();
    s.current.push_back(FrameSample{name, 0, 0});
    s.name_to_index.emplace(name, index);
    return index;
}

} // namespace

void BeginFrame() {
    State& s = GetState();
    for (FrameSample& sample : s.current) {
        sample.total_ns = 0;
        sample.hit_count = 0;
    }
    s.stack.clear();
    s.frame_start = Clock::now();
}

void EndFrame() {
    State& s = GetState();
    auto now = Clock::now();
    s.last_frame_total_ns =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - s.frame_start).count());
    s.last = s.current;
}

const std::vector<FrameSample>& GetLastFrameSamples() {
    return GetState().last;
}

std::uint64_t GetLastFrameTotalNs() {
    return GetState().last_frame_total_ns;
}

bool IsEnabled() {
    return GetState().enabled;
}

void SetEnabled(bool enabled) {
    GetState().enabled = enabled;
}

void PushScope(const char* name) {
    State& s = GetState();
    s.stack.push_back(ScopeFrame{name, Clock::now(), 0});
}

void PopScope() {
    State& s = GetState();
    if (s.stack.empty())
        return;
    ScopeFrame frame = s.stack.back();
    s.stack.pop_back();
    auto now = Clock::now();
    std::uint64_t total_ns =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - frame.start).count());
    // Record exclusive time (total minus time spent in nested scopes) so the
    // bucket sum approximates frame time without double counting.
    std::uint64_t exclusive_ns = total_ns > frame.children_ns ? total_ns - frame.children_ns : 0;
    std::size_t index = FindOrCreateBucket(s, frame.name);
    FrameSample& sample = s.current[index];
    sample.total_ns += exclusive_ns;
    sample.hit_count += 1;
    if (!s.stack.empty()) {
        s.stack.back().children_ns += total_ns;
    }
}

PerfToggles& GetPerfToggles() {
    return GetState().toggles;
}

} // namespace core::perf
