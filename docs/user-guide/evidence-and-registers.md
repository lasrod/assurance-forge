# Evidence and registers

Two register views turn the argument into a table: the **evidence register**
(what evidence exists and what rests on it) and the **CSE register** (claim /
evidence pairings and the judgement recorded about each). Both are derived from
the SACM model — the register is a view, not a second store — and both are
opened from the Case Explorer or **View**.

## Evidence register

One row per piece of evidence, **including evidence nothing cites**, which is
listed as unlinked rather than hidden. The Case Explorer shows that count beside
the register.

| Column group | Where it is stored |
|---|---|
| Owner, type, maturity, controlled environment | `TaggedValue`s (`assuranceForge.evidence.*`) on the `Artifact` the reference cites |
| Version, date | The Artifact's provenance (SACM clause 12.7); `Recency` becomes the date |
| Notes | The Artifact's Description |
| Location | The `location` of the Resource the ArtifactReference cites (clause 12.12) |

These are Assurance Forge conventions inside standard SACM structures: another
SACM tool will preserve them, but will not interpret them as register columns.

### What you can do from a row

- **Locate** — opens the owning package's canvas with the element selected.
- **Set a location** — type a path or a URL, or **browse** for a file. A file
  inside the project is recorded relative to the project root, so the case stays
  portable.
- **Open** — opens the recorded location. Two caveats: this is **implemented on
  Windows only** (elsewhere it reports that opening is unavailable), and the
  location is handed to the operating system as recorded. Treat locations inside
  a case you did not author with the same care as any other link.
- **Add evidence** — creates a Solution with a statement, under a chosen claim
  or bare (registered before anything rests on it).
- **Used by** — lists the claims resting on this evidence, with an unlink for
  each and a picker to link another.
- **Remove** — after a confirmation listing what goes with it.

On the canvas, evidence with a recorded location carries a **link badge** that
opens it, and the [SVG export](export-a-diagram.md) carries the link too.

## CSE register

One row per claim / evidence pairing, with the assessment columns — owner,
criteria, status, notes — stored on the `AssertedEvidence` relationship that
carries the support being judged. Where one relationship carries several
pairings, those rows **share one assessment** and are marked as sharing it.

Each row has a **Show in argument** button, which replaced the raw id columns:
an opaque storage id is not how a reader finds the support being judged.

## Assessments from older projects

Assessments used to live in a project sidecar (`registers/register-assessments.af.json`).
Those are still shown and editable, and the register's **Move into SACM** action
imports them as one audited transaction; two rows sharing a relationship but
disagreeing are refused rather than one silently overwriting the other. Nothing
is rewritten when a project is opened, because the SACM file is the argument.

!!! warning "Moving assessments while a working draft is open"
    Under a working draft the import is staged as a draft edit, but the
    project-file copy is released immediately. If you then discard the draft and
    save, those assessments are gone from both places. Accept the draft, or move
    the assessments when no draft is open.
