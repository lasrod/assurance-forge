# SACM conformance verification records

A conformance matrix row moves to `verified` only when a `sacm-conformance-verifier`
pass says it can. This directory is where those passes are recorded, so that
`verified` means something a reader can check rather than something a contributor
typed into a markdown cell.

## Why records exist

The matrix alone cannot distinguish a genuine verification from an edit. Before
these records existed, every row's status rested on trust, and three rows were in
fact marked `verified` with no test carrying their requirement ID. The
`sacm_matrix_check` CTest now catches that class of drift mechanically; these
records cover the part a script cannot judge — whether the tests actually
demonstrate what the requirement demands.

## Convention

- One file per verification pass: `<YYYY-MM-DD>-<slice-slug>.md`.
- Start from [TEMPLATE.md](TEMPLATE.md), which carries the verifier agent's own
  output format plus parseable front matter.
- The verifier does not write these files — it has no write tools, deliberately,
  so that it cannot fix what it judges. It reports; the implementation lead
  commits the record.
- A `FAIL` record is still committed. A verification history that only contains
  passes is not evidence of quality, it is evidence of selective recording.

## Running a verification

Invoke the `sacm-conformance-verifier` agent against the slice under review, then
save its report here. See
[../prompts/sacm-verification-prompt.md](../prompts/sacm-verification-prompt.md).

## The records

Newest first. **Generated** from each record's own front matter by
`tools/docs/generate_verification_index.py` — do not edit the table by hand.
Reading the metadata from the records is the point: a table assembled from
filenames records `2026-07-20-phases-0-8.md` as a pass, because it is the one
failing record whose name lacks the `-FAIL` suffix.

A failing round is kept deliberately. The rounds a requirement took are part of
what the evidence says, and a history containing only the round that worked
would overstate how settled it is.

<!-- BEGIN GENERATED: verification records -->

| Date | Requirements | Verdict | Record |
|---|---|---|---|
| 2026-08-08 | `SACM23-LIB-002` | Pass | [Lib 002 resolution](2026-08-08-lib-002-resolution-round-5.md) |
| 2026-08-08 | `SACM23-LIB-002` | **FAIL** | [Lib 002 resolution](2026-08-08-lib-002-resolution-round-4-FAIL.md) |
| 2026-08-08 | `SACM23-LIB-002` | **FAIL** | [Lib 002 resolution](2026-08-08-lib-002-resolution-round-3-FAIL.md) |
| 2026-07-26 | `SACM23-LIB-002` | **FAIL** | [Undo library primary](2026-07-26-lib-002-undo-library-primary-round-1-FAIL.md) |
| 2026-07-26 | `SACM23-LIB-002` | **FAIL** | [Strategy migration preserves unknown content](2026-07-26-lib-002-strategy-migration-round-2-FAIL.md) |
| 2026-07-25 | `SACM23-LIB-002` | Pass | [Phase 9 stage 7 source of truth](2026-07-25-lib-002-source-of-truth.md) |
| 2026-07-25 | `SACM23-LIB-002` | **FAIL** | [Phase 9 stage 7 source of truth](2026-07-25-lib-002-source-of-truth-round-2-FAIL.md) |
| 2026-07-25 | `SACM23-LIB-002` | **FAIL** | [Phase 9 stage 7 new project seed](2026-07-25-lib-002-source-of-truth-round-1-FAIL.md) |
| 2026-07-25 | `SACM23-INT-002` | Pass | [Phase 9 int 002 delete preview integration](2026-07-25-int-002-delete-preview.md) |
| 2026-07-25 | `SACM23-INT-001`, `SACM23-INT-002` | **FAIL** | [Phase 9 int 002 delete preview integration](2026-07-25-int-002-delete-preview-round-1-FAIL.md) |
| 2026-07-25 | `SACM23-INT-001` | Pass | [Phase 9 int 001 edit path](2026-07-25-int-001-edit-path.md) |
| 2026-07-25 | `SACM23-INT-001` | **FAIL** | [Phase 9 int 001 edit path](2026-07-25-int-001-edit-path-round-4-FAIL.md) |
| 2026-07-25 | `SACM23-COMPAT-001` | Pass | [Phase 10 gsn context preservation](2026-07-25-compat-mode-separation.md) |
| 2026-07-25 | `SACM23-COMPAT-002` | Pass | [Compat 002 third party corpus](2026-07-25-compat-002-third-party-corpus.md) |
| 2026-07-25 | `SACM23-COMPAT-002` | **FAIL** | [Compat 002 third party corpus](2026-07-25-compat-002-third-party-corpus-round-3-FAIL.md) |
| 2026-07-21 | `SACM23-XMI-003` | Pass | [Xmi 003 generated id collision](2026-07-21-xmi-003-generated-id-collision.md) |
| 2026-07-20 | `SACM23-LIB-001` and 28 more | **FAIL** | [Phases 0 8 library core](2026-07-20-phases-0-8.md) |

<!-- END GENERATED: verification records -->

Also here: [TEMPLATE.md](TEMPLATE.md), the starting point for a new record.

`SACM23-LIB-002` reached `verified` on its eighth recorded round — six of the
eight are FAILs, and two of those FAILs were probe-measured silent-loss paths
the whole test suite had missed: the strongest argument this directory makes
for adversarial verification.
Every non-`out-of-scope` matrix row is now `verified`; what that claim does and
does not mean is bounded by the
[compliance points](../sacm-compliance-points.md) and the
[completeness audit](../sacm-matrix-completeness-audit.md).
