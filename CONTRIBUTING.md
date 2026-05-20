## 🧱 Engineering Principles

Assurance Forge is a model-driven tool for safety case development.

The architecture should support correctness, traceability, and user trust.

### Core Principles

- SACM XML represents the assurance case and is treated as the source of truth
- The tool must not introduce ambiguity or alter the meaning of assurance data
- Users must be able to clearly separate their production data from the tool itself
- Visualization and tooling must always be derived from the underlying model
- The system should remain deterministic and reproducible

---

### Data and Trust

- User data belongs to the user at all times
- No data may be sent externally without explicit user consent
- AI integrations must be transparent and user-controlled
- The tool must not silently modify or reinterpret safety arguments

---

### Quality and Testing

- Core functionality must be covered by tests
- Changes to parsing, serialization, or model handling require validation
- Round-trip integrity (import → export) should be preserved where possible

---

### Design Direction

When contributing, aim to:

- Keep the architecture simple and modular
- Maintain a clear separation of concerns
- Avoid introducing hidden behavior or implicit assumptions
- Ensure the tool scales to large and complex safety cases

---

### Scope Awareness

Assurance Forge is not a general-purpose diagram editor.

Contributions should reinforce its role as a safety case engineering tool.

---

## Code Contribution Guidelines

These guidelines are inspired by large readable C++ projects such as Godot, but adapted to Assurance Forge's smaller scope and use of the C++ standard library.

### Start With the Problem

Every non-trivial change should be tied to a concrete user, data, review, or maintenance problem.

- Describe the problem before the solution.
- Avoid future-proofing for workflows that do not exist yet.
- Prefer one focused solution for one real problem.
- Add extension points only when there is an immediate caller or a clearly planned near-term use.
- Prefer maintainability and clarity over cleverness or speculative flexibility.

### Keep Solutions Local

Prefer putting code near the workflow that needs it. Move behavior into a shared layer only when the reuse is real and the new owner is obvious.

- Do not add helpers to `core` for a single UI or app caller.
- Do not make a parser, serializer, or model API broader just to simplify one distant call site.
- Small local duplication is acceptable when it keeps a core API easier to understand.
- Shared abstractions should remove meaningful complexity, not merely reduce line count.

### Respect Module Boundaries

- `core` owns UI-independent domain behavior.
- `parser` and `sacm` own file formats, parsed data, SACM model types, and serialization.
- `app` owns runtime orchestration, controllers, project workflow, and command handling.
- `ui` owns immediate-mode rendering and transient UI state.
- `ai` owns AI settings, prompt construction, provider calls, response parsing, and AI task execution.
- `external` contains third-party or submodule code and should not be reformatted as part of normal project edits.

Lower layers should not depend on higher layers. In particular, `core`, `parser`, and `sacm` must not depend on ImGui or `app`.

### C++ Readability

Assurance Forge uses C++23 with the standard library. Keep modern C++ usage conservative and readable in a browser-based code review.

- Prefer explicit types over `auto` unless the type is noisy, repeated, or impractical to spell.
- Prefer named helper functions over lambdas when the logic is reused or non-trivial.
- Prefer small functions with clear ownership of errors and side effects.
- Use full words for names; avoid abbreviations unless they are established domain terms such as SACM, GSN, SCCG, or AI.
- Keep comments sparse and useful. Explain intent, constraints, or non-obvious behavior rather than restating the code.
- Include the matching header first in `.cpp` files, then project headers, then third-party/system headers.
- Keep third-party style untouched in `external`.

### Error Handling and Tests

- Use clear error messages that name the failed operation or missing resource.
- Prefer result structs or `bool` plus an error string for recoverable workflow errors.
- Changes to parsing, serialization, model mutation, project workflow, or AI response handling should include focused tests.
- Round-trip behavior should be preserved for assurance-case data whenever possible.
- Runtime bundled data that is required by the app should have a build or test check that verifies it is copied into the target runtime directory.

### Formatting

The repository root contains `.clang-format` and `.editorconfig` for project-owned files. Formatting should be mechanical and separate from behavior changes when practical. Do not reformat files under `external` unless intentionally updating vendored code according to that upstream's rules.
