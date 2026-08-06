# Layers and ownership

Which subsystem owns what, and which direction dependencies are allowed to run.

This page is the **canonical source** for subsystem responsibilities and the
dependency rule. `cmake/check_layer_gates.cmake` enforces the same rule at
configure time, and `tools/docs/check_documentation.py` fails if this page stops
describing a subsystem the gate checks — the two drifted apart before, and the
result was four architecture documents that each omitted a different part of the
application.

## Subsystems

Every top-level directory under `src/`, plus the reusable library. The forbidden
column is taken from the layer gate; it is what the build actually rejects, not
an aspiration.

| Directory | Owns | Must not include |
|---|---|---|
| `libs/sacm` | The reusable SACM 2.3 library: model types, XMI import/export, validation, commands, semantic comparison. Independent of Assurance Forge — see [ADR 0006](decisions/0006-sacm-23-independent-library.md). | Any Assurance Forge header; ImGui; pugixml in public headers |
| `src/sacm` | Legacy SACM model types, parsing and serialization, predating `libs/sacm`. | `ai/`, `export/`, `ui/`, `app/` |
| `src/parser` | XML parsing into the flat POD model, SACM model building, and SCCG guideline-catalog loading. | `sacm/`, `ai/`, `export/`, `ui/`, `app/` |
| `src/sacm_adapter` | The seam between `libs/sacm` and the application: case projection, library-backed document edits, library load, GSN role tagging. | `ai/`, `export/`, `ui/`, `app/` |
| `src/core` | UI-independent domain behaviour: tree building, add/remove logic, project model, problems, reviews, drafts, audit. | `ai/`, `export/`, `ui/`, `app/` |
| `src/ai` | AI settings, prompt construction, provider calls, response parsing, background task execution, secret storage. | `export/`, `ui/`, `app/` |
| `src/export` | SVG export of GSN diagrams: its own projection, layout and renderer, separate from the canvas. | `ai/`, `ui/`, `app/` |
| `src/ui` | ImGui rendering, transient UI state, the GSN canvas, panels and widgets. | `ai/`, `export/`, `app/` |
| `src/bridge` | Local transport between the MCP adapter and the running application: protocol, endpoint, transport. | `ai/`, `export/`, `ui/`, `agent/`, `mcp/`, `app/` |
| `src/agent` | Operations an external agent can request — read, change, draft, placement — independent of transport. | `ai/`, `export/`, `ui/`, `mcp/`, `app/` |
| `src/mcp` | The MCP server: JSON-RPC, session, tools, guidance. Its own executable entry point. | `ai/`, `export/`, `ui/`, `app/` |
| `src/app` | Runtime orchestration, controllers, project workflow, modal state, command handling. May include anything. | — |

`src/sacm` and `libs/sacm/include/sacm` both answer to the `sacm/` include
prefix, so `#include "sacm/model/document.h"` is the library while
`#include "sacm/sacm_parser.h"` is not. Resolving which one an include names
requires checking the filesystem. That ambiguity is recorded in the
[repository quality baseline](../quality/repository-baseline.md) and tracked in
[#291](https://github.com/lasrod/assurance-forge/issues/291).

## Ownership rules

Keep the code that solves a problem close to where the problem originates.
Shared abstractions earn their place only when they make more than one real
workflow simpler.

- **Keep `core` small.** Add to it only when the behaviour is reusable domain
  logic with no UI, file-dialog, project-workflow or provider dependency. Do not
  add a helper there for a single `ui` or `app` caller. `core` is currently the
  largest subsystem in the repository, so this rule is under real pressure.
- **Keep `app` orchestration.** Controllers coordinate user workflows; domain
  invariants belong in `core`, `parser` or the SACM layers when they do not
  depend on the UI shell.
- **Keep `ui` render-only.** Panels receive state plus small action objects
  rather than reaching into application internals.
- **Keep `ai` provider-neutral above the provider boundary.** Prompt assembly
  and response validation must not depend on a specific service.
- **Keep external data explicit.** A bundled runtime asset gets one discovery
  and copy path, with tests for the copies that matter.

When adding a file, choose the lowest layer that can own the behaviour without
importing a higher one.

## State ownership

Three owners, kept apart so that reusable UI components never carry application
workflow state:

| Owner | Holds |
|---|---|
| `core::AppState` | Loaded project data, and file load/save behaviour |
| `ui::UiState` | Cross-panel view state: selection, language toggle, active center view, transient canvas navigation |
| `AppRuntime::Impl` | Application workflow state — modals, animations — that must not live in a reusable UI component |

Large UI surfaces receive the state and actions they need as parameters. Small
stateless widgets stay plain functions.

The field-level detail for the first two lives in
[Runtime and State](runtime-state.md) and [UI Panels](ui-panels.md); this page
states only who owns what.

## The dependency rule

Dependencies run one way: lower layers never include higher ones. The gate scans
`#include` directives at configure time and fails the build with a
`FATAL_ERROR`, so a violation cannot reach `main` unnoticed.

**The allow-list is empty, and stays that way.** It previously held two entries —
`preferences_panel.h` reaching into `ai/` and `welcome_modal.h` into `app/` —
both removed by giving each panel its own view type and mapping onto it in `app`.
See [ADR 0011](decisions/0011-panels-own-their-view-types.md).

Removing an exception means inverting the dependency, extracting an interface, or
relocating the type, never rewording the rule. An entry that is genuinely
unavoidable needs its own ADR and an issue to remove it.

The gate is itself tested. `layer_gate_negative_check` feeds it nine forbidden
dependencies it must reject and four allowed ones it must not — a gate that
passes on a clean tree is indistinguishable from one that has stopped working.

### What the gate does not cover

It checks source-level `#include` directives, not CMake target dependencies.
`af_common` is an INTERFACE target giving every subsystem the whole `src/`
include path and the full third-party link surface, so each layer can already
compile against every other layer's headers — the source scan is the only thing
stopping it. Measured against actual usage:

| Subsystem | Actually uses | Also links |
|---|---|---|
| `core` | picosha2, nlohmann_json | imgui, pugixml, nfd, yaml-cpp, curl |
| `parser` | pugixml, nlohmann_json, yaml-cpp | imgui, nfd, picosha2, curl |
| `ui` | imgui | pugixml, nfd, picosha2, nlohmann_json, yaml-cpp, curl |
| `app` | imgui, nfd, nlohmann_json | pugixml, picosha2, yaml-cpp, curl |
| `export`, `sacm_adapter` | *(none)* | all seven |

Narrowing this is open under
[#291](https://github.com/lasrod/assurance-forge/issues/291).

UI code should not depend on `app` directly. When a panel needs to request a
command, `AppRuntime` passes a small action object into it — which keeps the
dependency visible at the call site and preserves the immediate-mode style.

Prefer a local helper over widening a `core` API for one caller. Move it only
once the repeated use is real and the new home is obvious.

## HelloImGui scope

HelloImGui provides the platform runner, window creation, event loop, DPI
scaling, macOS bundling and preferences persistence. Assurance Forge keeps its
own `NoDefaultWindow` layout and custom menu flow.

User-facing appearance is deliberately limited to the two themes exposed by
`ui::AppTheme`: `Dark` and `Light`. HelloImGui's built-in theme names survive
only as a persistence bridge for the existing INI file; the Assurance Forge theme
layer applies the final ImGui style, migrates old or invalid saved names to
`Dark` or `Light`, and owns the semantic palette for GSN nodes, canvas, edges,
status severities and attention states.

Higher-level HelloImGui features — docking layouts, default layout management,
status bars, logging windows, theme tweak windows, asset image utilities —
remain outside the architecture. Domain colours flow through `ui::GetTheme()` or
the semantic colour helpers, never through a local hardcoded `ImVec4`.

Application chrome localization uses the Assurance Forge message catalog. That is
separate from SACM model translations, which are part of the parsed
assurance-case data.

## Core data

Safety Case Core Guidelines are tracked as the
`external/safety-case-core-guidelines` submodule. Assurance Forge consumes the
generated SCCG distribution rather than the authored source tree: the build
copies `dist/sccg.full.yaml` into each target runtime directory as
`data/sccg.full.yaml`, and release packaging overlays it into the shipped `data`
folder. Runtime discovery prefers `data/sccg.full.yaml`.

After cloning:

```bash
git submodule update --init --recursive
```

If the SCCG submodule is present but `dist/sccg.full.yaml` is missing, regenerate
the distribution in the SCCG repository before configuring Assurance Forge.
