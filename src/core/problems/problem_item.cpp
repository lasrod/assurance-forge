#include "core/problems/problem_item.h"

namespace core {

const char* ToString(ProblemSeverity severity) {
    switch (severity) {
        case ProblemSeverity::Info: return "Info";
        case ProblemSeverity::Warning: return "Warning";
        case ProblemSeverity::Error: return "Error";
    }
    return "Unknown";
}

const char* ToString(ProblemSource source) {
    switch (source) {
        case ProblemSource::Manual: return "Manual";
        case ProblemSource::ModelValidation: return "ModelValidation";
        case ProblemSource::GuidelineReview: return "GuidelineReview";
        case ProblemSource::AIReview: return "AIReview";
        case ProblemSource::ImportExport: return "ImportExport";
    }
    return "Unknown";
}

}  // namespace core
