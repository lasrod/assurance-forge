# SACM slice brief

## Slice name

## Goal

## Requirement IDs

## Normative sources

## Included behavior

- Model elements:
- XMI import/export:
- Validation:
- Editing/mutation:
- Operation preview/destructive effects:
- CLI behavior, if any:
- Assurance Forge adapter behavior, if any:

## Excluded behavior

- Layout/visual representation remains outside the SACM library.
- GSN UI terminology remains outside the core SACM library.

## Tests to write first

| Test | Requirement IDs | Expected initial failure |
|---|---|---|

## Fixtures

| Fixture | Purpose |
|---|---|

## Acceptance criteria

- Public library API uses SACM terminology only.
- Public mutations leave the document valid for the supported slice or fail unchanged.
- Destructive operations provide affected-element previews where applicable.
- Strict save emits SACM 2.3 XMI with no Assurance Forge layout metadata.
- Semantic round-trip passes for the covered slice.

## Open questions

## Handoff notes
