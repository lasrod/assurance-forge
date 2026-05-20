# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

**Windows** (run from Developer Command Prompt for VS 2022):
```bash
git submodule update --init --recursive
cmake --preset default
cmake --build --preset release
cmake --build --preset debug
```

**Linux** (install `xorg-dev libgl1-mesa-dev libglu1-mesa-dev libgtk-3-dev` first):
```bash
cmake -B build -DHELLOIMGUI_DOWNLOAD_GLFW_IF_NEEDED=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Run the app:**
```bash
build\Release\assurance-forge.exe          # Windows
./build/assurance-forge                    # Linux
```

## Test Commands

```bash
ctest --preset release                     # Windows (all tests)
ctest --preset debug                       # Windows (debug build)
ctest --test-dir build --output-on-failure # Linux / macOS

build\Release\tests.exe                    # Run test binary directly (Windows)
./build/tests                              # Linux / macOS

# Run a single test by name (GoogleTest filter):
build\Release\tests.exe --gtest_filter=TestSuiteName.TestName
./build/tests --gtest_filter=TestSuiteName.TestName
```

## Architecture

Assurance Forge is a C++23 ImGui desktop application for safety case engineering. It parses and produces **SACM 2.3 XML** and visualizes assurance arguments using **Goal Structuring Notation (GSN)**.

### Layers (low → high, no upward dependencies)

| Layer | Owns |
|-------|------|
| `sacm` | SACM model types, XML parsing, serialization |
| `parser` | XML parsing, SACM model building, SCCG guideline catalog loading |
| `core` | UI-independent domain behavior (tree building, add/remove logic, project model) |
| `ai` | AI settings, prompt construction, provider calls, response parsing, background task execution |
| `ui` | ImGui rendering, transient UI state, GSN canvas, panels, widgets |
| `app` | Runtime orchestration, controllers, project workflow, modal state, command handling |

**The dependency rule is enforced at build time** by `cmake/check_layer_gates.cmake`. `core`, `parser`, and `sacm` must never include ImGui or `app` headers. `ui` must not depend on `app` directly — panels receive action objects passed in from `AppRuntime`.

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

## C++ Style

- C++23, standard library. Column limit: 120. Format with `clang-format` (LLVM-based, config in `.clang-format`).
- Prefer explicit types over `auto` unless the type is noisy or impractical to spell.
- Prefer named helper functions over non-trivial lambdas.
- Full words for names; abbreviations only for established domain terms (SACM, GSN, SCCG, ACP).
- In `.cpp` files: matching header first, then project headers, then third-party/system headers. **`SortIncludes` is disabled** — preserve include order manually (Windows headers and some third-party headers have order dependencies).
- Do not reformat files under `external/`.
- Prefer result structs or `bool` + error string for recoverable errors.

## Key Constraints

- **Keep `core` small.** Add to it only when behavior is reusable domain logic with no UI, file-dialog, or provider dependency. Do not add helpers there for a single `ui` or `app` caller.
- **Changes to parsing, serialization, model mutation, or AI response handling require tests.**
- **Round-trip integrity** (import → export) must be preserved for assurance-case data.
- SACM XML is the source of truth; the tool must never silently modify or reinterpret safety arguments.
- No data is sent externally without explicit user consent — AI integrations are transparent and user-controlled.
