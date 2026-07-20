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
