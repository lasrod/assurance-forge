# Assurance Forge Architecture

This document captures the current intended shape of the application. It is deliberately small: the goal is to keep the code easy to trace, not to introduce a framework.

## Layers

- `app`: owns runtime orchestration, layout composition, file workflow state, modal state, and commands that mutate the loaded project.
- `core`: owns application/domain operations that are independent of ImGui, such as tree building and add/remove behavior.
- `parser` and `sacm`: own XML parsing, SACM model types, and serialization.
- `parser::GuidelinesParser`: owns loading and querying the generated SCCG catalog from the bundled `data/sccg.full.yaml` runtime asset.
- `ai`: owns AI settings, provider calls, prompt construction, response parsing, and background AI task execution.
- `ui`: owns immediate-mode rendering, transient UI state, and small shared UI helpers.
- `ui/panels`: owns larger panel/modal surfaces.
- `ui/widgets`: owns reusable low-level widgets.
- `ui/gsn`: owns GSN canvas model, layout, rendering, and adapter code.

## Ownership Rules

Assurance Forge borrows a simple rule from large readable C++ projects: keep the code that solves a problem close to where the problem originates. Shared abstractions are useful only when they keep more than one real workflow simpler.

- Keep `core` small. Add code there only when it represents reusable assurance-case domain behavior and has no UI, file-dialog, project-workflow, or provider dependency.
- Keep `app` as orchestration. Controllers may coordinate user workflows, but domain invariants should move into `core`, `parser`, or `sacm` when they are independent of the UI shell.
- Keep `ui` render-only. Panels should receive state plus small action objects or controller calls rather than reaching into application internals directly.
- Keep `ai` provider-neutral above the provider boundary. Prompt assembly and response validation should not depend on a specific AI service unless a provider implementation requires it.
- Keep external data explicit. Bundled runtime assets should have one discovery/copy path and tests for important copies.

When adding a new file, choose the lowest layer that can own the behavior without importing a higher layer. For example, `core` must not include ImGui or `app`, and parser code must not emit UI events.

## Frame Flow

```text
AppRuntime::RenderFrame
 -> rebuild derived views if the model is dirty
 -> render splitters and panels
 -> panels mutate UiState or invoke explicit action callbacks
 -> AppRuntime command handlers mutate AppState/core model
 -> AppRuntime marks derived views dirty
 -> next frame rebuilds tree/register/canvas views
```

## Interaction Flow

```text
User interaction
 -> UI view updates UiState for visual state, selection, or navigation
 -> UI view invokes ElementContextActions for model-changing commands
 -> AppRuntime handles the command
 -> core mutates parser::AssuranceCase and optional sacm::AssuranceCasePackage
 -> AppRuntime sets tree_needs_rebuild
 -> RebuildDerivedViewsIfNeeded refreshes AssuranceTree, registers, and canvas
```

## State Ownership

- `core::AppState` owns loaded project data and file load/save behavior.
- `ui::UiState` owns cross-panel UI state, such as selected element, language toggle, active center view, and temporary canvas navigation flags.
- `AppRuntime::Impl` owns application workflow state that should not live in reusable UI components.

Large UI surfaces should receive the state and actions they need as parameters. Small stateless widgets can stay as simple functions.

## Dependency Rule

UI rendering code should avoid depending directly on `app`. If a panel needs to request an application command, `AppRuntime` passes a small action object into that panel.

This keeps dependencies visible at the call site and preserves the simple immediate-mode style.

Prefer local helpers over widening core APIs for one caller. If a helper is needed by several modules, move it only after the repeated use is real and the new home is obvious.

## HelloImGui Scope

HelloImGui provides the platform runner, window creation, event loop, DPI scaling, macOS bundling, global ImGui themes, and application settings/user preferences. Assurance Forge keeps its manual `NoDefaultWindow` layout and custom menu flow, but generic widget styling now comes from HelloImGui themes and the selected theme is remembered through HelloImGui settings.

Higher-level HelloImGui features such as docking layouts, default layout management, status bars, logging windows, and asset image utilities remain outside the current architecture. The `ui` layer keeps a small semantic palette for GSN node colors, canvas colors, edge colors, and status severity colors because those values carry domain meaning beyond generic ImGui theme colors.

Application chrome localization is handled by a lightweight Assurance Forge message catalog. This is separate from SACM/model translations, which remain part of the parsed assurance-case data and UI state.

## Core Data

Safety Case Core Guidelines are tracked as the `external/safety-case-core-guidelines` submodule. Assurance Forge consumes the generated SCCG distribution artifact rather than the authored SCCG source tree. The build copies `external/safety-case-core-guidelines/dist/sccg.full.yaml` into each target runtime directory as `data/sccg.full.yaml`, and release packaging overlays that file into the shipped `data` folder.

Runtime discovery should prefer `data/sccg.full.yaml` and the SCCG submodule `dist/sccg.full.yaml` path. Temporary compatibility fallback to old `guidelines.yaml` locations can remain where it keeps existing user checkouts usable.

After cloning, initialize data dependencies with:

```text
git submodule update --init --recursive
```

If the SCCG submodule is present but `dist/sccg.full.yaml` is missing, regenerate the SCCG distribution in the SCCG repository before configuring Assurance Forge.
