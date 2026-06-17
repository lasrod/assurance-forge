# 0002. Layered architecture with build-time gates

- Status: Accepted
- Date: 2026-06-17
- Deciders: Assurance Forge maintainers

## Context

Assurance Forge is an ImGui desktop application that must remain correct,
testable, and trustworthy for safety-case engineering. ImGui is an
immediate-mode UI library whose state and rendering concerns tend to leak into
whatever code touches it. Without a clear structure, domain logic (model
mutation, tree building, parsing) and UI rendering blur together, which makes
the core behavior hard to test in isolation and easy to corrupt.

We need the domain and data-model code to be independently testable and free of
UI concerns, and we need that boundary to hold over time rather than erode as
contributors take shortcuts.

## Decision

We will organize the codebase into layers with a strict, one-directional
dependency rule (lower layers never depend on higher ones):

| Layer | Owns |
| --- | --- |
| `sacm` | SACM model types, XML parsing, serialization |
| `parser` | XML parsing, SACM model building, SCCG catalog loading |
| `core` | UI-independent domain behavior (tree building, add/remove, project model) |
| `ai` | AI settings, prompt construction, provider calls, response parsing, task execution |
| `ui` | ImGui rendering, transient UI state, GSN canvas, panels, widgets |
| `app` | Runtime orchestration, controllers, project workflow, modal state, command handling |

`core`, `parser`, and `sacm` must never include ImGui or `app` headers. `ui`
must not depend on `app` directly; panels receive action objects passed in from
`AppRuntime`.

The rule is enforced at build time by `cmake/check_layer_gates.cmake`, so a
violating include fails the build rather than relying on review discipline.

## Consequences

- Domain logic in `core`, `parser`, and `sacm` can be unit-tested without an
  ImGui context.
- Accidental upward dependencies are caught mechanically at build time, not in
  code review.
- `core` is deliberately kept small: helpers are added there only when behavior
  is reusable domain logic with no UI, file-dialog, or provider dependency — not
  for a single `ui` or `app` caller.
- There is some indirection cost: `ui` panels communicate through callbacks and
  action objects wired by `app` rather than calling domain code directly, and
  new cross-layer interactions must be designed to respect the gates.
