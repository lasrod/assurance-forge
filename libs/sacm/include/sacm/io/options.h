#pragma once

#include "sacm/model/document.h"
#include "sacm/validation/diagnostic.h"

#include <optional>
#include <string>
#include <vector>

namespace sacm::io {

// Strict follows SACM 2.3 exactly; Tolerant additionally accepts known
// third-party/legacy shapes (element-name typing, attribute shorthands,
// namespace variants) with warnings. Strict is the default for saving
// (settled decision #10); Tolerant is the default for loading.
enum class Mode {
    Strict,
    Tolerant,
};

struct LoadOptions {
    Mode mode = Mode::Tolerant;
};

struct SaveOptions {
    Mode mode = Mode::Strict;
};

struct LoadResult {
    std::optional<model::Document> document;
    std::vector<validation::Diagnostic> diagnostics;
    // Namespace URI the source document used (decision #18: the import
    // choice is remembered with the document/session).
    std::string source_namespace;
    bool ok = false;
};

struct SaveResult {
    std::string xml;
    std::vector<validation::Diagnostic> diagnostics;
    bool ok = false;
};

}  // namespace sacm::io
