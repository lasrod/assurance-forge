#pragma once

#include "sacm/io/options.h"

#include <filesystem>
#include <string_view>

namespace sacm::io {

// Loads a SACM 2.3 XMI document. Accepts a bare interchange-package root
// (AssuranceCasePackage/ArgumentPackage/ArtifactPackage/TerminologyPackage)
// or an xmi:XMI wrapper. Element typing is resolved via xsi:type, then the
// parent's containment-role name, then (tolerant) the concrete class name.
// DOCTYPE declarations are always rejected (SACM-SEC-001).
LoadResult load_xmi_file(const std::filesystem::path& path, const LoadOptions& options = {});
LoadResult load_xmi_string(std::string_view xml, const LoadOptions& options = {});

// Serializes the document. Strict mode (default) emits the pinned SACM 2.3
// namespaces with deterministic, byte-stable output (fixed attribute and
// child order, two-space indent, LF newlines, UTF-8). Compatibility-only
// content makes a strict save fail (SACM-XMI-006).
SaveResult save_xmi_string(const model::Document& document, const SaveOptions& options = {});
SaveResult save_xmi_file(const std::filesystem::path& path, const model::Document& document,
                         const SaveOptions& options = {});

}  // namespace sacm::io
