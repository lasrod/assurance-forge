# 0007. Single GSN canvas with an explicit editor mode

- Status: Proposed
- Date: 2026-06-18
- Deciders: Assurance Forge maintainers

## Context

The GSN Pattern Editor needs a canvas to author patterns: core GSN shapes,
relationship routing, automatic layout, selection, drag, zoom/pan, copy/paste,
and undo/redo. Concrete GSN arguments already have all of this in one canvas and
one layout engine (`ui/gsn` + `core/gsn_layout`).

Patterns differ from concrete arguments in their *editing affordances*, not
their primitives: dialectic editing (counter argument / counter evidence /
defeated state, added in the GSN v3 challenge feature) must not exist inside a
pattern, while pattern-only abstractions (uninstantiated decorator, optionality,
multiplicity, choice) apply only inside a pattern. We could fork the canvas for
patterns, but a fork would duplicate the most valuable and most actively
developed code in the app and would drift over time.

We also need the user to be able to tell at a glance that they are editing a
pattern rather than a concrete argument, and we must guarantee that prohibited
operations cannot end up in a pattern even if invoked programmatically or via
audit replay — not merely greyed out in a menu.

## Decision

We will use **one GSN canvas and one layout engine** for both concrete arguments
and patterns, gated by an explicit mode `core::GsnEditorMode { Argument,
Pattern }`.

Concretely:

- A pattern opens as an argument-package canvas tab whose `is_pattern` flag is
  set from `core::IsPatternPackage`. `AppRuntime::CurrentEditorMode()` derives
  the mode from the active tab.
- The mode flows into `ui::ElementContextActions::editor_mode`. In Pattern mode
  the dialectic context-menu actions are **hidden** (not just disabled), and the
  corresponding action callbacks are left unset.
- The command layer is the backstop: `core::AddChallenge` rejects any challenge
  whose target element belongs to a pattern package, so replay and programmatic
  callers cannot introduce dialectic state into a pattern regardless of UI.
- The Pattern View is visibly distinct: the canvas tab is labelled
  "Pattern: &lt;name&gt;" and a "Pattern" badge is drawn at the top of the canvas.
- Node layout, relationship routing, zoom/pan, selection, drag, editing,
  copy/paste, and undo/redo remain the shared implementation; no canvas fork.

## Consequences

- Pattern authoring inherits every improvement to the shared canvas and layout
  engine, and vice versa; there is a single place to fix rendering or
  interaction bugs.
- Mode is determined by the data (`IsPatternPackage`) rather than a free-floating
  UI flag, so the editing context cannot disagree with what the model actually
  is.
- Prohibited dialectic operations are blocked in two layers (hidden in the UI,
  rejected in the command layer), satisfying the rule that a pattern can never
  carry counter/defeated state.
- New pattern-only affordances must be mode-gated rather than added
  unconditionally, and regression tests must confirm Argument-mode behaviour is
  unchanged.
- Builds on [ADR-0002](0002-layered-architecture-with-build-time-gates.md)
  (layered architecture) and [ADR-0006](0006-gsn-patterns-as-abstract-argument-packages.md)
  (patterns as abstract ArgumentPackages).
