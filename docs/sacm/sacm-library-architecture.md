# SACM library architecture

## Purpose

Create a standalone C++23 library for OMG SACM 2.3 that can be used by Assurance Forge and by other tools.

The library provides:

- SACM 2.3 model representation.
- SACM-native editing and mutation semantics.
- Operation previews for destructive edits.
- XMI import/export.
- Validation and diagnostics.
- Identity/reference resolution.
- Semantic comparison and deterministic export helpers.
- Conformance metadata and test support.
- Explicit compatibility helpers that do not weaken strict SACM 2.3 behavior.

## Non-goals

The reusable library does not provide:

- ImGui rendering.
- GSN canvas layout.
- Deterministic visual layout algorithms.
- Coordinates or drawing state.
- Assurance Forge project files.
- AI review prompts or provider calls.
- UI localization.
- Application runtime or modal workflows.
- GSN-facing terms such as Goal, Strategy, Solution, canvas node, or tree item in the core API.
- SCCG review semantics, except as external clients may reference SACM data.

## Proposed layers inside the library

```text
sacm::metadata
  Constants, standard version, metamodel inventory, requirement IDs.

sacm::model
  Typed SACM elements, packages, IDs, references, language strings, standard data.

sacm::commands
  SACM-native edit operations, operation previews, mutation results, delete policies.

sacm::io
  XMI/XML readers and writers, namespace handling, source locations, parser errors.

sacm::validation
  Semantic validation, multiplicity checks, reference resolution, type restrictions, diagnostics.

sacm::compare
  Semantic equivalence and canonicalization helpers for tests and tools.

sacm::compat
  Explicit legacy or third-party compatibility behavior, disabled for strict export unless requested.
```

## Public API shape

Illustrative only; agents should refine through tests and architecture review.

```cpp
namespace sacm {

struct VersionInfo {
    std::string_view library_version;
    std::string_view standard_version; // "2.3" for the first implementation.
};

namespace model {
class Document;
class AssuranceCasePackage;
class ArgumentPackage;
class Claim;
class ElementId;
class Reference;
class LangString;
}

namespace commands {

enum class ReferenceDeletePolicy {
    RejectIfReferenced,
    DeleteReferencingRelationships
};

enum class PackageDeletePolicy {
    RejectIfNonEmpty,
    DeleteRecursively
};

struct CreateAssuranceCasePackage;
struct CreateArgumentPackage;
struct CreateClaim;
struct DeleteClaim;
struct DeletePackage;

struct OperationPreview;
struct MutationResult;

} // namespace commands

namespace io {
struct LoadOptions;
struct SaveOptions;
struct LoadResult;
LoadResult load_xmi_file(std::filesystem::path const& path, LoadOptions const& options = {});
LoadResult load_xmi_string(std::string_view xml, LoadOptions const& options = {});
std::string save_xmi_string(model::Document const& document, SaveOptions const& options = {});
}

namespace validation {
struct Diagnostic;
std::vector<Diagnostic> validate(model::Document const& document);
}

namespace compare {
struct SemanticDifference;
std::vector<SemanticDifference> semantic_compare(model::Document const& a, model::Document const& b);
}

}
```

A `Document` may expose `preview(...)` and `apply(...)`, or equivalent free functions may be used. The architectural requirement is explicit SACM-native mutation with structured results, not a specific method name.

## Ownership model

Recommended default:

- `Document` owns top-level packages and global metadata.
- Containment owns child elements.
- References are stored as stable IDs or typed reference handles.
- Resolver indexes are derived and rebuildable.
- Public APIs avoid raw owning pointers.
- Mutating operations preserve IDs unless explicitly creating new elements.
- Generated IDs are stable after creation; caller-provided IDs are preserved when valid.

## Mutation model

Public mutation operations should be atomic:

```text
success -> document changed and remains valid for the implemented slice
failure -> document unchanged and diagnostics returned
```

Destructive operations should support previews. A preview must identify affected elements before mutation so clients can show consequences to humans.

Mutation results should include:

```text
operation name
created element IDs
changed element IDs
deleted element IDs
deleted relationship IDs
diagnostics
validation summary
machine-readable metadata usable for audit/undo design
```

Undo/redo design remains open, but mutation data must not make it impossible.

## XMI policy

- Import must be namespace-prefix independent.
- Export must be deterministic.
- Strict SACM 2.3 export must use SACM 2.3 names and namespaces.
- Strict export must not include Assurance Forge layout metadata.
- Compatibility export must be clearly labeled and tested separately.
- Parser must disable unsafe XML features such as external entities.
- Source locations should be preserved where feasible for diagnostics.
- Whitespace formatting is not semantic; standard elements and attributes are.
- Semantic round-trip is the baseline; exact textual round-trip is not required.

## Extension and unsupported-content policy

Recommended policy:

- Standard SACM elements must be typed to count as implemented compliance.
- Unknown vendor extensions may be preserved in compatibility mode with warnings.
- Unknown standard-looking elements must not be silently accepted as implemented.
- During migration, unsupported valid SACM content should be preserved where practical.
- If unsupported content cannot be preserved safely, load/save/edit operations should reject or block with diagnostics rather than silently drop data.

## Code generation policy

Use the normative SACM XML/metamodel to generate or verify inventories, coverage maps, and possibly metadata tables.

Do not blindly generate the entire public C++ API unless the team accepts that API. A hybrid approach is safer:

- Generate/check metamodel inventory.
- Generate test expectations or coverage tables.
- Hand-write public model and command APIs for clarity.
- Add generated metadata behind stable public interfaces if useful.

## Assurance Forge adapter seam

Suggested adapter location, subject to repository conventions:

```text
src/sacm_adapter/ or src/adapter/sacm/
```

Adapter responsibilities:

- Convert library document into UI tree/GSN/evidence/terminology projections.
- Compute deterministic layout outside the SACM library.
- Map UI commands to SACM library commands.
- Use library operation previews to explain delete consequences before applying them.
- Surface diagnostics.
- Keep projected state rebuildable and discardable.
- Ensure save/export goes through the library serializer.

Adapter anti-patterns:

- Re-serializing SACM from UI tree state.
- Treating GSN nodes as the full SACM model.
- Storing hidden SACM standard data only in UI objects.
- Adding Assurance Forge fields or layout fields to library model classes for convenience.
- Using Goal/Strategy/Solution terms in the core SACM library API.
