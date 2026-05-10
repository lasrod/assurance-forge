#pragma once

#include <string>
#include <vector>

namespace sacm {
struct Term;
}

namespace core {

std::string JoinCategoryRefs(const std::vector<std::string>& refs);
std::vector<std::string> SplitCategoryRefs(const std::string& raw);
std::vector<std::string> SplitNormalizedCategoryRefs(const std::string& raw);
std::string TermContextDisplayLabel(const sacm::Term& term);

} // namespace core