<!--
Keep this short. The goal is that a reviewer can tell what changed and why
without reading the diff first.
-->

## What and why

<!-- What this changes, and the problem it solves. Link the issue: Closes #123 -->

## Before / after

<!--
The observable difference. A sentence is fine; a small table is better when
there are several. Screenshots for UI changes.
-->

## Verification

<!--
What you actually ran, and what it said. "Tests pass" is not verification —
name them. If you added a test, say what it would catch, and confirm you saw
it fail before the fix.
-->

- [ ] `ctest` passes locally
- [ ] New or changed behaviour has a test
- [ ] I saw the new test fail before the fix (or explained below why not)

## Checklist

<!-- Delete rows that genuinely do not apply. Do not delete an unticked box you are unsure about. -->

- [ ] One responsibility. Pure moves and renames are not mixed with behaviour changes.
- [ ] Layer boundaries respected (`core`/`parser`/`sacm` include no ImGui or `app` headers; `ui` does not depend on `app`).
- [ ] User-visible strings go through `ui::i18n`, and `tools/i18n/regenerate_ja_po.py` was updated and run.
- [ ] Capability change → [`docs/features/feature-matrix.md`](../docs/features/feature-matrix.md) row added or updated, and the exporter re-run.
- [ ] Change under `libs/sacm/` → [`docs/sacm/sacm-conformance-matrix.md`](../docs/sacm/sacm-conformance-matrix.md) updated or a verification record added.
- [ ] No new compatibility, preservation, audit, undo or recovery behaviour was removed.

## Safety-case data

<!--
Answer this if the change touches parsing, serialization, migration, save/load,
audit, undo, or anything that reads or writes a user's assurance case.
Write "not applicable" otherwise — do not delete the section.
-->

- Can this change cause content in a user's SACM file to be lost, reordered, or
  reinterpreted?
- Do files written by the previous version still load?
- If an operation is unsupported, does it refuse visibly rather than silently
  dropping data?

## Notes for the reviewer

<!-- Anything you want looked at hardest, decisions you were unsure about, or work deliberately left out. -->
