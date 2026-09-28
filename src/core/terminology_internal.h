#pragma once

#include "core/terminology_package_service.h"
#include "legacy_sacm/sacm_model.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace core::detail {

// Shared anon-namespace-style helpers used across the terminology_*_service.cpp split.
// Kept in a private header (`core::detail`) so each sibling translation unit can call
// them without duplicating the definitions.

std::unordered_set<std::string> CollectElementIds(const sacm::AssuranceCasePackage& package);
std::unordered_set<std::string> CollectGids(const sacm::AssuranceCasePackage& package);

std::string GenerateUniqueId(const sacm::AssuranceCasePackage& package, const std::string& prefix);
std::string GenerateUniqueGid(const sacm::AssuranceCasePackage& package, const std::string& id);

std::vector<std::string> NormalizeCategoryRefs(const std::vector<std::string>& refs);

void ApplyTermDraft(sacm::Term& term, const TerminologyTermDraft& draft);
void ApplyCategoryDraft(sacm::Category& category, const TerminologyCategoryDraft& draft);

bool MatchesCategoryRefString(const sacm::Category& category, const std::string& raw_ref);
bool MatchesRawRef(const std::string& raw_ref, const std::string& id, const std::string& gid);

} // namespace core::detail
