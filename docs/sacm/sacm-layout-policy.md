# Layout and representation policy

## Decision

Layout, rendering, visual representation, canvas coordinates, tree positions, and deterministic GSN presentation are not SACM concerns and must not be part of the reusable SACM library.

Assurance Forge may use deterministic layout so the same SACM data always appears the same way inside Assurance Forge. That can be compliant because strict SACM exchange concerns the SACM data and XMI semantics, not the visual layout chosen by a tool.

## SACM library responsibility

The library is responsible for:

```text
SACM 2.3 model data
containment
references
identity
validation
editing semantics
XMI import/export
semantic round-trip
strict and compatibility modes
machine-readable diagnostics
```

The library is not responsible for:

```text
node positions
canvas lanes
tree expansion state
visual grouping
layout algorithms
localized labels
GSN display terms such as Goal or Strategy
Assurance Forge project UX
```

## Assurance Forge responsibility

Assurance Forge may deterministically project SACM data into UI concepts:

```text
SACM Claim -> GSN Goal display node
SACM AssertedInference -> GSN reasoning/strategy-like display
SACM AssertedEvidence -> GSN evidence/solution-like display
SACM ArtifactReference -> evidence-related display
SACM package containment -> tree/folder display
```

This projection is a client concern. It must be rebuildable from the library document.

## Save/export rule

Strict SACM 2.3 save/export should not include Assurance Forge layout metadata.

If layout metadata is ever needed, prefer one of these options outside strict SACM export:

```text
separate Assurance Forge project file
separate sidecar file
explicit compatibility/vendor-extension mode
```

Strict export should remain clean SACM 2.3 XMI.

## SACM 2.4 changes the fallback, not the decision

Draft SACM 2.4 adds `SACMView` / `SACMDiagram` (§15.3), built on OMG Diagram
Definition/Interchange. **This policy survives**: `SACMView` is an *optional*
compliance point, the mandatory one is Packaging, and strict export at the
mandatory point still needs no diagram data. Layout stays out of the library and
out of strict output.

What changes is the third option above. Once `SACMDiagram` exists, a
vendor-extension encoding is the wrong target — the standard-aligned one is
`SACMDiagram` + `SACMDiagramElement`. Worth knowing what SACM actually
standardises there: that a diagram exists, which model elements each diagram
element denotes, and an opaque rendered `representation` (e.g. SVG). It defines
**no geometry** — coordinates come from DD/DI.

Practical consequence, and the only thing to act on now: keep the deterministic
layout module's output expressible as (element reference → geometry or rendered
representation) pairs, because that is the shape `SACMDiagramElement` takes.
No code change is warranted while 2.4 remains unpublished and every RTF issue is
still open — see `docs/sacm/sacm-2.4-watch.md`.

## Testing implication

SACM library tests should not assert visual layout. Assurance Forge adapter/UI tests may assert deterministic layout separately, but those tests do not prove SACM conformance.
