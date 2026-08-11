# 0012. A claim carries one Description

- Status: Accepted
- Date: 2026-08-11
- Deciders: Jesper Brännström

## Context

The application models a claim-like element (Claim, ArgumentReasoning) with two
text slots: a statement (POD `content`) and a note (POD `description`). SACM
clause 8.9 gives a ModelElement `Description[0..1]` in the specification text;
the machine-readable model permits `[0..*]`, and `libs/sacm` stores a vector so
that full validation can flag surplus entries.

The two slots are carried as ordinal positions in that list: slot 0 is the
statement, slot 1 is the note. `apply_text_edit` writes them with
`SetDescription` and `SetDescriptionAt`, and `case_projection` reads them back.
Where a claim has only one Description, the projection **mirrors** slot 0 into
the POD `description` field, so the app shows the same text in both places.

This arrangement is the last thing keeping `UpdateElementText` on the legacy
bridge (#350 phase 3). Replay is measured against the legacy mutator, and the
two legacy states — "content set, description empty" and "description set,
content empty" — collapse to one library state, so the seam cannot reproduce
both.

Measuring the corpus (14 files, 139 claim-like elements) settled what the two
slots are actually used for:

| Shape | Count |
|---|---|
| Two Descriptions, slot 0 == slot 1 | 120 |
| Two Descriptions, slot 0 != slot 1 (a real note) | **0** |
| One Description | 7 |
| No Description | 12 |

Not one claim in the repository uses the second slot for distinct text. The 120
duplicates are the mirror being written back to disk: the hand-authored
`gsn-pattern-decorators` case carries one Description per claim, while the three
`kitchen-blender` projects — which have been round-tripped through the
application — carry two identical ones, the second tagged `lang="en"` from the
mirrored POD field. Every save through the bridge materialises a redundant
Description that clause 8.9 does not sanction.

Assurance Forge is not in production use. Conformance to the standard is worth
more here than compatibility with the shape the tool has been writing.

## Decision

**A claim-like element carries exactly one Description: its statement.**

- The POD `content` field is that Description (slot 0), and remains the only
  text slot the application offers for a claim or an argument reasoning.
- `case_projection` stops mirroring slot 0 into the POD `description` field for
  claim-like elements; that field projects empty for them.
- `apply_text_edit` reports `TextField::Description` as unsupported for
  claim-like elements. It stays fully supported for every other kind —
  relationships, artifacts and references legitimately carry a `<description>`
  that is a note rather than a statement, and they keep it.
- Slot 1 is never written for a claim-like element, so no new duplicate is
  produced.
- **No migration is provided.** A file already carrying a duplicate second
  Description keeps it on disk until it is next saved, at which point it is
  written with one. Existing users are expected to resolve any consequence
  themselves; this is recorded in the release notes rather than automated.

## Consequences

Positive:

- The written file matches clause 8.9 rather than exceeding it, and the surplus
  entry that full validation flags stops being produced.
- The claim-text divergence that keeps `UpdateElementText` bridged disappears,
  which unblocks #350 phase 3 and, behind it, `ApplyProposal` — the promotion
  path MCP drafts run through.
- One text box per claim is what every case in the repository already contains,
  so the interface matches the data.
- The mirror, its duplicate write, and the slot-1 mapping all go away together.

Negative, and accepted:

- **A note can no longer be attached to a claim separately from its statement.**
  Nothing in the repository does this, but it is a capability being removed, not
  merely unused.
- **A pre-existing duplicate is dropped on next save without asking.** For the
  120 measured cases the dropped text is identical to the kept text, so nothing
  is lost; for a hand-authored file that put distinct text in a second
  Description, it is a silent loss. This is the one place the decision trades
  against "the tool must never silently modify a safety argument", and it is
  accepted only because the tool is pre-production.
- Audit events recording an `UpdateElementText` against a claim's Description
  describe an edit the model no longer has a slot for. How replay treats them
  needs its own answer and is not settled here.

Follow-up:

- The capability matrix row for claim text needs to state one slot, not two.
- The release notes must say that a second Description on a claim is dropped on
  save.
