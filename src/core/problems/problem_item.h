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
};

const char* ToString(ProblemSeverity severity);
const char* ToString(ProblemSource source);

}  // namespace core
