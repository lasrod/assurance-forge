# Phase 9 Stage 3 — projection baseline

Stage 3 loads every repository fixture through both the legacy parser and the
SACM library, projects the library document into the same POD model the
application renders from, and compares. **The differences are the deliverable.**
Each one is either a projection bug to fix or a legacy behaviour to consciously
drop, and Stage 4 — making the library the source of truth — is gated on this
list reaching zero.

Nothing in the application depends on the library yet. The measurement comes
first precisely so that migrating does not silently change what users see.

Test: `tests/test_sacm_library_parallel_load.cpp`
Baseline: `tests/data/sacm_parallel_load_baseline.json`

The test fails on any *new* difference and on any baseline entry that no longer
occurs, so the list can only shrink and cannot be padded.

## Current state: 98 differences across 6 fixtures

| Fixture | element-missing | field |
|---|---:|---:|
| `data/oasc-ja.xml` | 19 | — |
| `data/open-autonomy-safety-case.sacm.xml` | 19 | — |
| `tests/data/fixture_roundtrip_open_autonomy.sacm.xml` | 19 | — |
| `tests/data/fixture_roundtrip_sample.sacm.xml` | 2 | 4 |
| `tests/data/fixture_roundtrip_sanitized_strict.sacm.xml` | — | 34 |
| `tests/data/fixture_roundtrip_core_argument.sacm.xml` | — | 1 |

## The three causes

### 1. Terminology expressions are dropped by the library (59 of 98)

**This is a data-loss defect, and the most important finding of Stage 3.**

Assurance Forge's own files write terminology expressions as a containment
shorthand:

```xml
<expression id="TERM_SAFE" value="System operates without causing harm" />
```

The library treats `expression` as a **reference role** — in SACM 2.3 an
`Expression` is contained in a `TerminologyPackage` under the
`terminologyElement` role, and `expression` names the association *to* one. So
these elements are not read at all: they are absent from the library document
entirely, not merely absent from the projection.

Every `TERM_*` entry in the table above is one lost expression. Migrating
without fixing this would silently drop terminology from existing projects.

**Action: fix in the library reader** — tolerant mode should accept the
containment shorthand, as it already does for other legacy spellings. Tracked
separately; not fixed here because Stage 3 is a measurement slice.

### 2. `undeveloped` means different things in each model (6)

The legacy parser reads a GSN `undeveloped="true"` XML attribute. The projection
derives it from SACM `assertionDeclaration = needsSupport`, per
`docs/sacm/sacm-gsn-mapping.md`.

These are genuinely different sources for the same idea, and the fixtures show
they disagree: claims carrying `assertionDeclaration="needsSupport"` without the
GSN attribute project as undeveloped but do not render as undeveloped today.

**This needs a product decision, not a code fix.** Adopting the SACM reading
would change which goals display the undeveloped diamond in existing projects —
a visible change to a safety argument's appearance. Options: keep the GSN
attribute as the source, adopt the SACM declaration, or treat either as
sufficient. Recorded rather than decided.

### 3. The library reads descriptions the legacy parser misses (3)

Cases where legacy yields an empty description and the library yields real text,
e.g. `G2.description: legacy '' vs projected 'Hazards are identified'`.

Here **the library is right and the legacy parser is losing data.** These three
resolve themselves on migration; they are listed so the count reconciles and so
nobody "fixes" the projection to match a worse parser.

## Deliberate exclusions

Two things are excluded from the comparison rather than reported as differences,
because reporting them would be noise:

- **Packages** (`AssuranceCasePackage`, `ArgumentPackage`, `ArtifactPackage`).
  The POD model's `elements` are the nodes the application draws; packages are
  containers. The legacy parser lists neither.
- **Utility elements** (`Description`, `Note`, `TaggedValue`,
  `ImplementationConstraint`). These are metadata carried *on* elements, not
  elements in their own right.
- **Assurance Claim Points.** Synthesized by the application from vendor
  TaggedValues rather than read from SACM, so they are not part of this
  projection. See `docs/sacm/sacm-gsn-metamodel-gaps.md`.

Excluding these took the count from roughly 400 to 98, and the remainder are all
substantive.

## What Stage 4 needs

1. Fix cause 1 in the library reader — it is real data loss.
2. Decide cause 2 as a product question.
3. Cause 3 resolves on migration.

When the baseline reaches zero, `SACM23-INT-001` can move from `implemented`
toward `verified` and the library can become the source of truth.
