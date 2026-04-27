# Assurance Forge Architecture

This document captures the current intended shape of the application. It is deliberately small: the goal is to keep the code easy to trace, not to introduce a framework.

## Layers

- `app`: owns runtime orchestration, layout composition, file workflow state, modal state, and commands that mutate the loaded project.
- `core`: owns application/domain operations that are independent of ImGui, such as tree building and add/remove behavior.
- `parser` and `sacm`: own XML parsing, SACM model types, and serialization.
- `ui`: owns immediate-mode rendering, transient UI state, and small shared UI helpers.
- `ui/panels`: owns larger panel/modal surfaces.
- `ui/widgets`: owns reusable low-level widgets.
- `ui/gsn`: owns GSN canvas model, layout, rendering, and adapter code.

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
