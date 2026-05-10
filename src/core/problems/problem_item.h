#pragma once

#include <string>

namespace core {

enum class ProblemSeverity {
    Info,
    Warning,
    Error,
};

enum class ProblemSource {
    Manual,
    ReviewComment,
    ModelValidation,
    GuidelineReview,
    AIReview,
    ImportExport,
};

struct ProblemItem {
    std::string id;
    ProblemSeverity severity = ProblemSeverity::Info;
    ProblemSource source = ProblemSource::Manual;
    std::string element_id;
    std::string type;
    std::string message;
    std::string guideline_id;
    std::string quick_fix_label;
    std::string quick_fix_payload;
};

const char* ToString(ProblemSeverity severity);
const char* ToString(ProblemSource source);

} // namespace core
