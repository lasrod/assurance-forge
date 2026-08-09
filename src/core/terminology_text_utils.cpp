#include "core/terminology_text_utils.h"

#include "core/string_utils.h"
#include "legacy_sacm/sacm_model.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace core {
namespace {

std::vector<std::string> SplitCategoryRefsInternal(const std::string& raw, bool normalize_refs) {
    std::string normalized = raw;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::stringstream stream(normalized);
    std::vector<std::string> refs;
    std::string item;
    while (stream >> item) {
        item = normalize_refs ? NormalizeRef(item) : TrimWhitespace(item);
        if (!item.empty() && std::find(refs.begin(), refs.end(), item) == refs.end())
            refs.push_back(std::move(item));
    }
    return refs;
}

} // namespace

std::string JoinCategoryRefs(const std::vector<std::string>& refs) {
    std::string result;
    for (const auto& ref : refs) {
        if (ref.empty())
            continue;
        if (!result.empty())
            result += ", ";
        result += ref;
    }
    return result;
}

std::vector<std::string> SplitCategoryRefs(const std::string& raw) {
    return SplitCategoryRefsInternal(raw, false);
}

std::vector<std::string> SplitNormalizedCategoryRefs(const std::string& raw) {
    return SplitCategoryRefsInternal(raw, true);
}

std::string TermContextDisplayLabel(const sacm::Term& term) {
    if (term.value.empty())
        return term.name.empty() ? term.id : term.name;
    if (term.name.empty() || term.name == term.value)
        return term.value;
    return term.value + ": " + term.name;
}

} // namespace core