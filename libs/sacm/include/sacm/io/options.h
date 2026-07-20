#pragma once

#include "sacm/metadata/namespaces.h"
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
    // Namespace URI to export under. Empty means the pinned default.
    //
    // SACM 2.3 determines no instance-document namespace (the normative MOF
    // model declares no nsURI), so the pin is a project choice and every other
    // producer has invented a different one. A caller that must be read by a
    // specific tool can name that tool's URI here; feeding back
    // `LoadResult::source_namespace` re-exports a document under the namespace
    // it arrived with (decision #18).
    //
    // Scope: this rebinds the single SACM namespace. It does **not** reproduce
    // the EMF dialect, which declares one namespace per metamodel package
    // (`http://omg.sacm/2.3/{base,argumentation,...}`); emitting that form is a
    // larger change to the writer and is not supported. The prefix stays
    // `sacm` — it is arbitrary in XML and only the URI carries meaning.
    std::string namespace_uri;
};

struct LoadResult {
    std::optional<model::Document> document;
    std::vector<validation::Diagnostic> diagnostics;
    // Namespace URI the source document used (decision #18: the import
    // choice is remembered with the document/session).
    std::string source_namespace;
    // SACM revision the source declared, derived from that namespace. The
    // library implements 2.3; anything else loads in tolerant mode and is
    // reported, so that "it parsed" is never mistaken for "it is 2.3".
    metadata::namespaces::StandardVersion source_version =
        metadata::namespaces::StandardVersion::Unknown;
    bool ok = false;
};

struct SaveResult {
    std::string xml;
    std::vector<validation::Diagnostic> diagnostics;
    bool ok = false;
};

}  // namespace sacm::io
