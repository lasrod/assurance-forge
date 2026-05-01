#pragma once

#include "core/problems/problem_item.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace core {

bool ShouldHighlightProblemAttention(const ProblemItem& problem);
std::unordered_set<std::string> CollectAttentionElementIds(const std::vector<ProblemItem>& problems);

}  // namespace core