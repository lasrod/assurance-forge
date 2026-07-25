---
slice: phase-9-int-002-delete-preview-integration
date: 2026-07-25
verdict: FAIL
requirements: [SACM23-INT-001, SACM23-INT-002]
commit: 2da697d + working tree (branch sacm23/libsacm-gsn-context-preservation)
verifier: sacm-conformance-verifier
---

## Verification result

FAIL. Superseded by
[2026-07-25-int-002-delete-preview.md](2026-07-25-int-002-delete-preview.md)
after the findings below were fixed. Kept per
[README.md](README.md): "A `FAIL` record is still committed. A verification
history that only contains passes is not evidence of quality, it is evidence of
selective recording."

The library-side seam was well built and the five tests of the day were genuine.
The slice failed on two things: the preview described an operation the
application does not perform for `RemoveMode::NodeOnly`, and the test cited as
preventing preview/apply drift did not compare against the path the app
dispatches. Separately, SACM23-INT-001's matrix note and the matching header
comment stated the wrong delete policy — the exact behaviour INT-002 is built on.

## Scope

As in the superseding record; the same files and both test trees were inspected,
with the full suite at 778/778 passing at the time.

## Findings

| Severity | Requirement ID | Finding |
|---|---|---|
| Major | SACM23-INT-002 | The preview described a different operation than the app performs for `RemoveMode::NodeOnly`. `BuildRemovalPreview` was not given `mode` and modelled every removal as N × `DeleteElement{ScrubReferences}`, but `RemoveElementCommand::Apply` restricts the library seam to `NodeAndDescendants`; `NodeOnly` falls through to `core::RemoveElement`, which calls `ReparentChildrenToParent` first — retargeting a child's inference onto the grandparent. The library instead cascades that inference away, because its only target is doomed. For any interior node the dialog would state "Will be removed: &lt;the child's inference&gt;" when that inference in fact survives, and would say nothing about the children being promoted. Worse, this was a *new* dialog: before the change a single-id NodeOnly plan deleted with no confirmation. |
| Major | SACM23-INT-002 | `SACM23_INT_002_DeletePreviewMatchesWhatApplyDoes` was weaker than the matrix claimed. It compared against `sacm_adapter::apply_delete_element` — the library seam — for one id on a fixture whose answer is trivially `{G2, R1}`. It never dispatched `core::commands::RemoveElementCommand` (what the UI runs), never touched `core::RemoveElement` (what runs for NodeOnly), and never exercised a multi-id set — the very case the scratch-copy technique exists for. |
| Major | SACM23-INT-001 | The matrix note said `apply_delete_element` uses `DeleteReferencingRelationships`; the code uses `ScrubReferences`. The same stale claim was repeated in the public header comment. INT-001 was being proposed for `verified` with a note contradicting the implementation. |
| Major | SACM23-INT-002 | The utility-element filter hid ACP destruction. `is_attachment` dropped every `TaggedValue` effect, but ACPs are encoded as `assuranceForge.acp.*` TaggedValues and surfaced in a dedicated panel. Deleting `G2` cascades `R1`, which carries `ACP2` and a `topGoalReference` to a confidence package; the user was told the inference would go and never that an ACP died with it. The filter's rationale ("attachments deleted with an owner that is already listed") does not hold when the owner is itself a consequential deletion the user never selected. |
| Minor | SACM23-INT-002 | Preview and apply disagreed on unresolvable ids: the preview silently skipped them, `RemoveElementCommand` treated the same id as a hard failure after deleting earlier ids in the cascade. |
| Minor | SACM23-INT-002 | `DeletePreview` carried no `document_revision`, discarding the library's SACM-CMD-003 staleness guarantee at the seam. |
| Minor | SACM23-INT-002 | The disclosure omitted contract items named in `sacm-editing-policy.md` §"Operation preview"; `can_apply` was computed and never read, and `AF-PREVIEW-001` diagnostics were discarded on the `!supported` return. |
| Minor | SACM23-INT-002 | An all-unresolvable preview was presented as a successful preview with an empty consequence list. |
| Info | SACM23-INT-001 | `command_bus.h:71-77` stale comment about the flip gate — the comment that made the apply path look purely legacy. |
| Info | SACM23-INT-001 | A mid-cascade delete failure leaves earlier deletions applied with no audit event. No reachable trigger constructed. |
| Info | SACM23-LIB-002 | Noticed in passing: the LIB-002 note still claimed a legacy-parser fallback removed in f282f3a. |
| Info | SACM23-INT-002 | `AF-PREVIEW-001` / `AF-SEED-001` adapter codes shown to the user with no registry. |

Checks that passed and were recorded: the scratch-copy technique is sound
(`const LibraryDocument&`, Tolerant→Tolerant round trip, live document provably
untouched); set semantics are correctly not a per-element union and the test
genuinely discriminates it; the layer boundary is clean (strings and bools only,
no `sacm::model` into `core`/`app`, layer gate passed); i18n catalog green.

## Matrix updates allowed

- **May mark verified:** none.
- **Must remain open:** `SACM23-INT-002`, `SACM23-INT-001`.
