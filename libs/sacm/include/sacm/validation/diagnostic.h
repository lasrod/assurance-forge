#pragma once

#include "sacm/model/element_id.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sacm::validation {

enum class Severity {
    Info,
    Warning,
    Error,
};

std::string_view severity_name(Severity severity);

// Position in a loaded XMI document, when known.
struct SourceLocation {
    std::string file;
    int line = 0;
    int column = 0;
};

// Machine-readable diagnostic. `code` values are stable API and catalogued
// in docs/sacm/sacm-diagnostics-catalog.md; `requirement_id` names the
// conformance-matrix row the diagnostic supports.
struct Diagnostic {
    std::string code;
    Severity severity = Severity::Error;
    std::string requirement_id;
    std::string operation;
    std::vector<model::ElementId> affected;
    std::optional<SourceLocation> location;
    std::string message;
};

bool has_errors(std::span<const Diagnostic> diagnostics);

} // namespace sacm::validation
