#include "app/structure_problem_sync.h"

#include "core/problems/argument_cycles.h"
#include "core/problems/problem_utils.h"
#include "ui/i18n/localization.h"

#include <string>
#include <vector>

namespace app {
namespace {

constexpr const char* kStructureProblemPrefix = "structure:";

// "G1 → S1 → G2 → G1" — the loop written out, closing back on itself so the
// reader can see it is a loop rather than a path.
std::string DescribeCycle(const core::ArgumentCycle& cycle) {
    std::string path;
    for (const std::string& element_id : cycle.element_ids) {
        if (!path.empty())
            path += " → ";
        path += element_id;
    }
    if (cycle.element_ids.size() > 1) {
        path += " → ";
        path += cycle.element_ids.front();
    }
    return path;
}

} // namespace

void SyncStructureProblems(core::ProblemsManager& problems_manager, const parser::AssuranceCase* model) {
    core::ClearProblemsByIdPrefix(problems_manager, kStructureProblemPrefix);
    if (!model)
        return;

    for (const core::ArgumentCycle& cycle : core::FindSupportCycles(*model)) {
        if (cycle.element_ids.empty())
            continue;

        core::ProblemItem problem;
        // Keyed on the cycle's own members, so the problem is stable across
        // rebuilds and disappears when the loop is broken.
        problem.id = kStructureProblemPrefix + std::string("cycle:") + DescribeCycle(cycle);
        // An argument that assumes its own conclusion establishes nothing, and
        // it looks complete while doing it. That is an error, not advice.
        problem.severity = core::ProblemSeverity::Error;
        problem.source = core::ProblemSource::ModelValidation;
        // Anchor on the first element so selecting the problem navigates into
        // the loop.
        problem.element_id = cycle.element_ids.front();
        problem.type = "CircularArgument";
        problem.message =
            cycle.element_ids.size() == 1
                ? ui::i18n::trf("{0} is supported by itself, so it establishes nothing.",
                                cycle.element_ids.front())
                : ui::i18n::trf("Circular support: {0}. An argument that assumes its own conclusion "
                                "establishes nothing.",
                                DescribeCycle(cycle));
        problem.quick_fix_label = ui::i18n::tr("Show cycle");
        problem.quick_fix_payload = cycle.element_ids.front();
        problems_manager.AddOrUpdateProblem(problem);
    }
}

} // namespace app
