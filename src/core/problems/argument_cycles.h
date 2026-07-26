#pragma once

#include "core/sacm_model.h"

#include <string>
#include <vector>

namespace core {

// A cycle in the argument's support structure: G1 is supported by G2, which is
// supported by G1. Such an argument proves nothing, and it is the most damaging
// structural defect a safety case can carry because nothing about it *looks*
// wrong — every node has support, so it reads as complete.
//
// `element_ids` lists the cycle in traversal order. The edge closing the loop
// runs from the last id back to the first; the first is not repeated. A
// single-element vector is an element that supports itself.
struct ArgumentCycle {
    std::vector<std::string> element_ids;
};

// Finds every distinct cycle reachable in the support graph.
//
// Only *support* edges are followed:
//   * `assertedinference` — the SACM source is the premise and the target the
//     conclusion, so support runs target -> source. A `reasoning` strategy is
//     included as an intermediate node so the cycle reads in GSN terms.
//   * `assertedevidence` — target -> source.
//
// `assertedcontext` is excluded because InContextOf is not support, and
// counter relationships (`is_counter`) are excluded because a GSN v3 challenge
// is not support either — following one would report every dialectic argument
// as circular.
//
// Cycles are deduplicated by rotation, so the same loop found from different
// entry points is reported once. Ordering is deterministic.
std::vector<ArgumentCycle> FindSupportCycles(const parser::AssuranceCase& model);

} // namespace core
