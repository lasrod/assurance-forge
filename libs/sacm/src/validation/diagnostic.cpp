#include "sacm/validation/diagnostic.h"

#include <algorithm>

namespace sacm::validation {

std::string_view severity_name(Severity severity) {
    switch (severity) {
    case Severity::Info:
        return "info";
    case Severity::Warning:
        return "warning";
    case Severity::Error:
        return "error";
    }
    return "unknown";
}

bool has_errors(std::span<const Diagnostic> diagnostics) {
    return std::ranges::any_of(diagnostics,
                               [](const Diagnostic& diagnostic) { return diagnostic.severity == Severity::Error; });
}

} // namespace sacm::validation
