#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Create a minimal standalone SACM library skeleton.

Usage:
  bash scripts/scaffold-sacm-library.sh [options]

Options:
  --repo PATH       Repository root. Default: current directory.
  --path PATH       Library path relative to repo root. Default: libs/sacm
  --force           Allow writing into an existing empty-looking skeleton.
  --help            Show this help.

The skeleton is intentionally small. It should be expanded only through the
SACM slice workflow: requirement matrix -> failing tests -> implementation.
It includes placeholders for the editable-first policy: commands, previews,
mutation results, diagnostics, and a CLI smoke utility.
USAGE
}

REPO_ROOT="$(pwd)"
LIB_PATH="libs/sacm"
FORCE=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --repo)
      if [ "$#" -lt 2 ]; then echo "error: --repo needs a path" >&2; exit 2; fi
      REPO_ROOT="$2"
      shift 2
      ;;
    --path)
      if [ "$#" -lt 2 ]; then echo "error: --path needs a path" >&2; exit 2; fi
      LIB_PATH="$2"
      shift 2
      ;;
    --force)
      FORCE=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

REPO_ROOT="$(cd "$REPO_ROOT" && pwd)"
TARGET="$REPO_ROOT/$LIB_PATH"

if [ -e "$TARGET" ] && [ "$FORCE" -eq 0 ]; then
  echo "error: $LIB_PATH already exists. Use --force only after reviewing the directory." >&2
  exit 1
fi

mkdir -p \
  "$TARGET/include/sacm/commands" \
  "$TARGET/include/sacm/compare" \
  "$TARGET/include/sacm/io" \
  "$TARGET/include/sacm/metadata" \
  "$TARGET/include/sacm/model" \
  "$TARGET/include/sacm/validation" \
  "$TARGET/src" \
  "$TARGET/tests" \
  "$TARGET/tools" \
  "$TARGET/cmake"

write_if_missing() {
  local path="$1"
  if [ -e "$path" ] && [ "$FORCE" -eq 0 ]; then
    return
  fi
  cat > "$path"
}

write_if_missing "$TARGET/CMakeLists.txt" <<'CMEOF'
cmake_minimum_required(VERSION 3.21)
project(sacm LANGUAGES CXX)

add_library(sacm
  src/version.cpp
)

add_library(sacm::sacm ALIAS sacm)

target_compile_features(sacm PUBLIC cxx_std_23)
target_include_directories(sacm
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Keep the public library independent of Assurance Forge application layers.
target_compile_definitions(sacm PUBLIC SACM_STANDARD_VERSION="2.3")

add_executable(sacm_cli tools/sacm_cli.cpp)
target_link_libraries(sacm_cli PRIVATE sacm::sacm)

option(SACM_BUILD_TESTS "Build SACM standalone library tests" ON)
if(SACM_BUILD_TESTS)
  enable_testing()
  add_executable(sacm_smoke_tests tests/test_smoke.cpp)
  target_link_libraries(sacm_smoke_tests PRIVATE sacm::sacm)
  add_test(NAME sacm_smoke_tests COMMAND sacm_smoke_tests)
endif()
CMEOF

write_if_missing "$TARGET/include/sacm/version.h" <<'CMEOF'
#pragma once

#include <string_view>

namespace sacm {

std::string_view library_version();
std::string_view standard_version();

}  // namespace sacm
CMEOF

write_if_missing "$TARGET/src/version.cpp" <<'CMEOF'
#include "sacm/version.h"

namespace sacm {

std::string_view library_version() {
    return "0.1.0-dev";
}

std::string_view standard_version() {
    return "2.3";
}

}  // namespace sacm
CMEOF

write_if_missing "$TARGET/include/sacm/model/element_id.h" <<'CMEOF'
#pragma once

#include <string>
#include <utility>

namespace sacm::model {

class ElementId {
public:
    ElementId() = default;
    explicit ElementId(std::string value) : value_(std::move(value)) {}

    [[nodiscard]] const std::string& value() const noexcept { return value_; }
    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }

private:
    std::string value_;
};

}  // namespace sacm::model
CMEOF

write_if_missing "$TARGET/include/sacm/validation/diagnostic.h" <<'CMEOF'
#pragma once

#include <string>
#include <vector>

namespace sacm::validation {

enum class Severity {
    Info,
    Warning,
    Error
};

struct Diagnostic {
    Severity severity = Severity::Error;
    std::string code;
    std::string requirement_id;
    std::string message;
    std::vector<std::string> affected_element_ids;
};

}  // namespace sacm::validation
CMEOF

write_if_missing "$TARGET/include/sacm/model/document.h" <<'CMEOF'
#pragma once

#include <string>
#include <vector>

#include "sacm/model/element_id.h"
#include "sacm/validation/diagnostic.h"

namespace sacm::model {

struct LangString {
    std::string language = "en";
    std::string text;
};

struct Claim {
    ElementId id;
    LangString content;
};

struct ArgumentPackage {
    ElementId id;
    LangString name;
    std::vector<Claim> claims;
};

struct AssuranceCasePackage {
    ElementId id;
    LangString name;
    std::vector<ArgumentPackage> argument_packages;
};

struct Document {
    std::string standard_version = "2.3";
    std::vector<AssuranceCasePackage> assurance_case_packages;
    std::vector<validation::Diagnostic> diagnostics;
};

}  // namespace sacm::model
CMEOF

write_if_missing "$TARGET/include/sacm/commands/mutation.h" <<'CMEOF'
#pragma once

#include <string>
#include <vector>

#include "sacm/model/element_id.h"
#include "sacm/validation/diagnostic.h"

namespace sacm::commands {

enum class ReferenceDeletePolicy {
    RejectIfReferenced,
    DeleteReferencingRelationships
};

enum class PackageDeletePolicy {
    RejectIfNonEmpty,
    DeleteRecursively
};

struct MutationSummary {
    std::string operation;
    std::vector<model::ElementId> created;
    std::vector<model::ElementId> changed;
    std::vector<model::ElementId> deleted;
    std::vector<validation::Diagnostic> diagnostics;
};

struct OperationPreview {
    MutationSummary summary;
    bool can_apply = false;
};

struct MutationResult {
    MutationSummary summary;
    bool applied = false;
};

}  // namespace sacm::commands
CMEOF

write_if_missing "$TARGET/include/sacm/io/xmi.h" <<'CMEOF'
#pragma once

#include <string>
#include <string_view>

#include "sacm/model/document.h"

namespace sacm::io {

struct LoadOptions {
    bool strict_sacm23 = true;
};

struct SaveOptions {
    bool strict_sacm23 = true;
};

struct LoadResult {
    model::Document document;
    bool ok = false;
};

LoadResult load_xmi_string(std::string_view xml, const LoadOptions& options = {});
std::string save_xmi_string(const model::Document& document, const SaveOptions& options = {});

}  // namespace sacm::io
CMEOF

write_if_missing "$TARGET/include/sacm/validation/validator.h" <<'CMEOF'
#pragma once

#include <vector>

#include "sacm/model/document.h"
#include "sacm/validation/diagnostic.h"

namespace sacm::validation {

std::vector<Diagnostic> validate(const model::Document& document);

}  // namespace sacm::validation
CMEOF

write_if_missing "$TARGET/include/sacm/compare/semantic_compare.h" <<'CMEOF'
#pragma once

#include <string>
#include <vector>

#include "sacm/model/document.h"

namespace sacm::compare {

struct SemanticDifference {
    std::string requirement_id;
    std::string message;
};

std::vector<SemanticDifference> semantic_compare(
    const model::Document& left,
    const model::Document& right);

}  // namespace sacm::compare
CMEOF

write_if_missing "$TARGET/tools/sacm_cli.cpp" <<'CMEOF'
#include "sacm/version.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc <= 1 || std::string(argv[1]) == "--version") {
        std::cout << "sacm library " << sacm::library_version()
                  << " standard " << sacm::standard_version() << '\n';
        return 0;
    }

    std::cerr << "sacm_cli scaffold supports only --version until the first slice is implemented\n";
    return 2;
}
CMEOF

write_if_missing "$TARGET/tests/test_smoke.cpp" <<'CMEOF'
#include "sacm/version.h"
#include "sacm/commands/mutation.h"
#include "sacm/model/document.h"

#include <cassert>

int main() {
    assert(sacm::standard_version() == "2.3");

    sacm::model::Document document;
    assert(document.standard_version == "2.3");

    sacm::commands::OperationPreview preview;
    assert(!preview.can_apply);

    return 0;
}
CMEOF

cat > "$TARGET/README.md" <<'CMEOF'
# SACM library skeleton

This directory is a neutral SACM 2.3 library skeleton. It must remain independent of Assurance Forge UI, app, parser, core, AI, review, GSN visualization, layout, and project-specific data structures.

Expand through the SACM slice workflow only:

1. Record requirement IDs in docs/sacm/sacm-conformance-matrix.md.
2. Add failing tests and fixtures.
3. Implement the minimum library behavior.
4. Verify edit, validation, XMI, and semantic round-trip behavior.
5. Integrate Assurance Forge through an adapter/projection layer.

First target slice:

```text
create AssuranceCasePackage
create ArgumentPackage
create Claim
preview/apply delete Claim/package
validate
save/load strict SACM 2.3 XMI
semantic round-trip
```

Layout and GSN display terminology remain outside this library.
CMEOF

echo "created SACM library skeleton at $LIB_PATH"
echo "next: wire add_subdirectory($LIB_PATH) only after reviewing docs/sacm/sacm-library-architecture.md"
