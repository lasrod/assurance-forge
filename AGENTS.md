# AGENTS.md

## Build Commands

**Windows** (run from Developer Command Prompt for VS 2022):
```bash
git submodule update --init --recursive
cmake --preset default
cmake --build --preset release
cmake --build --preset debug
```

**Run the app:**
```bash
build\Release\assurance-forge.exe
```

## Architecture

Assurance Forge is a C++23 ImGui desktop application for safety case engineering. It parses and produces **SACM 2.3 XML** and visualizes assurance arguments using **Goal Structuring Notation (GSN)**.

### Layers (low → high, no upward dependencies)

> **Canonical source:** `docs/architecture/layers-and-ownership.md`. The table
> below is a convenience copy; where they disagree, that page wins.

| Layer | Owns |
|-------|------|
| `libs/sacm` | The reusable SACM 2.3 library: model, XMI I/O, validation, commands. Independent of Assurance Forge |
| `sacm` | Legacy SACM model types, parsing, serialization, predating `libs/sacm` |
| `parser` | XML parsing, SACM model building, SCCG guideline catalog loading |
| `sacm_adapter` | The seam between `libs/sacm` and the app: projection, library-backed edits, library load |
| `core` | UI-independent domain behavior (tree building, add/remove logic, project model, drafts, audit) |
| `ai` | AI settings, prompt construction, provider calls, response parsing, background task execution |
| `export` | SVG export of GSN diagrams — its own projection and renderer, separate from the canvas |
| `ui` | ImGui rendering, transient UI state, GSN canvas, panels, widgets |
| `bridge` | Local transport between the MCP adapter and the running application |
| `agent` | Operations an external agent can request, independent of transport |
| `mcp` | The MCP server: JSON-RPC, session, tools. Its own executable |
| `app` | Runtime orchestration, controllers, project workflow, modal state, command handling |

**The dependency rule is enforced at build time** by `cmake/check_layer_gates.cmake`. `core`, `parser`, and `sacm` must never include ImGui or `app` headers. `ui` must not depend on `app` directly — panels receive action objects passed in from `AppRuntime`. `bridge` and `agent` must not include each other's transport or `mcp`.

Note that `src/sacm` and `libs/sacm/include/sacm` share the `sacm/` include prefix: `sacm/model/document.h` is the library, `sacm/sacm_parser.h` is not.

### Frame & Interaction Flow

```
AppRuntime::RenderFrame
 → rebuild derived views if model is dirty
 → render splitters and panels
 → panels update UiState or invoke action callbacks
 → AppRuntime command handlers mutate AppState / core model
 → AppRuntime sets tree_needs_rebuild
 → next frame: RebuildDerivedViewsIfNeeded refreshes AssuranceTree, registers, canvas
```

User interaction → `UiState` (visual/selection) or `ElementContextActions` (model mutations) → `AppRuntime` handles command → `core` mutates `parser::AssuranceCase` → derived views rebuilt next frame.

### State Ownership

- `core::AppState` — loaded project data, file load/save
- `ui::UiState` — selected element, language toggle, active center view, transient canvas navigation
- `AppRuntime::Impl` — application workflow state (modals, animations) that shouldn't live in reusable UI components

### SCCG / AI Guidelines

Safety Case Core Guidelines live in `external/safety-case-core-guidelines` (git submodule). The build copies `external/safety-case-core-guidelines/dist/sccg.full.yaml` into each target runtime directory as `data/sccg.full.yaml`. Runtime discovery uses `data/sccg.full.yaml` first. If `dist/sccg.full.yaml` is missing after cloning, regenerate it in the SCCG submodule before configuring.

### HelloImGui Scope

HelloImGui provides the platform runner, window/event loop, DPI scaling, and preferences persistence. Assurance Forge keeps its own `NoDefaultWindow` layout and does not use HelloImGui's docking layouts, status bars, logging windows, or theme tweak windows. The two valid app themes are `Dark` and `Light` (defined in `ui::AppTheme`). Domain colors must flow through `ui::GetTheme()` or semantic color helpers — not local hardcoded `ImVec4` values.

## UI Localization

Every user-visible string goes through `ui::i18n`. Catalog source of truth: `tools/i18n/regenerate_ja_po.py`.

- Wrap with `AF_TR("literal")`, `ui::i18n::trf("{0}/{1}", a, b)` for dynamic, `trn`/`trnf` for plurals. Always a literal — `AF_TR(var)` is invisible to the extractor.
- Use real UTF-8 in source (`"● PAUSED"`, not `"\xe2\x97\x8f PAUSED"`).
- Window/popup titles double as ImGui IDs — translate visible part, keep ID stable: `(AF_TR("Title") + "###" + kStableEnglishId).c_str()`.
- Layer rule: `core/sacm/parser/ai` can't include `ui/i18n`. Store English msgids in data; the `ui/` panel translates at display with `AF_TR(field)`. `app/` may use `ui::i18n` directly for dynamic templating (`trf` at sync time).
- After adding/removing strings: add (or remove) the entry in `regenerate_ja_po.py`, then `python tools/i18n/regenerate_ja_po.py && cmake --build --preset release`.
- Caches that bake translations (e.g. `ProblemItem::message` built via `trf`) must refresh on language change. Use `ui::i18n::LanguageEpoch()` — see the existing hook in `AppRuntime::RenderFrame`.
- For runtime-formatted text use `trf("Name: {0}", value)` / `trnf(...)` with positional placeholders — never `AF_TR("Name: %s")` as a printf format string (translators can't manage `%`-specifiers or reorder them). Pass the result to ImGui as `"%s"` or `TextUnformatted`.
- CI enforces catalog consistency via the `i18n_catalog_check` CTest (runs `tools/i18n/check_catalog.py`): fails if a source msgid is missing from the `.po`, the committed `.mo` is out of sync, or a translated msgid contains a printf specifier (`%s`/`%d`/…). Run `python tools/i18n/check_catalog.py` before pushing.

## C++ Style

- C++23, standard library. Column limit: 120. Format with `clang-format` (LLVM-based, config in `.clang-format`).
- Prefer explicit types over `auto` unless the type is noisy or impractical to spell.
- Prefer named helper functions over non-trivial lambdas.
- Full words for names; abbreviations only for established domain terms (SACM, GSN, SCCG, ACP).
- In `.cpp` files: matching header first, then project headers, then third-party/system headers. **`SortIncludes` is disabled** — preserve include order manually (Windows headers and some third-party headers have order dependencies).
- Do not reformat files under `external/`.
- Prefer result structs or `bool` + error string for recoverable errors.

## SACM 2.3 library conformance (`libs/sacm`)

`libs/sacm` is an independent, reusable SACM 2.3 library; Assurance Forge is (to
become) one of its clients. Its compliance claims live in
`docs/sacm/sacm-conformance-matrix.md`, which is the canonical source of
requirement IDs — test names embed them.

- **Any change under `libs/sacm/include/` or `libs/sacm/src/` requires a matrix
  update or a verification record.** A behavioural change that leaves the matrix
  untouched silently invalidates whatever the matrix claims.
- **A row reaches `verified` only with a test whose name embeds its requirement
  ID** (e.g. `SACM23_XMI_004_...`) and a `sacm-conformance-verifier` pass
  recorded under `docs/sacm/verification/`. Prose evidence and "manual" steps do
  not count.
- Run `python tools/sacm/check_conformance_matrix.py` before pushing. CI enforces
  it via the `sacm_matrix_check` CTest, which fails on matrix rot: a verified row
  with no ID-bearing test, a test naming a requirement that does not exist, or a
  cited path that has moved.
- The library must stay independent — no Assurance Forge headers, no ImGui, no
  GSN/layout vocabulary in its public API. `cmake/check_layer_gates.cmake`
  enforces this at configure time.
- **SACM 2.3 does not determine an instance-document namespace URI** (the
  normative MOF model declares no `nsURI`). Ours is a project choice, and the EMF
  reference implementation uses a different one per package. Import accepts both
  dialects; strict export normalizes to our pin. Do not treat the pinned URI as
  normative.

## Capability matrix (`docs/features/feature-matrix.md`)

The canonical record of what Assurance Forge can actually do, separate from the SACM conformance matrix: that one answers "does the library implement the standard", this one answers "can a user do the thing".

- **Any user-visible capability change requires a matrix row change.** A new feature adds a row; a finished feature raises a status; a discovered limitation goes in Notes.
- **`supported` requires a cited test that exists.** `planned`, `candidate` and `not-planned` rows must cite no tests.
- After editing, run `python tools/features/export_feature_matrix.py` and `python tools/features/check_feature_matrix.py`. CI enforces both via the `feature_matrix_check` CTest.

## GSN work

GSN→SACM mappings are evidence-backed and recorded in `docs/sacm/sacm-gsn-mapping.md`; never invent one in code. The canvas (`src/ui/gsn/`) and the SVG export (`src/export/`) are separate renderers with separate models — a decorator added to one is absent from the other.

## Repository gates

All run under `ctest`. A change that trips one is not ready:

| Gate | Fails when |
|---|---|
| `i18n_catalog_check` | A source msgid is missing from the `.po`, or the committed `.mo` is stale |
| `sacm_matrix_check` | A `verified` row has no ID-bearing test, or a cited path moved |
| `gsn_matrix_check` | GSN taxonomy, statuses, or cited evidence drift |
| `feature_matrix_check` | A `supported` row cites no existing test, or the exported JSON is stale |
| `no_committed_artifacts_check` | A build log, test output or scratch file is tracked by git |
| `documentation_check` | A broken internal link, an unreachable page, an unmarked generated doc, or an architecture page missing a subsystem |
| `verification_index_check` | The verification index no longer matches the records' front matter |
| `evidence_package_check` | The release evidence package cannot be generated from this checkout — matrix unparseable, spec pins mismatch, or a frozen doc missing |

Formatting is applied on commit by the `.githooks/pre-commit` hook, installed by `cmake --preset default`. There is no CI format gate.

Documentation authority — which page is canonical for which policy — is `docs/documentation-map.md`.

## Key Constraints

- **Keep `core` small.** Add to it only when behavior is reusable domain logic with no UI, file-dialog, or provider dependency. Do not add helpers there for a single `ui` or `app` caller.
- **Changes to parsing, serialization, model mutation, or AI response handling require tests.**
- **Round-trip integrity** (import → export) must be preserved for assurance-case data.
- SACM XML is the source of truth; the tool must never silently modify or reinterpret safety arguments.
- No data is sent externally without explicit user consent — AI integrations are transparent and user-controlled.
