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
| `src/legacy_sacm` | Legacy SACM model types, parsing and serialization, predating `libs/sacm`. | `review/`, `ai/`, `export/`, `ui/`, `app/` |
| `src/parser` | XML parsing into the flat POD model, SACM model building, and SCCG guideline-catalog loading. | `legacy_sacm/`, `sacm/`, `review/`, `ai/`, `export/`, `ui/`, `app/` |
| `src/sacm_adapter` | The seam between `libs/sacm` and the application: case projection, library-backed document edits, library load, GSN role tagging. | `review/`, `ai/`, `export/`, `ui/`, `app/` |
| `src/core` | UI-independent domain behaviour: tree building, add/remove logic, project model, problems, reviews, drafts, audit. | `review/`, `ai/`, `export/`, `ui/`, `app/` |
| `src/review` | Review methods: what to review, SCCG profile selection, data packaging, prompt and result contracts, result parsing and validation — see [ADR 0013](decisions/0013-review-methods-independent-of-inference-providers.md). | `ai/`, `export/`, `ui/`, `bridge/`, `agent/`, `mcp/`, `app/` |
| `src/ai` | AI settings, provider calls, normalized responses, background task execution, secret storage. Inference only — it never parses a review result. | `review/`, `export/`, `ui/`, `app/` |
| `src/export` | SVG export of GSN diagrams: its own projection, layout and renderer, separate from the canvas. | `review/`, `ai/`, `ui/`, `app/` |
| `src/ui` | ImGui rendering, transient UI state, the GSN canvas, panels and widgets. | `review/`, `ai/`, `export/`, `app/` |
| `src/bridge` | Local transport between the MCP adapter and the running application: protocol, endpoint, transport. | `review/`, `ai/`, `export/`, `ui/`, `agent/`, `mcp/`, `app/` |
| `src/agent` | Operations an external agent can request — read, change, draft, placement — independent of transport. May use `review/` so external clients get the same review method as the built-in path. | `ai/`, `export/`, `ui/`, `mcp/`, `app/` |
| `src/mcp` | The MCP server: JSON-RPC, session, tools, guidance. Its own executable entry point. Reaches review behaviour only through `agent`. | `review/`, `ai/`, `export/`, `ui/`, `app/` |
| `src/app` | Runtime orchestration, controllers, project workflow, modal state, command handling. May include anything. | — |

`sacm/` now names exactly one thing: the reusable library under `libs/sacm`.
The legacy model answers to `legacy_sacm/`, so an include states which
subsystem it comes from instead of requiring a filesystem check to find out
([#341](https://github.com/lasrod/assurance-forge/issues/341)). The layer gate's
SACM-independence rule bans the whole `legacy_sacm/` prefix as a result, where it
previously had to match on the header stem (`sacm/sacm_`) to tell the two apart.

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
- **Keep review methods and inference apart.** `review` owns what a review asks
  and how its result is validated; `ai` owns talking to a provider. Neither
  includes the other, and `app` composes them
  ([ADR 0013](decisions/0013-review-methods-independent-of-inference-providers.md)).
- **Keep `ai` provider-neutral above the provider boundary.** Request assembly
  and response normalization must not depend on a specific service.
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

The gate is itself tested. `layer_gate_negative_check` feeds it thirteen
forbidden dependencies it must reject and six allowed ones it must not — a gate
that passes on a clean tree is indistinguishable from one that has stopped
working.

### Third-party dependencies

Each target declares what it uses. This is the map; `src/*/CMakeLists.txt` is
what the build enforces.

| Subsystem | Public — in its headers | Private — sources only |
|---|---|---|
| `parser` | — | pugixml, yaml-cpp |
| `sacm` | — | pugixml |
| `sacm_adapter` | `sacm::sacm` | — |
| `core` | — | picosha2 |
| `review` | — | — |
| `ai` | — | libcurl |
| `export` | — | — |
| `ui` | Dear ImGui | hello_imgui |
| `bridge`, `agent`, `mcp` | — | — |
| `app` | Dear ImGui | hello_imgui, nfd |

`nlohmann_json` is not in the table: it is in the public headers of `core`,
`bridge`, `agent` and `mcp`, so most of the tree meets it by inclusion rather
than by convenience, and it stays on `af_common` with the `src/` include root.
Header-only, so that is an include path rather than a link. **`af_common` is not
a place to put the next dependency** — anything added there goes to twelve
targets to spare one of them a line.

Until [#291](https://github.com/lasrod/assurance-forge/issues/291), `af_common`
carried all seven third-party libraries for every target. `export` and
`sacm_adapter` used none of them, `core` used two, `ui` used one — and the SVG
exporter could `#include "imgui.h"` and compile. It no longer can.

`af_sacm_adapter` was already built this way, and is where the pattern came
from: the src include root, `sacm::sacm`, and a comment saying why it is the
only target that links the library.

### What the gate does not cover

It checks source-level `#include` directives, not CMake target dependencies.
Every subsystem still receives the whole `src/` include path from `af_common`,
because a cross-layer include is written as `core/app_state.h` and has to
resolve from the tree root. So each layer can still *compile* against every
other layer's headers, and the source scan remains the only thing stopping it.

Confining that too would mean giving each layer its own include root and prefix
directory. That is a larger restructure than narrowing the link surface was, and
it is not scheduled.

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
