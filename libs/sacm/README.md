# sacm — OMG SACM 2.3 library

Independent, editable C++23 implementation of the OMG Structured Assurance
Case Metamodel (SACM) 2.3. The library owns SACM model state, SACM-native
editing with operation previews, XMI import/export, validation, identity,
and semantic comparison. Assurance Forge consumes it through adapters; the
library itself must stay reusable by other tools.

## Boundary rules

- No Assurance Forge dependencies: nothing under `src/**`, no ImGui/UI/app
  types (enforced by `cmake/check_layer_gates.cmake` in the parent build).
- Strict SACM terminology only: no Goal/Strategy/Solution/canvas/layout
  concepts in the public API.
- pugixml is a private implementation detail; it never appears in public
  headers.
- Public mutations succeed and leave the document valid for the supported
  slice, or fail leaving it unchanged. Destructive operations provide
  previews and require explicit policies.
- Strict SACM 2.3 is the default save mode; compatibility behavior is
  explicit and separate.

## Layout

- `include/sacm/` public headers (`metadata`, `model`, `commands`, `io`,
  `validation`, `compare`)
- `src/` private implementation
- `tests/` GoogleTest suites + fixtures under `tests/data/sacm23/`
- `tools/sacm_cli.cpp` CLI smoke utility (`version`, `validate`,
  `roundtrip`, `export`)

## Building

In-tree: built automatically by the Assurance Forge root CMake.

Standalone:

```bash
cmake -S libs/sacm -B build-sacm-standalone
cmake --build build-sacm-standalone --config Debug
ctest --test-dir build-sacm-standalone -C Debug --output-on-failure
```

## Workflow

Development follows the slice workflow in
`docs/sacm/sacm-library-implementation-plan.md`: conformance-matrix rows →
failing tests → implementation → verification → matrix update. Requirement
IDs (`SACM23-*`) appear in test names. The metamodel source of truth is
`docs/sacm/sacm-2.3-metamodel-inventory.md`, generated from the normative
OMG machine-readable model.
