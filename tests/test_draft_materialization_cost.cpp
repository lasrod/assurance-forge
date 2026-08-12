#include "core/drafts/draft_workspace_store.h"

#include "core/reviews/review_proposal.h"

#include <gtest/gtest.h>

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

// Measures what the integrated-draft-workspace plan (§5, §19) requires to be
// measured before trusting the frame path: materialization reruns whenever the
// workspace revision or the accepted model changes, everything on screen reads
// its result, and the revision-keyed cache is the only mitigation. This is the
// Phase-2 exit criterion carried in #283 -- "measured on a large argument, and
// the number recorded".
//
// This file is a measurement harness, not a gate. It asserts correctness
// (materialization succeeds, the working model has the right shape, the cached
// path is effectively free) but deliberately does not assert a wall-clock
// budget, because a loaded CI runner would turn a healthy number into a flaky
// red. The numbers are printed and recorded as test properties; the recorded
// baseline lives in #283.

namespace {

struct TempDir {
    std::filesystem::path path;
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::filesystem::path UniqueTempPath(const std::string& stem) {
    static int counter = 0;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("af_mat_cost_" + stem + "_" + std::to_string(++counter));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

core::SacmElement Claim(const std::string& id, const std::string& text, bool undeveloped) {
    core::SacmElement element;
    element.id = id;
    element.type = "claim";
    element.name = id;
    element.content = text;
    element.undeveloped = undeveloped;
    return element;
}

core::SacmElement Supports(const std::string& id, const std::string& child, const std::string& parent) {
    core::SacmElement element;
    element.id = id;
    element.type = "assertedinference";
    element.source_refs = {child};
    element.target_refs = {parent};
    return element;
}

// A claim tree with fan-out five. Leaves are marked undeveloped so the model
// is well-formed the way a real in-progress case is: a baseline that trips
// thousands of EV.1 findings would make this measure findings assembly on a
// pathological argument rather than materialization on a realistic one. The
// wording avoids the qualifiers CL.5 names for the same reason.
core::AssuranceCase LargeCase(int claim_count) {
    core::AssuranceCase model;
    model.id = "case-large";
    model.name = "Large";
    const int last_parent_index = claim_count >= 2 ? (claim_count - 2) / 5 + 1 : 1;
    model.elements.reserve(static_cast<std::size_t>(claim_count) * 2);
    model.elements.push_back(Claim("C1", "The system meets its stated performance targets.", claim_count == 1));
    for (int index = 2; index <= claim_count; ++index) {
        const std::string id = "C" + std::to_string(index);
        const std::string parent = "C" + std::to_string((index - 2) / 5 + 1);
        const bool is_leaf = index > last_parent_index;
        model.elements.push_back(
            Claim(id, "Subsystem behaviour item " + std::to_string(index) + " meets its stated target.", is_leaf));
        model.elements.push_back(Supports("R" + std::to_string(index), id, parent));
    }
    return model;
}

core::reviews::PatchOperation CreateClaimOp(const std::string& create_ref, const std::string& text) {
    core::reviews::PatchOperation operation;
    operation.type = core::reviews::PatchOperationType::CreateClaim;
    operation.create_ref = create_ref;
    operation.text = text;
    return operation;
}

core::reviews::PatchOperation SupportOp(const std::string& child_ref, const std::string& parent_id) {
    core::reviews::PatchOperation operation;
    operation.type = core::reviews::PatchOperationType::AddSupportedBy;
    core::reviews::ElementRef source;
    source.create_ref = child_ref;
    core::reviews::ElementRef target;
    target.existing_id = parent_id;
    operation.source = source;
    operation.target = target;
    return operation;
}

double MedianMilliseconds(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

struct CostReport {
    int accepted_elements = 0;
    double cold_ms = 0.0;
    double cached_ms = 0.0;
    double stage_ms = 0.0;
};

CostReport MeasureAt(int claim_count, int group_count, int claims_per_group, int runs) {
    TempDir dir{UniqueTempPath(std::to_string(claim_count))};
    core::AssuranceCase accepted = LargeCase(claim_count);

    core::drafts::DraftWorkspaceStore store;
    store.SetProjectRoot(dir.path);
    const std::filesystem::path argument_file = dir.path / "arguments" / "main.sacm";
    std::filesystem::create_directories(argument_file.parent_path());
    std::string error;
    EXPECT_TRUE(store.Open(argument_file, accepted, error)) << error;

    // The draft: several groups, each adding developed-elsewhere claims under
    // parents spread across the tree, the shape an MCP conversation produces.
    core::drafts::DraftGroupRequest request;
    request.source = core::drafts::DraftSource::Mcp;
    request.source_label = "Claude Code";
    int created = 0;
    for (int group = 0; group < group_count; ++group) {
        request.title = "Draft group " + std::to_string(group + 1);
        const std::string group_id = store.BeginGroup(request, accepted, error);
        EXPECT_FALSE(group_id.empty()) << error;
        std::vector<core::reviews::PatchOperation> operations;
        for (int item = 0; item < claims_per_group; ++item) {
            ++created;
            const std::string ref = "$draft-" + std::to_string(created);
            const std::string parent = "C" + std::to_string(1 + (created * 7) % std::max(claim_count / 5, 1));
            operations.push_back(
                CreateClaimOp(ref, "Draft claim " + std::to_string(created) + " states one further obligation."));
            operations.push_back(SupportOp(ref, parent));
        }
        EXPECT_TRUE(store.StageOperations(group_id, operations, accepted, error)) << error;
    }

    constexpr std::uint64_t kAcceptedRevision = 1;

    // Cold: the cost paid on every staging call and accepted-model change.
    std::vector<double> cold;
    for (int run = 0; run < runs; ++run) {
        store.InvalidateMaterialization();
        const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
        const core::drafts::DraftMaterializationResult& result = store.Materialize(accepted, kAcceptedRevision);
        const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        EXPECT_TRUE(result.success) << result.error;
        EXPECT_EQ(result.working_model.elements.size(),
                  accepted.elements.size() + static_cast<std::size_t>(created) * 2);
        cold.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
    }

    // Cached: the per-frame path -- the revision keys match, nothing recomputes.
    std::vector<double> cached;
    for (int run = 0; run < runs; ++run) {
        const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
        const core::drafts::DraftMaterializationResult& result = store.Materialize(accepted, kAcceptedRevision);
        const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        EXPECT_TRUE(result.success);
        cached.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
    }

    // One further staging call: what an MCP client waits for per stage_operations.
    request.title = "Timing probe group";
    const std::string probe_group = store.BeginGroup(request, accepted, error);
    EXPECT_FALSE(probe_group.empty()) << error;
    std::vector<core::reviews::PatchOperation> probe_operations;
    probe_operations.push_back(CreateClaimOp("$probe-1", "One further obligation is stated for the probe."));
    probe_operations.push_back(SupportOp("$probe-1", "C1"));
    const std::chrono::steady_clock::time_point stage_begin = std::chrono::steady_clock::now();
    EXPECT_TRUE(store.StageOperations(probe_group, probe_operations, accepted, error)) << error;
    const std::chrono::steady_clock::time_point stage_end = std::chrono::steady_clock::now();

    CostReport report;
    report.accepted_elements = static_cast<int>(accepted.elements.size());
    report.cold_ms = MedianMilliseconds(cold);
    report.cached_ms = MedianMilliseconds(cached);
    report.stage_ms = std::chrono::duration<double, std::milli>(stage_end - stage_begin).count();
    return report;
}

} // namespace

TEST(DraftMaterializationCost, MeasuredOnLargeArguments) {
    const int kGroups = 10;
    const int kClaimsPerGroup = 10;

#ifdef NDEBUG
    // The recorded baseline (see #283) comes from Release runs of these sizes.
    const std::vector<int> kSizes{250, 1000, 2500};
    const int kRuns = 5;
#else
    // CI builds Debug on every platform, where a cold materialization is
    // several times slower; the full sweep would add minutes to every run for
    // numbers nobody records. One small size keeps the harness itself from
    // rotting unnoticed.
    const std::vector<int> kSizes{250};
    const int kRuns = 3;
#endif

    for (const int claim_count : kSizes) {
        const CostReport report = MeasureAt(claim_count, kGroups, kClaimsPerGroup, kRuns);

        std::printf("[materialization-cost] accepted=%d elements, draft=%d groups x %d claims: "
                    "cold median %.2f ms, cached median %.4f ms, one staging call %.2f ms\n",
                    report.accepted_elements,
                    kGroups,
                    kClaimsPerGroup,
                    report.cold_ms,
                    report.cached_ms,
                    report.stage_ms);
        RecordProperty("accepted_elements_" + std::to_string(claim_count), report.accepted_elements);
        RecordProperty("cold_ms_" + std::to_string(claim_count), std::to_string(report.cold_ms));
        RecordProperty("cached_ms_" + std::to_string(claim_count), std::to_string(report.cached_ms));
        RecordProperty("stage_ms_" + std::to_string(claim_count), std::to_string(report.stage_ms));

        // The one hard requirement: the per-frame path must be a cache lookup,
        // not a recomputation. A millisecond here would already be a tenth of a
        // 60fps frame spent on nothing, and it does not vary with load the way
        // the cold path legitimately does.
        EXPECT_LT(report.cached_ms, 1.0) << "the cached materialization path recomputed something at "
                                         << report.accepted_elements << " elements";
    }
}
