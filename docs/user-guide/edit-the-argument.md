# Edit the argument

Every edit goes through an audited command: it is recorded, it can be undone,
and a change the SACM library cannot express is **refused with the file
unchanged** rather than applied approximately. That refusal is a feature — see
[SACM as the source of truth](../architecture/decisions/0003-sacm-xml-as-source-of-truth.md).

## Add an element

Right-click a node on the canvas or a row in the argument navigator:

| Menu | Adds |
|---|---|
| **Add → Goal / Strategy / Solution / Context / Assumption / Justification** | A child of the selected element, in GSN terms — a Claim, ArgumentReasoning, ArtifactReference, or a context-class element in SACM terms ([mapping](../sacm/sacm-gsn-mapping.md)) |
| **Add → ACP** | An assurance claim point on the selected element; a relationship can carry one too, from the ACP marker the canvas draws on eligible edges. Each ACP can hold its own confidence argument |
| **Add → Add Counter Argument / Add Counter Evidence** | A dialectic challenge — drawn as a dashed open-arrow edge |

The **Add** menu adds files rather than elements: **New GSN / SACM File** adds another argument
file to the project, and **New Evidence Register** / **New J3377 CAE Register** add register files.

## Remove an element

**Remove** offers *this node only* and *this node and its descendants*, each
with the count it would remove. The confirmation lists what actually goes —
including the relationships and any glossary term citing the element — as
previewed by the SACM library, so the consequence is visible before the press,
not after.

## Edit text

The inspector edits **Name** and **Content** for the selected element. A claim
carries one description; the reasoning is
[ADR 0012](../architecture/decisions/0012-a-claim-carries-one-description.md).

**Undeveloped** marks a goal or strategy that is deliberately not developed
further. It renders as the GSN undeveloped diamond and is reported by the
well-formedness checks, so an undeveloped goal is a recorded decision rather
than a gap that reads like an oversight.

## Two languages

An element's text can carry a second language. Pick the language under
**Translation Language** in the inspector, tick **Add Translation**, and write
the secondary text; the canvas shows it beneath the primary, and the
[SVG export](export-a-diagram.md) can be taken in either.

Text written by a machine translation is **flagged for a reviewer**: it stays
marked until a person reads it and accepts it as an explicit act. A translation
is part of the argument, so it is reviewed like the rest of it.

## Undo

**Edit → Undo** (`Ctrl+Z`) steps back through the audited commands. Undo has
explicit boundaries — accepting a working draft is one of them — so a single
`Ctrl+Z` cannot half-unpick a composite change.

## What edits are refused

- A shape the SACM library has no seam for: the file is left byte-unchanged and
  the reason is reported.
- Glossary edits that the working-draft vocabulary cannot express, while a draft
  is open — renaming the terminology package, deleting a package or category,
  linking a term to an element as context.
- Register cells with no way to record what you typed.

A refusal always names what it refused and leaves the argument as it was.
