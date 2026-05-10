#include "core/reviews/review_text_utils.h"

namespace core::reviews {

std::string TruncateForProblemMessage(const std::string& value, size_t limit) {
    if (value.size() <= limit)
        return value;
    return value.substr(0, limit) + "...";
}

} // namespace core::reviews